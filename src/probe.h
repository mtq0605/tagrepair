#pragma once
// =============================================================================
// 借 FFmpeg 看一个文件:容器 / 编码 / 音频数据里有没有同步标记 / FFmpeg 读到的三个标签 / 整轨样本的 MD5
// =============================================================================
// 什么样的文件才去找同步标记、找多少:
//   无损 PCM(TrueHD / DTS / DSD 不算)、两声道、整数样本 ≥ 16 bit、44.1 k 或 48 k 才找;
//   从头解最多 10 秒喂 syncdetect::Detector,找到同步标记且读到原始采样率就停;判据是 Detector::markerFound()。
// 文件一律经已打开的句柄读(自己的 AVIO):宽字符 / 长路径由调用方开,修复时全程用独占的那个句柄。
// =============================================================================
#include "tag_rule.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace probe {

// FFmpeg 的四个运行库:avutil、swresample(avcodec 自己要用)、avcodec、avformat。
// 文件名里的版本号在构建时按 FFmpeg 开发包定下
constexpr int kFfmpegDlls = 4;
const wchar_t* ffmpegDllName(int i);

// 找这四个运行库并装进来。四个要在同一个文件夹里、都装得进来才算数;不到系统的搜索路径里去找。找的顺序:
//   1. forcedDir(命令行 --ffmpeg-dir;给了就只看这一处)
//   2. 本程序所在的文件夹
//   3. 往上两级的文件夹(被别的程序装成 <那个程序>\plugins\tagrepair\tagrepair.exe 时,运行库在那个程序那一层)
//   4. 环境变量 TAGREPAIR_FFMPEG_DIR 指的文件夹
// 失败时 tried = 找过的文件夹
bool loadFfmpeg(const std::wstring& forcedDir, std::vector<std::wstring>* tried);
// 找的顺序本身(纯函数;tests/ 也核对它):exeDir = 本程序所在的文件夹,envDir = 环境变量的值(可空)。空的、重复的不列
inline std::vector<std::wstring> ffmpegSearchDirs(const std::wstring& forcedDir, const std::wstring& exeDir, const std::wstring& envDir) {
    std::vector<std::wstring> dirs;
    auto add = [&](std::wstring d) {
        while (d.size() > 3 && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
        if (d.empty()) return;
        for (const std::wstring& x : dirs)
            if (_wcsicmp(x.c_str(), d.c_str()) == 0) return;
        dirs.push_back(d);
    };
    if (!forcedDir.empty()) {                                 // 明说了去哪找:只看这一处,找不到就是找不到
        add(forcedDir);
        return dirs;
    }
    add(exeDir);
    std::wstring up = exeDir;
    for (int i = 0; i < 2 && !up.empty(); ++i) {
        const size_t s = up.find_last_of(L"\\/");
        up = s == std::wstring::npos ? std::wstring() : up.substr(0, s);
    }
    if (up.size() == 2 && up[1] == L':') up += L'\\';        // "C:" → "C:\"(不然是这个盘的当前目录)
    add(up);
    add(envDir);
    return dirs;
}

struct Info {
    bool opened = false;          // FFmpeg 打得开、有音轨
    msg::Text error;              // 打不开的原因
    std::string container;        // FFmpeg 的格式名("flac" / "wav" / "matroska,webm" …)
    std::string codec;            // "flac" / "pcm_s24le" …
    bool losslessPcm = false;
    int  rate = 0, channels = 0, bits = 0;
    bool sniffed = false;         // 符合条件、真去找过同步标记
    bool marker = false;
    int  origRate = 0;            // 码流里的原始采样率(0 = 没读到)
    bool has[tagrepair::kKeys] = { false, false, false };    // FFmpeg 的容器元数据里有没有这三个键
    std::string value[tagrepair::kKeys];
    std::vector<std::pair<std::string, std::string>> meta;   // FFmpeg 的容器元数据里的全部条目(键、值),按它给的顺序
};
// nameHint:文件名(只给 FFmpeg 按扩展名认格式用,和直接把路径交给 FFmpeg 的效果一样)
bool inspect(HANDLE file, const std::wstring& nameHint, Info& out);

// 整轨解码 / 编码时往外报的两样(都可以不给)
struct Watch {
    std::function<bool(double)> tick;                                 // 做到哪了(0..1);返回 false = 取消,立刻停
    std::function<void(const int32_t* lr, size_t samples)> sink;      // 解出的样本(交错、左对齐 int32;samples = 两个声道合计的个数)
};

struct Pcm {
    std::string md5;              // 所有解出的样本(交错、左对齐 int32)的 MD5
    uint64_t frames = 0;
    int errors = 0;               // 解码报错的包数(帧 CRC 也查)
    bool cancelled = false;       // watch->tick 说了停
};
bool decodeAll(HANDLE file, const std::wstring& nameHint, Pcm& out, msg::Text& err, const Watch* watch = nullptr);

// ---- 另存:把 src 的全部采样无损地编成一个新的 FLAC,写进 out(一个空的、可写的文件)----
// 采样值原样:位深不变时每个采样就是源里的那个数;padBits = 8 时(16 bit → 24 bit)每个采样 = 源采样 × 2^8,也就是
// 原来的 16 位原样放在高位、低 8 位补零 —— 不重新计算、不加抖动。
// 文件头("fLaC"、STREAMINFO)是这里自己拼的,音频帧用运行时装进来的 FFmpeg 里的 FLAC 编码器;
// STREAMINFO 之后的元数据块(注释、图片…)由调用方拼好给进来
struct FlacWrite {
    int outBits = 0;                                // 新文件的位深:16 或 24
    int padBits = 0;                                // 0 = 位深不变;8 = 16 bit 的采样放进 24 bit 的格子
    std::vector<uint8_t> metaAfterStreamInfo;       // 已经拼好的块,最后一块带着"最后一块"标志;不能为空
};
enum class WriteFail { None, Source, Format, NoEncoder, Encode, Write, Cancelled };
struct FlacWritten {
    WriteFail fail = WriteFail::None;
    msg::Text sourceError;                          // fail == Source:源打不开的原因
    unsigned long winError = 0;                     // fail == Write:系统错误码
    uint64_t frames = 0;                            // 每声道的采样数
    int decodeErrors = 0;                           // 解源文件时报错的包数
    int srcBits = 0, rate = 0, channels = 0;
    std::string srcMd5;                             // 源:交错、左对齐 int32 的 MD5(与 Pcm::md5 同一种算法)
    std::string srcNativeMd5;                       // 源:按它自己的位深小端排的 MD5
    uint8_t outMd5[16] = {};                        // 新文件的采样按新位深小端排的 MD5(写进它的 STREAMINFO)
};
bool writeFlacCopy(HANDLE src, const std::wstring& nameHint, HANDLE out, const FlacWrite& spec, FlacWritten& res, const Watch* watch = nullptr);

} // namespace probe
