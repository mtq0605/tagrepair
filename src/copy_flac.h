#pragma once
// =============================================================================
// 另存:在原文件旁边新写一个 FLAC(原文件只读地打开,从不修改、改名、删除)
// =============================================================================
// 两种,各由一个明说的选项打开(不给选项,fix 不会新建任何文件):
//   WavToFlac   带同步标记的 WAV → <文件名>.flac:采样率、声道数、位深相同,无损编码,带三个识别标签,
//               外加 FFmpeg 从 WAV 里读得到的普通标签(标题、艺人、专辑……)
//   To24Bit     带同步标记的 16 bit 文件(WAV 或 FLAC)→ <文件名>.24bit.flac:每个 16 bit 的采样原样放在 24 bit 格子的高位,
//               低 8 位补零(采样值不变,不重新计算、不加抖动)。有的设备只认 24 bit 的文件
// 守的几条:
//   · 没有同步标记的绝不转;
//   · 从不覆盖已有的文件:目标名下已经有东西就拒绝这个文件(开始之前查一遍;最后改名那一步也不带"覆盖");
//   · 先写进旁边的临时文件,核对无误才改成正式的名字。核对 = 把新文件从头到尾解一遍,
//     采样个数、全部采样的 MD5 与源相同(24 bit 的拷贝另外逐个看:低 8 位全是零、高 16 位就是源的采样),
//     文件头里的 MD5 就是这些采样的,新文件扫描出来是"有同步标记、标签完整"、原始采样率与源相同;
//     有一处对不上就删掉临时文件、报失败;
//   · 取消(runctl.h):编码、核对的时候看到就停,临时文件删掉;核对通过之后只剩改名那一步,做完。
// =============================================================================
#include "tagfile.h"

namespace tagrepair {

enum class CopyKind { WavToFlac, To24Bit };
enum class CopyOutcome { Created, WouldCreate, Skipped, Refused, Failed, Cancelled };
constexpr int kCopyOutcomes = 6;
inline const char* copyKindCode(CopyKind k) { return k == CopyKind::WavToFlac ? "wav_to_flac" : "copy_24bit"; }
const char* copyOutcomeCode(CopyOutcome o);

// 16 bit 的采样放进 24 bit 的格子:左移这么多位(= 低位补这么多个零)
constexpr int kPadTo24 = 24 - 16;

// 自检用的钩子(正常使用时全是空的)
struct CopyHooks {
    std::function<void(const char* phase)> phase;     // 走到哪一步了:scan、encode、verify、rename
    std::function<void()> duringEncode;               // 编码时每读一包源数据叫一次
    std::function<void(HANDLE temp)> afterWrite;      // 临时文件写完、核对之前(改它 = 模拟写坏了)
    std::function<void(std::string fin[kKeys])> tags; // 要写的三个标签定下来之后(改它 = 模拟写错了标签)
};

struct CopyOptions {
    bool dryRun = false;                              // 只说会另存成什么,不新建文件
    Rule rule;
    std::function<void(double)> progress;             // 这个文件做到哪了(0..1)
    CopyHooks test;
};

struct CopyReport {
    CopyOutcome outcome = CopyOutcome::Failed;
    std::string code;                                 // 没存的原因(给程序读):跳过时是文件的类别(state)或 not_16bit;
                                                      // target_exists / in_use / open_failed / decode_error / incomplete_audio /
                                                      // no_encoder / encode_failed / verify_failed / io / cancelled
    msg::Text why;                                    // 给人看的一句(两半);存好了也有(说存成了什么)
    Scan scan;                                        // 源文件的扫描结果
    std::wstring target;                              // 新文件的路径(长路径形式;没走到那一步为空)
    int bits = 0;                                     // 新文件的位深
    uint64_t frames = 0;                              // 每声道的采样数
    std::string pcmMd5;                               // 源的全部采样(交错、左对齐 int32)的 MD5;新文件解出来的与它相同
    std::string fin[kKeys];                           // 新文件里的三个标签
};

// 目标文件名:<去掉扩展名的文件名>.flac / .24bit.flac,和源在同一个文件夹里
inline std::wstring copyTargetPath(const std::wstring& source, CopyKind kind) {
    const size_t slash = source.find_last_of(L"\\/");
    const size_t dot = source.find_last_of(L'.');
    const size_t nameAt = slash == std::wstring::npos ? 0 : slash + 1;
    const bool hasExt = dot != std::wstring::npos && dot > nameAt;       // ".name" 这种开头的点不算扩展名
    return (hasExt ? source.substr(0, dot) : source) + (kind == CopyKind::To24Bit ? L".24bit.flac" : L".flac");
}

CopyReport copyFile(const std::wstring& path, CopyKind kind, const CopyOptions& opt);

// FFmpeg 的容器元数据键 → Vorbis 注释的键(空串 = 这一条不带过去)。
// 三个识别标签另有安排(scan 定下的 fin),所以这里不带;键名里有 Vorbis 注释不许出现的字符(空格到 '}' 之外、等号)的也不带。
// FFmpeg 把几个常用的键改过名字(album_artist、track、disc),这里改回 Vorbis 注释里通行的写法;其余的转成大写
inline std::string commentKeyFor(const std::string& containerKey) {
    if (containerKey.empty()) return std::string();
    std::string k;
    for (char c : containerKey) {
        if (c < 0x20 || c > 0x7D || c == '=') return std::string();
        k.push_back(c >= 'a' && c <= 'z' ? (char)(c - 32) : c);
    }
    for (int i = 0; i < kKeys; ++i)
        if (k == keyName(i)) return std::string();
    if (k == "ALBUM_ARTIST") return "ALBUMARTIST";
    if (k == "TRACK") return "TRACKNUMBER";
    if (k == "DISC") return "DISCNUMBER";
    return k;
}

} // namespace tagrepair
