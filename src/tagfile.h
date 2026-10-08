#pragma once
// =============================================================================
// 一个文件:看它是哪一类(scan)、把三个识别标签修好(fix)
// =============================================================================
// 修的时候守的几条(这是在改用户的音乐文件):
//   · 音频数据里没有同步标记的绝不写;结构走不通、解码有错、音频不完整的不碰;
//   · 标签完整的(tag_rule.h 的 tagsComplete)一个字节都不动;
//   · 只读的、被别的程序开着的不碰(独占打开,开不了就说明原因);
//   · 音频帧到文件末尾的字节前后逐字节相同(SHA-256 前后各算一遍),STREAMINFO 不变;
//   · 原地改写只在"长度能靠 PADDING 找平、而且改动的字节全落在同一个扇区里"时用(一个扇区的写入不会只写上一半);
//     其余一律在旁边写一个完整的新文件、核对无误后用 ReplaceFileW 换入 —— 任何时刻断电,留下的要么是旧文件、要么是新文件;
//   · 临时文件的名字每次都是新的(不会盖掉已有的文件);失败时删掉它、原文件保持原样。唯一不删的情况:
//     换入做到一半、原来的名字下已经没有文件了 —— 这时它是仅有的一份,留着,并且说清楚它在哪;
//   · 写完读回:自己的解析器与 FFmpeg 都要看到三个键、值对;FFmpeg 整轨解出的样本 MD5 与修之前相同。
//     原地改写读回不对就把原来的字节写回去;
//   · 取消(runctl.h):只读的阶段、写临时文件与核对临时文件时看到就停(临时文件删掉,原文件没动过);
//     原地写的那一下、换入开始之后不看 —— 这个文件做完再停。
// =============================================================================
#include "classify.h"
#include "flac_meta.h"
#include "probe.h"

#include <windows.h>

#include <functional>
#include <string>

namespace tagrepair {

struct Scan {
    probe::Info info;                     // FFmpeg 看到的
    bool nativeFlac = false;              // 容器是原生 FLAC
    bool flacOk = false;                  // 自己的解析器走得通(元数据块 + 注释块)
    const msg::Entry* flacWhy = nullptr;  // 走不通的原因
    flacmeta::Layout layout;
    Seen seen;                            // 三个键:原生 FLAC 用自己的解析器数(重复几份都数得出),别的容器用 FFmpeg 的
    unsigned bad = 0;                     // 每个键各自合不合格(只对有同步标记的文件有意义);完不完整看 tagsComplete(bad)
    std::string fin[kKeys];               // 修完 / 另存之后每个键的值(写得出来时)
    unsigned long openError = 0;          // 文件本身打不开时的系统错误码
    State state = State::Error;
    // 这个文件上能做的事
    bool canRepair = false;               // 原地修复标签(state == Repairable)
    bool canWavToFlac = false;            // 另存为 FLAC(state == Convertible)
    bool canCopy24 = false;               // 16 bit 的:另存一个 24 bit 的 FLAC(WAV 或者结构完好、标签写得出来的 FLAC)
};
bool scanHandle(HANDLE file, const std::wstring& name, Scan& out, const Rule& rule = defaultRule());
bool scanFile(const std::wstring& path, Scan& out, const Rule& rule = defaultRule());      // 共享只读打开,不妨碍别人

enum class Outcome { Fixed, Unchanged, WouldFix, Skipped, Refused, Failed, Cancelled };
constexpr int kOutcomes = 7;
const char* outcomeCode(Outcome o);

// 自检用的钩子(正常使用时全是空的)。模拟的都是真会发生、但平时造不出来的事:写盘失败、换入做到一半、读回对不上、做到一半被取消
struct TestHooks {
    std::function<void(HANDLE)> afterWrite;                                    // 原地写完、核对之前
    std::function<void(HANDLE)> afterTempWrite;                                // 临时文件写完、核对之前
    std::function<void(std::string& audioSha, std::string& pcmMd5)> expect;    // 修之前记下的两个指纹(改掉 = 模拟"前后对不上")
    std::function<bool()> writeOk;                                             // 写盘之前问一次;false = 当作写入失败(磁盘满)
    std::function<void()> beforeSwap;                                          // 放掉独占锁之后、换入之前
    std::function<bool(const wchar_t* replaced, const wchar_t* replacement, const wchar_t* backup, DWORD& err)> replaceFile;   // 代替 ReplaceFileW
    std::function<bool(const wchar_t* from, const wchar_t* to, DWORD flags, DWORD& err)> moveFile;                            // 代替 MoveFileExW
    // 走到哪一步了:scan、lock、hash、decode、write(原地写,从这里起不停)、temp、verify、swap(换入,从这里起不停)
    std::function<void(const char* phase)> phase;
    std::function<void()> duringTempWrite;                                     // 往临时文件拷音频时,每拷一段叫一次
};

struct FixOptions {
    bool dryRun = false;                  // 只说会怎么做,不写
    bool keepBackup = false;              // 原文件留一份(<文件名>.bak);这时一律走整文件重写,备份就是没动过的原文件
    Rule rule;                            // 写什么值、已有的什么值算合格(默认见 tag_rule.h)
    uint64_t sectorBytes = flacmeta::kSectorBytes;      // 原地改写的条件:改动落在同一个扇区里(0 = 一律整文件重写)
    std::function<void(double)> progress; // 这个文件做到哪了(0..1)
    TestHooks test;
};

struct FixReport {
    Outcome outcome = Outcome::Failed;
    std::string code;                     // 没修的原因(给程序读):read_only / in_use / access_denied / open_failed / damaged / too_big /
                                          // decode_error / incomplete_audio / changed / verify_failed / swap_failed / swap_incomplete /
                                          // io / internal / cancelled;修了或不用修为空
    msg::Text why;                        // 同一个原因,给人看的一句(两半)
    std::wstring reason;                  // = why.zh
    Scan scan;                            // 修之前
    bool inPlace = false;                 // 原地改写(false = 整文件重写后换入)
    unsigned wrote = 0;                   // 写了哪几个键(位图)
    Seen after;                           // 修完之后自己的解析器读回的
    std::string audioSha256;              // 音频帧到文件末尾的 SHA-256(前后相同才算修好)
    uint64_t audioBytes = 0;
    std::string pcmMd5;                   // FFmpeg 整轨解出的样本 MD5(前后相同才算修好)
    uint64_t pcmFrames = 0;
    std::wstring backup;                  // keepBackup 时备份放在哪
    std::wstring leftAt;                  // code == swap_incomplete:原来的名字下没有文件了,这首歌现在在这个路径
    int staleRemoved = 0;                 // 顺手删掉的、上次被打断留下的临时文件个数
};
FixReport fixFile(const std::wstring& path, const FixOptions& opt);

// 给人看的一句(两半):这个文件是哪一类
msg::Text stateMsg(const Scan& s);
inline std::wstring stateText(const Scan& s) { return stateMsg(s).zh; }
inline std::wstring stateTextEn(const Scan& s) { return stateMsg(s).en; }
inline std::wstring reasonEn(const FixReport& r) { return r.why.en; }
// 键名列出来(英文用逗号、中文用顿号)
msg::Text keyList(unsigned mask);

// 临时文件的名字:<文件名>.tagfix.<8 位十六进制>.tmp。isTempNameFor 判一个名字是不是 base 的临时文件名(自检也用)
bool isTempNameFor(const std::wstring& baseName, const std::wstring& candidateName);

// ---- 原地修复与另存(copy_flac.cpp)共用的几样 ----
namespace fileutil {

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { close(); }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
    void close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
};

// 临时文件:没走到改名 / 换入那一步就删掉。keep = 不删(已经改名成正式文件了;或者它是这首歌仅有的一份)
struct TempFile {
    std::wstring path;
    Handle f;
    bool keep = false;
    ~TempFile() {
        f.close();
        if (!keep && !path.empty()) DeleteFileW(path.c_str());
    }
};

// 在 path 旁边新建一个临时文件(<path 的文件名>.tagfix.<8 位十六进制>.tmp)。名字每次不同、只新建不覆盖
bool createTemp(const std::wstring& path, TempFile& tmp, DWORD& err);
bool exists(const std::wstring& longPath);

} // namespace fileutil

} // namespace tagrepair
