#pragma once
// =============================================================================
// 给人看的文字:全部在这一个文件里,每一句英文、中文各写一次
// =============================================================================
// 控制台上一律是「English text (中文文字)」:英文在前,中文在后、放在半角括号里(Text::both)。
// 给程序读的 JSON 里是同一句的两半:text / reason 用中文那一半,textEn / reasonEn 用英文那一半。
// 文件名、标签的值、数字是数据,不是文字:原样进两半({0}、{1}… 是放数据或另一句的位置)。
// 规矩:别的源文件里不写给人看的字面文字,要说什么先在这里加一条;每一条两半都要有
// (tests/unit_tests.cpp 逐条核对:两半都不空、中文那一半有汉字、英文那一半没有汉字、英文那一半用到的位置号中文那一半都有;
// 系统给的错误文字只放进中文那一半)。
// =============================================================================
#include <initializer_list>
#include <string>
#include <vector>

namespace msg {

struct Entry {
    const char* name;
    const wchar_t* en;
    const wchar_t* zh;
};

// 所有条目(按定义的顺序);核对用
inline std::vector<const Entry*>& all() {
    static std::vector<const Entry*> v;
    return v;
}

struct Registered : Entry {
    Registered(const char* n, const wchar_t* e, const wchar_t* z) : Entry{ n, e, z } { all().push_back(this); }
};

// 一句话的两半。从一个字符串构造 = 数据(两半相同)
struct Text {
    std::wstring en, zh;
    Text() {}
    Text(const std::wstring& same) : en(same), zh(same) {}
    Text(const wchar_t* same) : en(same), zh(same) {}
    Text(const std::wstring& e, const std::wstring& z) : en(e), zh(z) {}
    bool empty() const { return en.empty() && zh.empty(); }
    // 控制台上的写法:English (中文)。两半相同(纯数据)就只写一次
    std::wstring both() const {
        if (en == zh || zh.empty()) return en;
        if (en.empty()) return zh;
        return en + L" (" + zh + L")";
    }
};

inline Text operator+(const Text& a, const Text& b) { return Text(a.en + b.en, a.zh + b.zh); }

inline std::wstring fill(const wchar_t* pattern, const std::initializer_list<Text>& args, bool english) {
    std::wstring out;
    for (const wchar_t* p = pattern; *p; ++p) {
        if (p[0] == L'{' && p[1] >= L'0' && p[1] <= L'9' && p[2] == L'}') {
            const size_t i = (size_t)(p[1] - L'0');
            if (i < args.size()) out += english ? (args.begin() + i)->en : (args.begin() + i)->zh;
            p += 2;
        } else {
            out += *p;
        }
    }
    return out;
}

inline Text of(const Entry& e, const std::initializer_list<Text>& args = {}) { return Text(fill(e.en, args, true), fill(e.zh, args, false)); }

inline Text num(long long n) { return Text(std::to_wstring(n)); }

#define TAGREPAIR_MSG(id, en, zh) inline const Registered id(#id, en, zh);

// ---------------------------------------------------------------------------
// 开场、说明、用法
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(Title, L"tag repair for MQA-encoded FLAC files", L"MQA 标签修复")
TAGREPAIR_MSG(IntroWhat, L"Restores the lost identifying tags of MQA-encoded FLAC files that you own; the audio data is not changed.",
              L"给你自己拥有的、已经是 MQA 编码的 FLAC 文件补回丢失的识别标签;音频数据不动。")
TAGREPAIR_MSG(IntroCancel, L"Press Ctrl+C at any time to cancel: a file that is being written is finished or put back first, then the program stops.",
              L"随时可以按 Ctrl+C 取消:正在写的那个文件会先写完或者恢复原样,然后停下。")
TAGREPAIR_MSG(HowDrop1, L"How to use: drag music files, or a whole folder, onto the tagrepair.exe icon and drop them there.",
              L"用法:把音乐文件,或者整个文件夹,拖到 tagrepair.exe 的图标上再松手。")
TAGREPAIR_MSG(HowDrop2, L"It scans first, subfolders included, and lists what it found.", L"它先检查(文件夹连里面的子文件夹一起),把结果列出来。")
TAGREPAIR_MSG(HowDrop3, L"It changes or creates a file only after you answer Y to its question; without a Y, nothing is changed.",
              L"问过你、你回答 Y 之后才动手;不回答 Y,什么都不会改。")
TAGREPAIR_MSG(HowDrop4, L"Command-line usage: tagrepair --help", L"命令行的用法:tagrepair --help")
TAGREPAIR_MSG(PressEnter, L"Press Enter to close this window.", L"按回车键关闭窗口。")

TAGREPAIR_MSG(UseNoCommandLine, L"Without a command line: drag music files or a folder onto tagrepair.exe; it scans first and acts only after you answer Y",
              L"不用命令行:把音乐文件或文件夹拖到 tagrepair.exe 上,它先检查,问过你之后才动手")
TAGREPAIR_MSG(UseScan, L"Reports what each audio file is: not lossless PCM / not MQA / MQA with complete tags / MQA with missing or incomplete tags, "
                       L"can be repaired / MQA in a WAV file, can be saved as a FLAC copy / MQA but cannot be handled here",
              L"说出每个音频文件是哪一类:不是无损 PCM / 不是 MQA / MQA 标签完整 / MQA 标签缺失或不完整,可以修复 / "
              L"WAV 里的 MQA,可以另存为 FLAC / MQA 但这里处理不了")
TAGREPAIR_MSG(UseFix, L"Writes ENCODER / MQAENCODER / ORIGINALSAMPLERATE into the FLAC files that can be repaired; not one byte of audio data is changed",
              L"给能修的 FLAC 补上 ENCODER / MQAENCODER / ORIGINALSAMPLERATE;音频数据一个字节都不动")
TAGREPAIR_MSG(UseDryRun, L"--dry-run: only say what would be done, write nothing; --keep-backup: keep the original as <name>.bak",
              L"--dry-run:只说会怎么做,不写;--keep-backup:原文件留一份,名字是 <文件名>.bak")
TAGREPAIR_MSG(UseWavToFlac, L"--wav-to-flac: for MQA audio in a WAV file, save a new FLAC file <name>.flac next to it, with the tags; the WAV file is not changed",
              L"--wav-to-flac:WAV 里的 MQA,在旁边另存一个带标签的新文件 <文件名>.flac;WAV 文件不改")
TAGREPAIR_MSG(UseCopy24, L"--copy-24bit: for 16-bit MQA files, WAV or FLAC, save a 24-bit FLAC copy <name>.24bit.flac next to them: "
                         L"the same sample values with eight zero bits added below; the original is not changed",
              L"--copy-24bit:16 bit 的 MQA 文件,WAV 和 FLAC 都行,在旁边另存一个 24 bit 的 FLAC,名字是 <文件名>.24bit.flac:"
              L"采样值不变,低位补 8 个零;原文件不改")
TAGREPAIR_MSG(UseCopyOnly, L"--copy-only: only save the copies chosen above; do not repair any file in place", L"--copy-only:只做上面选的另存,不原地修任何文件")
TAGREPAIR_MSG(UseCopyRules, L"A copy never replaces an existing file, and it is kept only after every sample has been compared with the original",
              L"另存从不覆盖已有的文件;新文件要逐个样本和原文件核对无误才留下")
TAGREPAIR_MSG(UseEncoderTag, L"--encoder-tag \"<value>\": the value written to ENCODER / MQAENCODER, default \"{0}\"; it must contain \"{1}\" and \"{2}\"",
              L"--encoder-tag \"<值>\":写进 ENCODER / MQAENCODER 的值,默认 \"{0}\";值里必须带着 \"{1}\" 和 \"{2}\"")
TAGREPAIR_MSG(UseAcceptPrefix, L"--accept-prefix \"<text>\": an existing value that starts with this text is acceptable and is kept, default \"{0}\"; "
                               L"an empty text accepts any value that is not empty; scan takes these two options as well",
              L"--accept-prefix \"<文字>\":已有的值以它开头就算合格、留着不动,默认 \"{0}\";给空串就是只要不是空值都合格;scan 也认这两个选项")
TAGREPAIR_MSG(UseJson, L"--json: one JSON object per line, for another program; --recursive: include subfolders",
              L"--json:每行一个 JSON,给别的程序读;--recursive:连子文件夹一起")
TAGREPAIR_MSG(UseSelftest, L"Self-test: creates its own files and checks every rule above on them", L"自检:自己造文件,把上面的每一条都验一遍")
TAGREPAIR_MSG(UseInfo, L"Version, protocol number and commands, for a program that calls this one", L"版本、协议号、认哪些命令,给调用它的程序看的")
TAGREPAIR_MSG(UseFfmpegDir, L"scan / fix / selftest accept --ffmpeg-dir <folder>: the folder that holds the four FFmpeg run-time libraries ({0} and the others).",
              L"scan / fix / selftest 都可以加 --ffmpeg-dir <文件夹>:FFmpeg 的四个运行库({0} 那几个)所在的文件夹。")
TAGREPAIR_MSG(UseFfmpegOrder,
              L"Without it, the program looks next to itself, then two folders up, then in the folder named by the environment variable TAGREPAIR_FFMPEG_DIR.",
              L"不加的话依次找:本程序旁边、往上两级的文件夹、环境变量 TAGREPAIR_FFMPEG_DIR 指的文件夹。")
TAGREPAIR_MSG(UseExitCodes, L"Exit codes: 0 done; 1 a file was refused or failed; 2 wrong usage; 3 FFmpeg run-time libraries not found; 4 cancelled",
              L"退出码:0 正常;1 有文件被拒绝或失败;2 用法不对;3 找不到 FFmpeg 运行库;4 被取消")
TAGREPAIR_MSG(InfoLine, L"{0} {1}: id {2}, protocol {3}; commands: info, scan, fix, selftest", L"{0} {1}:标识 {2},协议 {3};命令:info、scan、fix、selftest")

// ---------------------------------------------------------------------------
// 程序自己干不了活
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(NoFfmpeg,
              L"FFmpeg run-time libraries not found: the four files {0} must sit together in one folder (looked in: {1}). Put them next to this program, "
              L"or point to them with --ffmpeg-dir <folder> or the environment variable TAGREPAIR_FFMPEG_DIR",
              L"找不到 FFmpeg 运行库:要 {0} 这四个文件放在同一个文件夹里(找过:{1})。把它们放到本程序旁边,或者用 --ffmpeg-dir <文件夹>、环境变量 "
              L"TAGREPAIR_FFMPEG_DIR 指给它")
TAGREPAIR_MSG(OptionBad, L"{0}: {1}", L"{0}:{1}")
TAGREPAIR_MSG(OptionNotText, L"not valid text (a lone UTF-16 surrogate)", L"不是合法的文字(UTF-16 里有落单的代理项)")
TAGREPAIR_MSG(OptionCopyOnlyAlone, L"--copy-only needs --wav-to-flac or --copy-24bit", L"--copy-only 要和 --wav-to-flac 或 --copy-24bit 一起用")
TAGREPAIR_MSG(OptionBackupNoCopy, L"--keep-backup applies to in-place repair only; a copy never changes the original, so there is nothing to back up",
              L"--keep-backup 只对原地修复有意义;另存从不改原文件,没有东西要备份")
TAGREPAIR_MSG(TagEmpty, L"the value is empty", L"值是空的")
TAGREPAIR_MSG(TagTooLong, L"the value is too long (200 bytes at most)", L"值太长(最多 200 字节)")
TAGREPAIR_MSG(TagNotUtf8, L"the value is not valid UTF-8", L"值不是合法的 UTF-8")
TAGREPAIR_MSG(TagControlChar, L"the value contains control characters (line breaks, tabs and the like)", L"值里有控制字符(换行、制表符之类)")
TAGREPAIR_MSG(TagEdgeSpace, L"the value must not begin or end with a space", L"值的头尾不能是空格")
TAGREPAIR_MSG(TagSemicolon, L"the value must not contain a semicolon", L"值里不能有分号")
TAGREPAIR_MSG(TagNeedsMarks,
              L"the value must contain \"tag repair\" and \"(unsigned)\" (they say that a tag repair tool wrote it, unsigned, and that it does not pose as an encoder)",
              L"值里必须带着 \"tag repair\" 和 \"(unsigned)\"(说明是标签修复工具写的、没有签名,不冒充编码器)")
TAGREPAIR_MSG(PrefixTooLong, L"the prefix is too long (200 bytes at most)", L"前缀太长(最多 200 字节)")
TAGREPAIR_MSG(PrefixNotUtf8, L"the prefix is not valid UTF-8", L"前缀不是合法的 UTF-8")
TAGREPAIR_MSG(PrefixControlChar, L"the prefix contains control characters", L"前缀里有控制字符")

// ---------------------------------------------------------------------------
// 一个文件是哪一类(scan 的那一句;JSON 的 text / textEn)
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(StError, L"can't be opened: {0}", L"打不开:{0}")
TAGREPAIR_MSG(StNotLossless, L"not lossless PCM ({0})", L"不是无损 PCM({0})")
TAGREPAIR_MSG(StNoMarker, L"not MQA", L"不是 MQA")
TAGREPAIR_MSG(StComplete, L"MQA, tags complete", L"MQA,标签完整")
TAGREPAIR_MSG(StRepairMissing, L"MQA, tags missing; can be repaired", L"MQA,标签缺失,可以修复")
TAGREPAIR_MSG(StRepairIncomplete, L"MQA, tags incomplete ({0}); can be repaired", L"MQA,标签不完整({0}),可以修复")
TAGREPAIR_MSG(StRateUnknown,
              L"MQA, tags incomplete; the stream does not state the original sample rate, so ORIGINALSAMPLERATE can't be written; not repaired",
              L"MQA,标签不完整;码流里没读到原始采样率,ORIGINALSAMPLERATE 写不出来,不修")
TAGREPAIR_MSG(StUnsupported, L"MQA, but the {0} container can't be repaired here (FLAC only)", L"MQA,但 {0} 容器这里修不了(只修 FLAC)")
TAGREPAIR_MSG(StDamaged, L"MQA, but the file structure is damaged ({0}); not repaired", L"MQA,但文件结构有问题({0}),不修")
TAGREPAIR_MSG(StConvertible, L"MQA in a WAV file; can be saved as a FLAC copy with the tags, the WAV file is not changed",
              L"WAV 里的 MQA;可以在旁边另存一个带标签的 FLAC,WAV 文件不改")
TAGREPAIR_MSG(StWavNoRate, L"MQA in a WAV file, but the stream does not state the original sample rate, so no FLAC copy with complete tags can be saved",
              L"WAV 里的 MQA,但码流里没读到原始采样率,存不出标签完整的 FLAC")
TAGREPAIR_MSG(StWavFormat, L"MQA in a WAV file, but only 16-bit and 24-bit PCM can be saved as a FLAC copy ({0})",
              L"WAV 里的 MQA,但只有 16 bit、24 bit 的 PCM 能另存为 FLAC({0})")
TAGREPAIR_MSG(SysErrorCode, L"Windows error {0}", L"系统错误 {0}")
TAGREPAIR_MSG(WhyInUse, L"the file is in use by another program", L"文件正被别的程序占用")
TAGREPAIR_MSG(WhyUnreadable, L"it can't be read", L"读不了")
TAGREPAIR_MSG(ProbeNoSize, L"the file size can't be read", L"读不到文件大小")
TAGREPAIR_MSG(ProbeNoMemory, L"not enough memory", L"内存不够")
TAGREPAIR_MSG(ProbeNotAudio, L"not an audio file that FFmpeg recognises", L"不是 FFmpeg 认得的音频文件")
TAGREPAIR_MSG(ProbeNoStreamInfo, L"the stream information can't be read", L"读不出流信息")
TAGREPAIR_MSG(ProbeNoAudioTrack, L"no audio track", L"没有音轨")
TAGREPAIR_MSG(ProbeNoDecoder, L"the decoder can't be opened", L"解码器打不开")
TAGREPAIR_MSG(ProbeNoMd5, L"the MD5 of the system can't be used", L"系统的 MD5 用不了")
TAGREPAIR_MSG(ProbeCancelled, L"cancelled", L"已取消")
// FLAC 元数据区走不通的原因
TAGREPAIR_MSG(LayIo, L"the file can't be read", L"读不了文件")
TAGREPAIR_MSG(LayId3TooLong, L"the ID3v2 length exceeds the file", L"ID3v2 的长度超出文件")
TAGREPAIR_MSG(LayNotFlac, L"not a FLAC file", L"不是 FLAC 文件")
TAGREPAIR_MSG(LayHeaderCut, L"a metadata block header is incomplete", L"元数据块的头不全")
TAGREPAIR_MSG(LayBadType, L"invalid metadata block type", L"元数据块类型不合法")
TAGREPAIR_MSG(LayFirstNotStreamInfo, L"the first metadata block is not STREAMINFO", L"第一个元数据块不是 STREAMINFO")
TAGREPAIR_MSG(LayTwoStreamInfo, L"two STREAMINFO blocks", L"有两个 STREAMINFO")
TAGREPAIR_MSG(LayBlockBeyondFile, L"a metadata block exceeds the file", L"元数据块超出文件")
TAGREPAIR_MSG(LayTooManyBlocks, L"too many metadata blocks", L"元数据块太多")
TAGREPAIR_MSG(LayMetaTooBig, L"the metadata area is too large", L"元数据区太大")
TAGREPAIR_MSG(LayRateZero, L"the sample rate in STREAMINFO is 0", L"STREAMINFO 的采样率是 0")
TAGREPAIR_MSG(LayNoFrameAfterMeta, L"no audio frame follows the metadata", L"元数据之后不是音频帧")
TAGREPAIR_MSG(LayNoAudio, L"no audio frames", L"没有音频帧")
TAGREPAIR_MSG(LayMetaUnreadable, L"the metadata area can't be read", L"读不了元数据区")
TAGREPAIR_MSG(LayBadComment, L"the comment block is damaged", L"注释块损坏")

// ---------------------------------------------------------------------------
// 原地修复:没修的原因(JSON 的 reason / reasonEn)
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(FixInUse, L"the file is in use by another program; the file is as it was", L"文件正被别的程序占用,没有改动")
TAGREPAIR_MSG(FixComplete, L"tags are complete; nothing changed", L"标签完整,没有改动")
TAGREPAIR_MSG(FixReadOnly, L"the file is read-only; the file is as it was", L"文件是只读的,没有改动")
TAGREPAIR_MSG(FixAccessDenied, L"no permission to write this file; the file is as it was", L"没有写这个文件的权限,没有改动")
TAGREPAIR_MSG(FixOpenFailed, L"the file can't be opened; the file is as it was", L"打不开:{0}")
TAGREPAIR_MSG(FixReadFailed, L"the disk could not be read or written; the file is as it was", L"读不了文件,没有改动")
TAGREPAIR_MSG(FixTooBig, L"the tags would exceed the FLAC block size limit; the file is as it was", L"标签写进去会超过 FLAC 元数据块 16 MB 的上限,没有改动")
TAGREPAIR_MSG(FixMetaDamaged, L"the metadata can't be parsed; the file is as it was", L"元数据区解析不了,没有改动")
TAGREPAIR_MSG(FixInternalLength, L"internal error; the file is as it was", L"内部错误(元数据长度对不上),没有改动")
TAGREPAIR_MSG(FixInternalCheck, L"internal error; the file is as it was", L"内部错误(算出的元数据自己核对不过),没有改动")
TAGREPAIR_MSG(FixDecodeFailed, L"the audio has bad frames; the file is as it was", L"解码不了({0}),没有改动")
TAGREPAIR_MSG(FixAudioBadFrames, L"the audio has bad frames; the file is as it was",
              L"音频不完整或有坏帧(解出 {0} 个采样,文件头写的是 {1};解码报错 {2} 处),没有改动")
TAGREPAIR_MSG(FixAudioShort, L"the audio is incomplete; the file is as it was", L"音频不完整或有坏帧(解出 {0} 个采样,文件头写的是 {1};解码报错 {2} 处),没有改动")
TAGREPAIR_MSG(FixDecodeErrors, L"the audio has bad frames; the file is as it was", L"音频解码报错 {0} 处(文件可能损坏),没有改动")
TAGREPAIR_MSG(FixWouldFix, L"can be repaired (dry run, nothing changed): {0}", L"可以修复(试运行,没有改动):{0}")
TAGREPAIR_MSG(FixWriteFailed, L"the disk could not be read or written", L"写入失败({0})")
TAGREPAIR_MSG(FixTailRestored, L"; the file is as it was", L";已把原来的字节写回去,文件保持原样")
TAGREPAIR_MSG(FixTailNotRestored, L"; the original bytes could not be written back, the tag area of this file may be damaged (audio data was not touched)",
              L";原来的字节也没能写回去,这个文件的标签区可能已经损坏(音频数据没有动过)")
TAGREPAIR_MSG(FixTailTempGone, L"; the file is as it was", L";临时文件已删,原文件没有动")
TAGREPAIR_MSG(FixTempCreate, L"the disk could not be read or written; the file is as it was", L"旁边建不了临时文件({0}),没有改动")
TAGREPAIR_MSG(FixTempWrite, L"the disk could not be read or written; the file is as it was", L"写临时文件失败({0});临时文件已删,原文件没有动")
TAGREPAIR_MSG(FixGuardOpen, L"the disk could not be read or written; the file is as it was", L"换入之前打不开原文件({0}),没有改动")
TAGREPAIR_MSG(FixChanged, L"another program changed the file meanwhile; the file is as it was", L"文件在换入之前被别的程序改了,没有改动")
TAGREPAIR_MSG(FixSwapRestored, L"the new file could not be swapped in; the file is as it was",
              L"换入新文件没成功({0});原文件已经放回原来的名字,内容没有动;临时文件已删")
TAGREPAIR_MSG(FixSwapAtBackup, L"the swap-in stopped half-way; the file is now at {1}",
              L"换入新文件没成功({0});原文件的内容没有动,但它现在的名字是 {1},请把它改回 {2}")
TAGREPAIR_MSG(FixSwapLost, L"the swap-in stopped half-way; no file is left under the original name and the new file can't be found",
              L"换入新文件没成功({0});原来的名字下已经没有文件,写好的新文件也找不到了")
TAGREPAIR_MSG(FixSwapAtTemp, L"the swap-in stopped half-way; the file is now at {1}",
              L"换入新文件没成功({0});原来的名字下已经没有文件,修好并核对过的文件留在 {1},请把它改名为 {2}")
TAGREPAIR_MSG(FixSwapFailed, L"the new file could not be swapped in; the file is as it was", L"换入新文件没成功({0});临时文件已删,原文件没有动")
TAGREPAIR_MSG(FixAfterSwapDiffers, L"the read-back check failed: the swapped-in file differs from what was written; please check this file",
              L"换入之后的文件与写好的不一致,请核对这个文件")
TAGREPAIR_MSG(FixCancelled, L"cancelled; the file is as it was", L"已取消,这个文件没有改动")
TAGREPAIR_MSG(FixCancelledTemp, L"cancelled; the file is as it was", L"已取消;临时文件已删,原文件没有动")
// 写完读回对不上的那一条
TAGREPAIR_MSG(VerLayout, L"the read-back check failed: the metadata can't be parsed ({0})", L"写完之后元数据区走不通({0})")
TAGREPAIR_MSG(VerAudioStart, L"the read-back check failed: the audio starts at the wrong offset", L"写完之后音频起点不对")
TAGREPAIR_MSG(VerAudioLength, L"the read-back check failed: the audio length changed", L"写完之后音频长度变了")
TAGREPAIR_MSG(VerStreamInfo, L"the read-back check failed: STREAMINFO changed", L"写完之后 STREAMINFO 变了")
TAGREPAIR_MSG(VerPrefix, L"the read-back check failed: the bytes before fLaC changed", L"写完之后文件开头(fLaC 之前)变了")
TAGREPAIR_MSG(VerMeta, L"the read-back check failed: the metadata differs from what was to be written", L"写完之后元数据区与要写的不一致")
TAGREPAIR_MSG(VerTags, L"the read-back check failed: the tags read back are wrong", L"读回的标签不对")
TAGREPAIR_MSG(VerTag, L"the read-back check failed: {0} read back is wrong", L"读回的 {0} 不对")
TAGREPAIR_MSG(VerSha, L"the read-back check failed: the SHA-256 of the audio data differs from before", L"音频数据的 SHA-256 与修之前不同")
TAGREPAIR_MSG(VerNoMarker, L"the read-back check failed: FFmpeg no longer finds the marker", L"FFmpeg 读回:认不出 MQA 了")
TAGREPAIR_MSG(VerFfTag, L"the read-back check failed: {0} as read by FFmpeg is wrong", L"FFmpeg 读回的 {0} 不对")
TAGREPAIR_MSG(VerPcm, L"the read-back check failed: the samples decoded by FFmpeg differ from before", L"FFmpeg 解出的样本与修之前不同")

// ---------------------------------------------------------------------------
// 另存(WAV → FLAC、16 bit → 24 bit):结果与原因(JSON 的 reason / reasonEn)
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(CopyCreated, L"saved {0}: {1} bit, {2} samples per channel, every sample identical to the original",
              L"已另存 {0}:{1} bit,每声道 {2} 个采样,逐个样本与原文件相同")
TAGREPAIR_MSG(CopyCreated24, L"saved {0}: 24 bit, the {1} 16-bit samples per channel of the original, each with eight zero bits added below",
              L"已另存 {0}:24 bit,原文件每声道 {1} 个 16 bit 采样,每个低位补了 8 个零")
TAGREPAIR_MSG(CopyWould, L"would save {0} (dry run, nothing written)", L"会另存 {0}(试运行,什么都没有写)")
TAGREPAIR_MSG(CopySkipNotWav, L"no FLAC copy: {0}", L"不另存为 FLAC:{0}")
TAGREPAIR_MSG(CopySkipNot16, L"no 24-bit copy: this is not a 16-bit MQA file that can be copied ({0})", L"不另存 24 bit:这不是能另存的 16 bit MQA 文件({0})")
TAGREPAIR_MSG(CopyTargetExists, L"{0} already exists; an existing file is never overwritten, nothing was written", L"{0} 已经存在;已有的文件从不覆盖,什么都没有写")
TAGREPAIR_MSG(CopySourceInUse, L"the original is being written by another program; nothing was written", L"原文件正被别的程序写着,没有另存")
TAGREPAIR_MSG(CopySourceOpen, L"the original can't be opened; nothing was written", L"原文件打不开:{0};没有另存")
TAGREPAIR_MSG(CopyNoEncoder, L"this FFmpeg has no usable FLAC encoder; nothing was written", L"这份 FFmpeg 里没有能用的 FLAC 编码器;没有另存")
TAGREPAIR_MSG(CopyDecode, L"the original can't be decoded completely; nothing was kept", L"原文件解码不完整({0});没有留下新文件")
TAGREPAIR_MSG(CopyEncode, L"encoding failed; nothing was kept", L"编码失败;没有留下新文件")
TAGREPAIR_MSG(CopyTempCreate, L"the temporary file can't be created next to the original; nothing was written", L"旁边建不了临时文件({0}),没有另存")
TAGREPAIR_MSG(CopyTempWrite, L"writing the temporary file failed; it was deleted, nothing was kept", L"写临时文件失败({0});临时文件已删,没有留下新文件")
TAGREPAIR_MSG(CopyVerifyFailed, L"the new file failed the check against the original ({0}); it was deleted, nothing was kept",
              L"新文件和原文件核对没通过({0});已删掉,没有留下新文件")
TAGREPAIR_MSG(CopyRename, L"the new file could not be given its final name; it was deleted, nothing was kept", L"新文件改不成正式的名字({0});已删掉,没有留下新文件")
TAGREPAIR_MSG(CopyCancelled, L"cancelled; no new file was kept", L"已取消;没有留下新文件")
TAGREPAIR_MSG(CvLayout, L"its structure can't be parsed", L"结构解析不了")
TAGREPAIR_MSG(CvFormat, L"sample rate, channel count or bit depth is wrong", L"采样率、声道数或位深不对")
TAGREPAIR_MSG(CvCount, L"the number of samples differs", L"采样个数不同")
TAGREPAIR_MSG(CvHeaderMd5, L"the MD5 in its header does not match the samples of the original", L"文件头里的 MD5 和原文件的采样对不上")
TAGREPAIR_MSG(CvScan, L"it does not scan as MQA with complete tags", L"扫描出来不是 MQA 标签完整")
TAGREPAIR_MSG(CvTags, L"the tags read back are wrong", L"读回的标签不对")
TAGREPAIR_MSG(CvDecode, L"it can't be decoded without errors", L"解码有错")
TAGREPAIR_MSG(CvSamples, L"the decoded samples differ from the original", L"解出的采样和原文件不同")
TAGREPAIR_MSG(CvLowBits, L"the low 8 bits are not all zero, or the upper 16 bits differ from the original", L"低 8 位不全是零,或者高 16 位和原文件不同")

// ---------------------------------------------------------------------------
// scan / fix 给人看的输出
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(OutCopies, L"{0} copies", L"共 {0} 份")
TAGREPAIR_MSG(OutCarrier, L"carrier {0} / {1} bit; original sample rate in the stream: {2}", L"载波 {0} / {1} bit;码流里的原始采样率:{2}")
TAGREPAIR_MSG(OutActions, L"possible here: {0}", L"这里可以做:{0}")
TAGREPAIR_MSG(ActRepair, L"repair the tags in place", L"原地修复标签")
TAGREPAIR_MSG(ActWavToFlac, L"save a FLAC copy with --wav-to-flac", L"用 --wav-to-flac 另存为 FLAC")
TAGREPAIR_MSG(ActCopy24, L"save a 24-bit copy with --copy-24bit", L"用 --copy-24bit 另存 24 bit 的拷贝")
TAGREPAIR_MSG(OutFixed, L"repaired, {0}", L"已修复,{0}")
TAGREPAIR_MSG(ModeInPlace, L"written in place", L"原地改写")
TAGREPAIR_MSG(ModeRewrite, L"whole file rewritten, then swapped in", L"整文件重写后换入")
TAGREPAIR_MSG(OutAudioSame, L"audio data untouched: {0} bytes, SHA-256 {1}…; MD5 of the decoded samples {2}, the same before and after",
              L"音频数据未动:{0} 字节,SHA-256 {1}…;解码样本 MD5 {2},修之前、修之后相同")
TAGREPAIR_MSG(OutBackupAt, L"the original is kept at {0}", L"原文件留在 {0}")
TAGREPAIR_MSG(OutStaleRemoved, L"removed {0} temporary file(s) left behind by an interrupted earlier run", L"顺手删掉了上次被打断留下的临时文件 {0} 个")
TAGREPAIR_MSG(OutWouldMode, L"{0}; method: {1}", L"{0};方式:{1}")
TAGREPAIR_MSG(SumScan,
              L"{0} file(s): MQA with complete tags {1}, can be repaired {2}, WAV that can be saved as FLAC {3}, MQA but cannot be handled here {4}, "
              L"not MQA {5}, not lossless PCM {6}, cannot be opened {7}",
              L"共 {0} 个文件:MQA 标签完整 {1},可以修复 {2},可以另存为 FLAC 的 WAV {3},MQA 但这里处理不了 {4},不是 MQA {5},不是无损 PCM {6},打不开 {7}")
TAGREPAIR_MSG(SumFix, L"{0} file(s): repaired {1}, complete and untouched {2}, skipped {3}, refused {4}, failed {5}",
              L"共 {0} 个文件:已修复 {1},标签完整没动 {2},不属于能修的 {3},拒绝 {4},失败 {5}")
TAGREPAIR_MSG(SumFixDry, L"{0} file(s): can be repaired {1}, complete and untouched {2}, skipped {3}, refused {4}, failed {5}",
              L"共 {0} 个文件:可以修复 {1},标签完整没动 {2},不属于能修的 {3},拒绝 {4},失败 {5}")
TAGREPAIR_MSG(SumCopy, L"{0}: saved {1}, not applicable {2}, refused {3}, failed {4}", L"{0}:新存 {1},不适用 {2},拒绝 {3},失败 {4}")
TAGREPAIR_MSG(SumCopyDry, L"{0}: would save {1}, not applicable {2}, refused {3}, failed {4}", L"{0}:会新存 {1},不适用 {2},拒绝 {3},失败 {4}")
TAGREPAIR_MSG(KindWavToFlac, L"WAV to FLAC", L"WAV 另存为 FLAC")
TAGREPAIR_MSG(KindCopy24, L"24-bit copies", L"另存 24 bit")

// ---------------------------------------------------------------------------
// 进度、取消
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(PgScan, L"Scanning", L"检查")
TAGREPAIR_MSG(PgRepair, L"Repairing", L"修复")
TAGREPAIR_MSG(PgSave, L"Saving", L"另存")
TAGREPAIR_MSG(CancelScan, L"Cancelled. Scanned {0} of {1}; nothing was changed.", L"已取消。{1} 个里检查了 {0} 个;什么都没有改。")
TAGREPAIR_MSG(CancelRepair, L"Cancelled. Repaired {0} of {1}", L"已取消。{1} 个里修好了 {0} 个")
TAGREPAIR_MSG(CancelSave, L"Cancelled. Saved {0} of {1}", L"已取消。{1} 个里存好了 {0} 个")
TAGREPAIR_MSG(CancelPartSkipped, L"; {0} skipped (nothing to do for them)", L",{0} 个不用处理")
TAGREPAIR_MSG(CancelPartNotDone, L"; {0} could not be done, see above", L",{0} 个没有做成,原因见上面")
TAGREPAIR_MSG(CancelPartStopped, L"; {0} stopped part-way and left as it was", L",{0} 个做到一半停下、保持原样")
TAGREPAIR_MSG(CancelPartNotStarted, L"; {0} not started.", L",{0} 个没有开始。")
TAGREPAIR_MSG(CancelAtPrompt, L"Cancelled. Nothing was changed.", L"已取消。什么都没有改。")
// 前面的问题已经做过事(修过、存过)之后:不能再说"什么都没有改",说"别的什么都没有改"
TAGREPAIR_MSG(CancelAtPromptAfter, L"Cancelled. Nothing else was changed.", L"已取消。别的什么都没有改。")

// ---------------------------------------------------------------------------
// 一问一答(双击 / 拖放)
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(AskScanning, L"Scanning {0} file(s)…", L"正在检查 {0} 个文件……")
TAGREPAIR_MSG(AskScanned, L"Scanned {0} file(s):", L"检查了 {0} 个文件:")
TAGREPAIR_MSG(AskComplete, L"MQA, tags complete: {0}", L"MQA,标签完整:{0} 个")
TAGREPAIR_MSG(AskRepairable, L"MQA, tags missing or incomplete; can be repaired in place: {0}", L"MQA,标签缺失或不完整,可以原地修复:{0} 个")
TAGREPAIR_MSG(AskWav, L"WAV: can be saved as a new FLAC file next to the original, the WAV is not changed: {0}",
              L"WAV:可以在原文件旁边另存一个新的 FLAC 文件,WAV 不改:{0} 个")
TAGREPAIR_MSG(Ask16, L"16-bit: tags can be repaired; some devices recognise only 24-bit files — a 24-bit copy can be saved next to the original: {0}",
              L"16 bit:标签可以修;有的设备只认 24 bit 的文件,可以在原文件旁边另存一个 24 bit 的拷贝:{0} 个")
TAGREPAIR_MSG(AskAlreadyThere, L"already there, will not be written again: {0}", L"已经有了,不会再写:{0}")
TAGREPAIR_MSG(AskNoMarker, L"Not MQA: {0}", L"不是 MQA:{0} 个")
TAGREPAIR_MSG(AskCannot, L"Cannot be handled: {0}", L"处理不了:{0} 个")
TAGREPAIR_MSG(AskNothingToDo, L"Nothing to do. Nothing was changed.", L"没有需要处理的文件,什么都没有改。")
TAGREPAIR_MSG(AskRepairExplain,
              L"Repair writes three tags, ENCODER / MQAENCODER / ORIGINALSAMPLERATE, into the tag area of each file; the audio data is not changed. "
              L"Everything is checked after writing, and the file is put back as it was if anything differs.",
              L"修复只往文件的标签区写三个标签:ENCODER / MQAENCODER / ORIGINALSAMPLERATE,音频数据不动;写完会核对,有一处对不上就恢复原样。")
TAGREPAIR_MSG(AskRepair, L"Repair these {0} file(s) in place? Y + Enter = start, anything else = leave them", L"原地修复这 {0} 个文件吗?输入 Y 再按回车就开始,别的回答都算不修")
TAGREPAIR_MSG(AskWavExplain,
              L"A new FLAC file is written next to each WAV: the same sample values, losslessly encoded, with the three tags. "
              L"The WAV file is not changed, and an existing file is never overwritten.",
              L"在每个 WAV 旁边新写一个 FLAC 文件:采样值一模一样、无损编码,带上三个标签。WAV 文件不改,已有的文件也从不覆盖。")
TAGREPAIR_MSG(AskWavQuestion, L"Save {0} WAV file(s) as new FLAC files? Y + Enter = start, anything else = leave them",
              L"把这 {0} 个 WAV 另存为新的 FLAC 文件吗?输入 Y 再按回车就开始,别的回答都算不存")
TAGREPAIR_MSG(Ask24Explain,
              L"A 24-bit FLAC copy is written next to each 16-bit file: every sample keeps its value and gets eight zero bits below it; nothing is recomputed. "
              L"The original is not changed, and an existing file is never overwritten.",
              L"在每个 16 bit 的文件旁边新写一个 24 bit 的 FLAC:每个采样值不变,只在低位补 8 个零,不做任何重新计算。原文件不改,已有的文件也从不覆盖。")
TAGREPAIR_MSG(Ask24Question, L"Save 24-bit copies of these {0} file(s)? Y + Enter = start, anything else = leave them",
              L"给这 {0} 个文件另存 24 bit 的拷贝吗?输入 Y 再按回车就开始,别的回答都算不存")
TAGREPAIR_MSG(StartRepair, L"Starting: repairing {0} file(s)…", L"开始修复 {0} 个文件……")
TAGREPAIR_MSG(StartWav, L"Starting: saving {0} WAV file(s) as FLAC…", L"开始把 {0} 个 WAV 另存为 FLAC……")
TAGREPAIR_MSG(Start24, L"Starting: saving 24-bit copies of {0} file(s)…", L"开始给 {0} 个文件另存 24 bit 的拷贝……")
TAGREPAIR_MSG(SkipRepair, L"Not repaired. Nothing was changed.", L"没有修复,什么都没有改。")
TAGREPAIR_MSG(SkipWav, L"No FLAC file was saved. Nothing was changed.", L"没有另存 FLAC,什么都没有改。")
TAGREPAIR_MSG(Skip24, L"No 24-bit copy was saved. Nothing was changed.", L"没有另存 24 bit 的拷贝,什么都没有改。")
TAGREPAIR_MSG(SkipWavAfter, L"No FLAC file was saved. Nothing else was changed.", L"没有另存 FLAC,别的什么都没有改。")
TAGREPAIR_MSG(Skip24After, L"No 24-bit copy was saved. Nothing else was changed.", L"没有另存 24 bit 的拷贝,别的什么都没有改。")
TAGREPAIR_MSG(DoneRepair, L"Repaired {0}; already complete and untouched {1}; not repaired {2}", L"已修复 {0} 个,已经完整没动 {1} 个,没修成 {2} 个")
TAGREPAIR_MSG(DoneSave, L"Saved {0} new file(s); not saved {1}", L"新存了 {0} 个文件,没存成 {1} 个")
TAGREPAIR_MSG(DoneSeeAbove, L"; the reason is on each file's line above", L";原因见上面各自的那一行")

// ---------------------------------------------------------------------------
// 自检(tagrepair selftest)
// ---------------------------------------------------------------------------
TAGREPAIR_MSG(TestPass, L"PASS", L"通过")
TAGREPAIR_MSG(TestFail, L"FAIL", L"失败")
TAGREPAIR_MSG(TestFolder, L"Self-test folder: {0}", L"自检目录:{0}")
TAGREPAIR_MSG(TestCannotWrite, L"self-test: can't write {0}", L"自检:写不了 {0}")
TAGREPAIR_MSG(TestSummary, L"passed {0}, failed {1}", L"通过 {0},失败 {1}")
TAGREPAIR_MSG(TestNoCompression, L"this disk does not take the compression attribute; only \"can be repaired\" was checked",
              L"这个盘设不了压缩属性,这一条只验了能修好")
TAGREPAIR_MSG(TestWhyParse, L"the repaired file can't be parsed", L"修完的文件解析不了")
TAGREPAIR_MSG(TestWhyAudio, L"the audio bytes changed", L"音频字节变了")
TAGREPAIR_MSG(TestWhyStreamInfo, L"STREAMINFO changed", L"STREAMINFO 变了")
TAGREPAIR_MSG(TestWhyPrefix, L"the bytes before fLaC changed", L"fLaC 之前的字节变了")
TAGREPAIR_MSG(TestWhyKeys, L"the three keys are not once each, or a value is unacceptable", L"三个键不是各一份,或者值不合格")
TAGREPAIR_MSG(TestWhyValue, L"wrong value: {0} = {1}", L"值不对:{0} = {1}")
TAGREPAIR_MSG(TestWhyRescan, L"scanning again does not say complete: {0}", L"修完再扫描不是「完整」:{0}")
TAGREPAIR_MSG(TestWhyFfmpeg, L"{0} as read by FFmpeg is wrong", L"FFmpeg 读回的 {0} 不对")
TAGREPAIR_MSG(T1_1, L"scan: MQA, original rate 352.8 kHz, tags incomplete, can be repaired", L"扫描:MQA、原始采样率 352.8 kHz、标签不完整、可以修复")
TAGREPAIR_MSG(T1_2, L"repair: three tags written; audio frames to the end of the file byte-identical; audio offset and file size unchanged",
              L"修复:三个标签写进去,音频帧到文件末尾逐字节相同,音频起点与文件大小不变")
TAGREPAIR_MSG(T1_3, L"other comments and their order, vendor string, seek table and picture untouched; encoder=Lavf… replaced where it was, no extra copy",
              L"别的注释与顺序、厂商串、定位表、图片都没动;encoder=Lavf… 在原位换掉、没有多一份")
TAGREPAIR_MSG(T1_4, L"repair again: nothing happens, the file is byte-identical", L"再修一次:不动,文件逐字节相同")
TAGREPAIR_MSG(T2_1, L"no PADDING: whole file rewritten and swapped in, audio byte-identical (its offset moved)",
              L"没有 PADDING:整文件重写后换入,音频逐字节相同(起点往后挪了)")
TAGREPAIR_MSG(T2_2, L"creation time kept; no temporary file left", L"建立时间保留;旁边不留临时文件")
TAGREPAIR_MSG(T2_3, L"repair again: nothing happens, the file is byte-identical", L"再修一次:不动,文件逐字节相同")
TAGREPAIR_MSG(T3, L"no comment block: one is created before PADDING; application block and picture kept as they are",
              L"没有注释块:新建一个,放在 PADDING 之前;应用块、图片原样留着")
TAGREPAIR_MSG(T4, L"two comment blocks: the three keys once each in the first; old values in the second removed, its other comments kept",
              L"两个注释块:三个键只在第一个块里各一份,第二个块里的旧值删掉、别的注释留着")
TAGREPAIR_MSG(T5, L"ID3v2 in front (310 bytes) and ID3v1 at the end kept as they are; FFmpeg still reads the three tags",
              L"前面的 ID3v2(310 字节)与末尾的 ID3v1 原样保留,FFmpeg 照样读得到三个标签")
TAGREPAIR_MSG(T6, L"comment block would exceed the 16 MB limit: refused, the file is byte-identical", L"注释块会超过 16 MB 上限:拒绝,文件逐字节没动")
TAGREPAIR_MSG(T7_1, L"read-only file: refused, says read-only, file untouched", L"只读文件:拒绝,说明是只读,文件没动")
TAGREPAIR_MSG(T7_2, L"open in another program, even one that shares everything: refused, says in use, file untouched; no temporary file left",
              L"别的程序开着,哪怕它什么都肯共享:拒绝,说明被占用,文件没动;旁边不留临时文件")
TAGREPAIR_MSG(T7_3, L"can be repaired once it is no longer in use", L"占用解除后能修")
TAGREPAIR_MSG(T8, L"cut inside the metadata / cut inside the audio / one bit flipped in the audio / bad block length / junk after the metadata: "
                  L"never repaired, files byte-identical",
              L"截断在元数据里 / 截断在音频里 / 音频里坏一位 / 块长写坏 / 元数据后面夹着杂字节:一律不修,文件逐字节没动")
TAGREPAIR_MSG(T9_1, L"FLAC that is not MQA, with no tags at all: \"not MQA\", fix writes nothing, file byte-identical",
              L"不是 MQA 的 FLAC,标签一个都没有:判「不是 MQA」,fix 不写,文件逐字节没动")
TAGREPAIR_MSG(T9_2, L"WAV: MQA audio is \"can be saved as a FLAC copy\", other audio is \"not MQA\"; fix without a copy option writes and creates nothing",
              L"WAV:码流是 MQA 的判「可以另存为 FLAC」,不是的判「不是 MQA」;fix 不带另存选项时不写、也不新建任何文件")
TAGREPAIR_MSG(T9_3, L"lossy audio (μ-law WAV): \"not lossless PCM\"", L"有损编码(μ-law WAV):判「不是无损 PCM」")
TAGREPAIR_MSG(T9_4, L"a .flac file that is not audio: not called MQA, not written; a file that does not exist: \"can't be opened\"",
              L"扩展名是 .flac 但不是音频的文件:不判成 MQA、不写;不存在的文件:判「打不开」")
TAGREPAIR_MSG(T10, L"ORIGINALSAMPLERATE differs from the stream (96000 vs 352800): only that key is changed, official ENCODER / MQAENCODER kept",
              L"ORIGINALSAMPLERATE 与码流不符(96000 ↔ 352800):只改这一个,官方的 ENCODER / MQAENCODER 原样留着")
TAGREPAIR_MSG(T11_1, L"--dry-run: says it can be repaired and how; file byte-identical", L"--dry-run:说可以修、怎么修,文件逐字节没动")
TAGREPAIR_MSG(T11_2, L"--keep-backup: repaired, the original is kept unchanged as .bak", L"--keep-backup:修好,原文件原样留在 .bak")
TAGREPAIR_MSG(T12_1, L"the swap-in step fails (original opened by someone else): reported as failed, temporary file deleted, original byte-identical",
              L"换入新文件那一步失败(原文件被别人开着):报失败,临时文件删掉,原文件逐字节没动")
TAGREPAIR_MSG(T12_2, L"in-place write reads back wrong: reported as failed, the original bytes are written back, file byte-identical to before",
              L"原地写完读回不对:报失败,把原来的字节写回去,文件逐字节与修之前相同")
TAGREPAIR_MSG(T12_3, L"SHA-256 of the audio data differs before / after: failed (verify_failed), file byte-identical to before, no temporary file",
              L"修之前、修之后音频数据的 SHA-256 对不上:报失败(verify_failed),文件逐字节与修之前相同,不留临时文件")
TAGREPAIR_MSG(T12_4, L"MD5 of the samples decoded by FFmpeg differs before / after: the same", L"修之前、修之后 FFmpeg 解出的样本 MD5 对不上:同上")
TAGREPAIR_MSG(T12_5, L"disk write fails (disk full): failed (io, not verify_failed), file byte-identical to before, no temporary file",
              L"写盘失败(磁盘满):报失败(io,不是 verify_failed),文件逐字节与修之前相同,不留临时文件")
TAGREPAIR_MSG(T12_6, L"swap-in stops half-way and the new file can't get the original name: the repaired file is kept (the only copy), its path is reported",
              L"换入做到一半、新文件怎么也改不成原名:修好的文件原样留着(它是仅有的一份,不删),结果里写明它在哪")
TAGREPAIR_MSG(T12_7, L"swap-in stops half-way, renaming blocked twice: retried until it works, counts as repaired",
              L"换入做到一半、改名头两次被挡住:多试几次改成了,算修好")
TAGREPAIR_MSG(T12_8, L"with a backup, swap-in stops half-way and neither name can be restored: the original sits unchanged under the backup name, "
                     L"its path is reported; temporary file deleted",
              L"留备份时换入做到一半、两个名字都改不回去:原文件原样在备份名下,结果里写明它在哪;临时文件删掉")
TAGREPAIR_MSG(T12_9, L"with a backup, swap-in stops half-way and the new file can't come in: the original is renamed back, byte-identical, "
                     L"no backup and no temporary file left",
              L"留备份时换入做到一半、新文件进不来:把原文件从备份名改回原名,文件逐字节没动,不留备份与临时文件")
TAGREPAIR_MSG(T12_10, L"file system without ReplaceFile: falls back to rename-over, still repaired, creation time restored; "
                      L"with a backup, the backup is a byte-identical copy of the original",
              L"文件系统不支持 ReplaceFile:退回改名覆盖,照样修好,建立时间改回原来的;留备份时备份是原文件的逐字节拷贝")
TAGREPAIR_MSG(T12_11, L"file changed by another program between unlock and swap-in: not swapped in (the other change is not overwritten), temporary file deleted",
              L"放掉独占锁之后、换入之前文件被别的程序改了:不换入(不把别人的改动盖掉),临时文件删掉")
TAGREPAIR_MSG(T12_12, L"first swap-in blocked while the original is still there: checked unchanged, swapped again, repaired",
              L"第一次换入被挡住、原文件还在:核对它没变之后再换一次,修好")
TAGREPAIR_MSG(T13, L"48 kHz carrier (original 192 kHz) and 16-bit carrier (original 88.2 kHz): ORIGINALSAMPLERATE is the value in each stream",
              L"48 kHz 载波(原始 192 kHz)与 16 bit 载波(原始 88.2 kHz):ORIGINALSAMPLERATE 写的是各自码流里的值")
TAGREPAIR_MSG(T14, L"marker at 9.9 s is found, at 10.2 s it is not (only the first 10 seconds are examined); what is not found is not written",
              L"同步在 9.9 秒处认得出,在 10.2 秒处不认(只看开头 10 秒);不认的不写")
TAGREPAIR_MSG(T14_2, L"first record fails its check, a good one follows at 3 s: still found, the original rate comes from the later record (176.4 kHz)",
              L"第一条记录的校验对不上、3 秒处还有一条好的:照样认出来,原始采样率读的是后面那条(176.4 kHz)")
TAGREPAIR_MSG(T15, L"path longer than 260 characters, with Chinese: still repaired (whole-file rewrite and swap-in)",
              L"路径超过 260 个字符、带中文:照样能修(这里走的是整文件重写 + 换入)")
TAGREPAIR_MSG(T16, L"in-place writing disallowed (sector rule off): whole-file rewrite gives a file byte-identical to the in-place result of item 1",
              L"不许原地改写时(扇区条件关掉):改走整文件重写,出来的文件与第 1 项原地改的逐字节相同")
TAGREPAIR_MSG(T17_1, L"change crossing a sector boundary: whole-file rewrite and swap-in (file size unchanged); change inside one sector: written in place",
              L"改动跨过扇区边界的:整文件重写后换入(文件大小不变);全在一个扇区里的:原地改写")
TAGREPAIR_MSG(T17_2, L"file with the compression attribute: not written in place, whole-file rewrite and swap-in", L"带压缩属性的文件:不原地改写,整文件重写后换入")
TAGREPAIR_MSG(T18, L"existing .tagfix.tmp and similar files next to it are neither overwritten nor deleted; only leftovers of an interrupted run are removed "
                   L"(name and content match, nobody has them open)",
              L"旁边已有的 .tagfix.tmp 等文件一个不盖、不删;只清掉上次被打断留下的(名字、内容都对得上,且没人开着)")
TAGREPAIR_MSG(T19_1, L"--encoder-tag: the given value is written (an equals sign reads back unchanged); repairing again with the same rule changes nothing; "
                     L"the default rule judges by its own prefix",
              L"--encoder-tag:写进去的是给的值(带等号也原样读回);同一规则再修不动;默认规则照自己的前缀判")
TAGREPAIR_MSG(T19_2, L"--accept-prefix with an empty text: existing non-empty values are kept (ENCODER=Lavf… untouched), only empty and missing ones are written",
              L"--accept-prefix 给空串:已有的非空值留着(ENCODER=Lavf… 不动),空值与缺的才写")
TAGREPAIR_MSG(T20_1, L"one acceptable encoder key (the other absent / Lavf… / duplicated) plus an acceptable sample rate: \"tags complete\"; "
                     L"fix and --dry-run change nothing, files byte-identical",
              L"只有一个编码器键合格(另一个不在 / 是 Lavf… / 重复)+ 采样率合格:判「标签完整」,fix 与 --dry-run 都不动,文件逐字节相同")
TAGREPAIR_MSG(T20_2, L"both encoder keys unacceptable, or the sample rate wrong: \"can be repaired\"; the repair makes all three keys acceptable, "
                     L"an acceptable official ENCODER is kept",
              L"两个编码器键都不合格、或采样率不对:判「可以修复」;修的时候三个键都补到合格,原来合格的官方 ENCODER 留着")
TAGREPAIR_MSG(T21_1, L"cancel while scanning / locking / hashing / decoding (before anything is written): stops at once, no later phase entered, "
                     L"file byte-identical, no temporary file",
              L"在扫描 / 加锁 / 算指纹 / 解码时取消(还没写任何东西):立刻停,后面的阶段一个都没进,文件逐字节没动,不留临时文件")
TAGREPAIR_MSG(T21_2, L"cancel during the in-place write: that file is finished and verified, completely repaired", L"原地写的那一下取消:这个文件写完并核对,完整修好")
TAGREPAIR_MSG(T21_3, L"cancel while the temporary file is being written or verified: stops at once, temporary file deleted, original byte-identical",
              L"在写临时文件 / 核对临时文件时取消:立刻停,临时文件删掉,原文件逐字节没动")
TAGREPAIR_MSG(T21_4, L"cancel once the swap-in has begun: that file is finished, completely repaired, no temporary file", L"换入已经开始之后取消:这个文件做完,完整修好,不留临时文件")
TAGREPAIR_MSG(T21_5, L"cancel already requested before a file starts: nothing is opened for writing, file byte-identical",
              L"开始之前就已经取消:不去开文件写,文件逐字节没动")
TAGREPAIR_MSG(T22_1, L"MQA WAV, 16 and 24 bit, 44.1 and 48 kHz: scan says \"can be saved as a FLAC copy\"; --wav-to-flac writes <name>.flac with the same "
                     L"rate, channels and bit depth; the WAV is byte-identical",
              L"带标记的 WAV,16 / 24 bit、44.1 / 48 kHz:扫描说「可以另存为 FLAC」;--wav-to-flac 写出 <文件名>.flac,采样率、声道数、位深相同;WAV 逐字节没动")
TAGREPAIR_MSG(T22_2, L"the new FLAC: every sample equals the WAV's (compared here sample by sample from the raw bytes); it scans as MQA with complete tags; "
                     L"the MD5 in its header is that of the samples",
              L"新的 FLAC:每个采样都等于 WAV 里的(这里从原始字节逐个比过);扫描是 MQA 标签完整;文件头里的 MD5 就是这些采样的")
TAGREPAIR_MSG(T22_3, L"ordinary tags of the WAV (title, artist…) are carried over; the three tags are ours; no temporary file left",
              L"WAV 里的普通标签(标题、艺人……)带过去;三个识别标签是我们写的;不留临时文件")
TAGREPAIR_MSG(T22_4, L"target <name>.flac already exists: refused before anything is encoded, the existing file is byte-identical",
              L"<文件名>.flac 已经存在:还没开始编码就拒绝,已有的文件逐字节没动")
TAGREPAIR_MSG(T22_5, L"target appears while encoding: the final rename does not overwrite it; the existing file is byte-identical, temporary file deleted",
              L"编码的时候目标名下冒出一个文件:最后改名那一步不覆盖它;已有的文件逐字节没动,临时文件删掉")
TAGREPAIR_MSG(T22_6, L"a WAV without the marker is never converted; neither is a FLAC, a lossy WAV, or a file that can't be opened",
              L"没有标记的 WAV 绝不转;FLAC、有损的 WAV、打不开的文件也不转")
TAGREPAIR_MSG(T22_7, L"a deliberately corrupted copy (one byte of audio changed after writing) fails the check: deleted, nothing kept",
              L"故意弄坏的拷贝(写完之后改掉音频里一个字节)核对不过:删掉,什么都不留")
TAGREPAIR_MSG(T22_8, L"a copy with a wrong tag, or with one sample changed and a matching frame checksum, fails the check too: deleted",
              L"标签不对的拷贝、改了一个采样而且帧校验对得上的拷贝,同样核对不过:删掉")
TAGREPAIR_MSG(T22_9, L"cancel while encoding: stops at once, the check is never started, no new file and no temporary file; cancel during the check: the same",
              L"编码的时候取消:立刻停,不进核对那一步,不留新文件、不留临时文件;核对的时候取消:一样")
TAGREPAIR_MSG(T22_10, L"--dry-run: says what would be saved, creates nothing; the original in use by a writer: refused",
              L"--dry-run:说会另存什么,不新建任何文件;原文件正被别的程序写着:拒绝")
TAGREPAIR_MSG(T23_1, L"16-bit FLAC to a 24-bit copy: <name>.24bit.flac is 24 bit; each sample is the 16-bit sample shifted left by 8, the low 8 bits all zero "
                     L"(compared here sample by sample); the original is byte-identical",
              L"16 bit 的 FLAC 另存 24 bit:<文件名>.24bit.flac 是 24 bit;每个采样 = 16 bit 的采样左移 8 位,低 8 位全是零(这里逐个比过);原文件逐字节没动")
TAGREPAIR_MSG(T23_2, L"the 24-bit copy still contains the marker with the same original rate, scans as complete, and keeps the original's comments and picture",
              L"24 bit 的拷贝里标记还在、原始采样率相同,扫描是标签完整;原文件的注释与图片带过去")
TAGREPAIR_MSG(T23_3, L"16-bit WAV to a 24-bit copy: the same; a 24-bit file is not copied; existing <name>.24bit.flac: refused",
              L"16 bit 的 WAV 另存 24 bit:同样;24 bit 的文件不另存;<文件名>.24bit.flac 已经存在:拒绝")
TAGREPAIR_MSG(T23_4, L"a 16-bit FLAC with complete tags, and one whose tags were never repaired, both get a copy with complete tags; the originals stay as they were",
              L"标签完整的 16 bit FLAC、标签没修过的 16 bit FLAC,另存出来的拷贝标签都是完整的;原文件各自保持原样")

#undef TAGREPAIR_MSG

} // namespace msg
