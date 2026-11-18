package com.omni.tagrepair.core;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * 给人看的文字:每一句英文、中文各写一次。
 * 前面一大段是从 Windows 版的 src/messages.h 原样转过来的(同一个名字、同样两半);最后一段是安卓版自己加的。
 * {0}、{1}… 是放数据或另一句的位置。纯 Java,不依赖安卓。
 */
public final class Msg {
    private Msg() {}

    public static final class Entry {
        public final String name, en, zh;
        Entry(String n, String e, String z) { name = n; en = e; zh = z; }
    }

    /** 一句话的两半。数据(文件名、数字)两半相同。 */
    public static final class Text {
        public final String en, zh;
        public Text(String same) { en = same; zh = same; }
        public Text(String e, String z) { en = e; zh = z; }
        public boolean empty() { return en.isEmpty() && zh.isEmpty(); }
        public Text plus(Text o) { return new Text(en + o.en, zh + o.zh); }
        /** Windows 控制台上的写法:English (中文) */
        public String both() {
            if (en.equals(zh) || zh.isEmpty()) return en;
            if (en.isEmpty()) return zh;
            return en + " (" + zh + ")";
        }
        @Override public String toString() { return both(); }
    }

    public static final Text EMPTY = new Text("");

    private static final List<Entry> ALL = new ArrayList<Entry>();
    public static List<Entry> all() { return Collections.unmodifiableList(ALL); }

    static Entry e(String n, String en, String zh) {
        Entry x = new Entry(n, en, zh);
        ALL.add(x);
        return x;
    }

    static String fill(String pattern, Text[] args, boolean english) {
        StringBuilder out = new StringBuilder();
        for (int p = 0; p < pattern.length(); p++) {
            char c = pattern.charAt(p);
            if (c == '{' && p + 2 < pattern.length() && pattern.charAt(p + 1) >= '0' && pattern.charAt(p + 1) <= '9' && pattern.charAt(p + 2) == '}') {
                int i = pattern.charAt(p + 1) - '0';
                if (i < args.length) out.append(english ? args[i].en : args[i].zh);
                p += 2;
            } else {
                out.append(c);
            }
        }
        return out.toString();
    }

    public static Text of(Entry e, Text... args) { return new Text(fill(e.en, args, true), fill(e.zh, args, false)); }
    public static Text num(long n) { return new Text(Long.toString(n)); }
    public static Text data(String s) { return new Text(s); }

    // ---------------------------------------------------------------------------
    // 以下由 Windows 版 src/messages.h 转来(条目、两半原样)
    // ---------------------------------------------------------------------------
    public static final Entry Title = e("Title",
        "tag repair for MQA-encoded FLAC files",
        "MQA 标签修复");
    public static final Entry IntroWhat = e("IntroWhat",
        "Restores the lost identifying tags of MQA-encoded FLAC files that you own; the audio data is not changed.",
        "给你自己拥有的、已经是 MQA 编码的 FLAC 文件补回丢失的识别标签;音频数据不动。");
    public static final Entry IntroCancel = e("IntroCancel",
        "Press Ctrl+C at any time to cancel: a file that is being written is finished or put back first, then the program stops.",
        "随时可以按 Ctrl+C 取消:正在写的那个文件会先写完或者恢复原样,然后停下。");
    public static final Entry HowDrop1 = e("HowDrop1",
        "How to use: drag music files, or a whole folder, onto the tagrepair.exe icon and drop them there.",
        "用法:把音乐文件,或者整个文件夹,拖到 tagrepair.exe 的图标上再松手。");
    public static final Entry HowDrop2 = e("HowDrop2",
        "It scans first, subfolders included, and lists what it found.",
        "它先检查(文件夹连里面的子文件夹一起),把结果列出来。");
    public static final Entry HowDrop3 = e("HowDrop3",
        "It changes or creates a file only after you answer Y to its question; without a Y, nothing is changed.",
        "问过你、你回答 Y 之后才动手;不回答 Y,什么都不会改。");
    public static final Entry HowDrop4 = e("HowDrop4",
        "Command-line usage: tagrepair --help",
        "命令行的用法:tagrepair --help");
    public static final Entry PressEnter = e("PressEnter",
        "Press Enter to close this window.",
        "按回车键关闭窗口。");
    public static final Entry UseNoCommandLine = e("UseNoCommandLine",
        "Without a command line: drag music files or a folder onto tagrepair.exe; it scans first and acts only after you answer Y",
        "不用命令行:把音乐文件或文件夹拖到 tagrepair.exe 上,它先检查,问过你之后才动手");
    public static final Entry UseScan = e("UseScan",
        "Reports what each audio file is: not lossless PCM / not MQA / MQA with complete tags / MQA with missing or incomplete tags, can be repaired / MQA in a WAV file, can be saved as a FLAC copy / MQA but cannot be handled here",
        "说出每个音频文件是哪一类:不是无损 PCM / 不是 MQA / MQA 标签完整 / MQA 标签缺失或不完整,可以修复 / WAV 里的 MQA,可以另存为 FLAC / MQA 但这里处理不了");
    public static final Entry UseFix = e("UseFix",
        "Writes ENCODER / MQAENCODER / ORIGINALSAMPLERATE into the FLAC files that can be repaired; not one byte of audio data is changed",
        "给能修的 FLAC 补上 ENCODER / MQAENCODER / ORIGINALSAMPLERATE;音频数据一个字节都不动");
    public static final Entry UseDryRun = e("UseDryRun",
        "--dry-run: only say what would be done, write nothing; --keep-backup: keep the original as <name>.bak",
        "--dry-run:只说会怎么做,不写;--keep-backup:原文件留一份,名字是 <文件名>.bak");
    public static final Entry UseWavToFlac = e("UseWavToFlac",
        "--wav-to-flac: for MQA audio in a WAV file, save a new FLAC file <name>.flac next to it, with the tags; the WAV file is not changed",
        "--wav-to-flac:WAV 里的 MQA,在旁边另存一个带标签的新文件 <文件名>.flac;WAV 文件不改");
    public static final Entry UseCopy24 = e("UseCopy24",
        "--copy-24bit: for 16-bit MQA files, WAV or FLAC, save a 24-bit FLAC copy <name>.24bit.flac next to them: the same sample values with eight zero bits added below; the original is not changed",
        "--copy-24bit:16 bit 的 MQA 文件,WAV 和 FLAC 都行,在旁边另存一个 24 bit 的 FLAC,名字是 <文件名>.24bit.flac:采样值不变,低位补 8 个零;原文件不改");
    public static final Entry UseCopyOnly = e("UseCopyOnly",
        "--copy-only: only save the copies chosen above; do not repair any file in place",
        "--copy-only:只做上面选的另存,不原地修任何文件");
    public static final Entry UseCopyRules = e("UseCopyRules",
        "A copy never replaces an existing file, and it is kept only after every sample has been compared with the original",
        "另存从不覆盖已有的文件;新文件要逐个样本和原文件核对无误才留下");
    public static final Entry UseEncoderTag = e("UseEncoderTag",
        "--encoder-tag \"<value>\": the value written to ENCODER / MQAENCODER, default \"{0}\"; it must contain \"{1}\" and \"{2}\"",
        "--encoder-tag \"<值>\":写进 ENCODER / MQAENCODER 的值,默认 \"{0}\";值里必须带着 \"{1}\" 和 \"{2}\"");
    public static final Entry UseAcceptPrefix = e("UseAcceptPrefix",
        "--accept-prefix \"<text>\": an existing value that starts with this text is acceptable and is kept, default \"{0}\"; an empty text accepts any value that is not empty; scan takes these two options as well",
        "--accept-prefix \"<文字>\":已有的值以它开头就算合格、留着不动,默认 \"{0}\";给空串就是只要不是空值都合格;scan 也认这两个选项");
    public static final Entry UseJson = e("UseJson",
        "--json: one JSON object per line, for another program; --recursive: include subfolders",
        "--json:每行一个 JSON,给别的程序读;--recursive:连子文件夹一起");
    public static final Entry UseSelftest = e("UseSelftest",
        "Self-test: creates its own files and checks every rule above on them",
        "自检:自己造文件,把上面的每一条都验一遍");
    public static final Entry UseInfo = e("UseInfo",
        "Version, protocol number and commands, for a program that calls this one",
        "版本、协议号、认哪些命令,给调用它的程序看的");
    public static final Entry UseFfmpegDir = e("UseFfmpegDir",
        "scan / fix / selftest accept --ffmpeg-dir <folder>: the folder that holds the four FFmpeg run-time libraries ({0} and the others).",
        "scan / fix / selftest 都可以加 --ffmpeg-dir <文件夹>:FFmpeg 的四个运行库({0} 那几个)所在的文件夹。");
    public static final Entry UseFfmpegOrder = e("UseFfmpegOrder",
        "Without it, the program looks next to itself, then two folders up, then in the folder named by the environment variable TAGREPAIR_FFMPEG_DIR.",
        "不加的话依次找:本程序旁边、往上两级的文件夹、环境变量 TAGREPAIR_FFMPEG_DIR 指的文件夹。");
    public static final Entry UseExitCodes = e("UseExitCodes",
        "Exit codes: 0 done; 1 a file was refused or failed; 2 wrong usage; 3 FFmpeg run-time libraries not found; 4 cancelled",
        "退出码:0 正常;1 有文件被拒绝或失败;2 用法不对;3 找不到 FFmpeg 运行库;4 被取消");
    public static final Entry InfoLine = e("InfoLine",
        "{0} {1}: id {2}, protocol {3}; commands: info, scan, fix, selftest",
        "{0} {1}:标识 {2},协议 {3};命令:info、scan、fix、selftest");
    public static final Entry NoFfmpeg = e("NoFfmpeg",
        "FFmpeg run-time libraries not found: the four files {0} must sit together in one folder (looked in: {1}). Put them next to this program, or point to them with --ffmpeg-dir <folder> or the environment variable TAGREPAIR_FFMPEG_DIR",
        "找不到 FFmpeg 运行库:要 {0} 这四个文件放在同一个文件夹里(找过:{1})。把它们放到本程序旁边,或者用 --ffmpeg-dir <文件夹>、环境变量 TAGREPAIR_FFMPEG_DIR 指给它");
    public static final Entry OptionBad = e("OptionBad",
        "{0}: {1}",
        "{0}:{1}");
    public static final Entry OptionNotText = e("OptionNotText",
        "not valid text (a lone UTF-16 surrogate)",
        "不是合法的文字(UTF-16 里有落单的代理项)");
    public static final Entry OptionCopyOnlyAlone = e("OptionCopyOnlyAlone",
        "--copy-only needs --wav-to-flac or --copy-24bit",
        "--copy-only 要和 --wav-to-flac 或 --copy-24bit 一起用");
    public static final Entry OptionBackupNoCopy = e("OptionBackupNoCopy",
        "--keep-backup applies to in-place repair only; a copy never changes the original, so there is nothing to back up",
        "--keep-backup 只对原地修复有意义;另存从不改原文件,没有东西要备份");
    public static final Entry TagEmpty = e("TagEmpty",
        "the value is empty",
        "值是空的");
    public static final Entry TagTooLong = e("TagTooLong",
        "the value is too long (200 bytes at most)",
        "值太长(最多 200 字节)");
    public static final Entry TagNotUtf8 = e("TagNotUtf8",
        "the value is not valid UTF-8",
        "值不是合法的 UTF-8");
    public static final Entry TagControlChar = e("TagControlChar",
        "the value contains control characters (line breaks, tabs and the like)",
        "值里有控制字符(换行、制表符之类)");
    public static final Entry TagEdgeSpace = e("TagEdgeSpace",
        "the value must not begin or end with a space",
        "值的头尾不能是空格");
    public static final Entry TagSemicolon = e("TagSemicolon",
        "the value must not contain a semicolon",
        "值里不能有分号");
    public static final Entry TagNeedsMarks = e("TagNeedsMarks",
        "the value must contain \"tag repair\" and \"(unsigned)\" (they say that a tag repair tool wrote it, unsigned, and that it does not pose as an encoder)",
        "值里必须带着 \"tag repair\" 和 \"(unsigned)\"(说明是标签修复工具写的、没有签名,不冒充编码器)");
    public static final Entry PrefixTooLong = e("PrefixTooLong",
        "the prefix is too long (200 bytes at most)",
        "前缀太长(最多 200 字节)");
    public static final Entry PrefixNotUtf8 = e("PrefixNotUtf8",
        "the prefix is not valid UTF-8",
        "前缀不是合法的 UTF-8");
    public static final Entry PrefixControlChar = e("PrefixControlChar",
        "the prefix contains control characters",
        "前缀里有控制字符");
    public static final Entry StError = e("StError",
        "can't be opened: {0}",
        "打不开:{0}");
    public static final Entry StNotLossless = e("StNotLossless",
        "not lossless PCM ({0})",
        "不是无损 PCM({0})");
    public static final Entry StNoMarker = e("StNoMarker",
        "not MQA",
        "不是 MQA");
    public static final Entry StComplete = e("StComplete",
        "MQA, tags complete",
        "MQA,标签完整");
    public static final Entry StRepairMissing = e("StRepairMissing",
        "MQA, tags missing; can be repaired",
        "MQA,标签缺失,可以修复");
    public static final Entry StRepairIncomplete = e("StRepairIncomplete",
        "MQA, tags incomplete ({0}); can be repaired",
        "MQA,标签不完整({0}),可以修复");
    public static final Entry StRateUnknown = e("StRateUnknown",
        "MQA, tags incomplete; the stream does not state the original sample rate, so ORIGINALSAMPLERATE can't be written; not repaired",
        "MQA,标签不完整;码流里没读到原始采样率,ORIGINALSAMPLERATE 写不出来,不修");
    public static final Entry StUnsupported = e("StUnsupported",
        "MQA, but the {0} container can't be repaired here (FLAC only)",
        "MQA,但 {0} 容器这里修不了(只修 FLAC)");
    public static final Entry StDamaged = e("StDamaged",
        "MQA, but the file structure is damaged ({0}); not repaired",
        "MQA,但文件结构有问题({0}),不修");
    public static final Entry StConvertible = e("StConvertible",
        "MQA in a WAV file; can be saved as a FLAC copy with the tags, the WAV file is not changed",
        "WAV 里的 MQA;可以在旁边另存一个带标签的 FLAC,WAV 文件不改");
    public static final Entry StWavNoRate = e("StWavNoRate",
        "MQA in a WAV file, but the stream does not state the original sample rate, so no FLAC copy with complete tags can be saved",
        "WAV 里的 MQA,但码流里没读到原始采样率,存不出标签完整的 FLAC");
    public static final Entry StWavFormat = e("StWavFormat",
        "MQA in a WAV file, but only 16-bit and 24-bit PCM can be saved as a FLAC copy ({0})",
        "WAV 里的 MQA,但只有 16 bit、24 bit 的 PCM 能另存为 FLAC({0})");
    public static final Entry SysErrorCode = e("SysErrorCode",
        "Windows error {0}",
        "系统错误 {0}");
    public static final Entry WhyInUse = e("WhyInUse",
        "the file is in use by another program",
        "文件正被别的程序占用");
    public static final Entry WhyUnreadable = e("WhyUnreadable",
        "it can't be read",
        "读不了");
    public static final Entry ProbeNoSize = e("ProbeNoSize",
        "the file size can't be read",
        "读不到文件大小");
    public static final Entry ProbeNoMemory = e("ProbeNoMemory",
        "not enough memory",
        "内存不够");
    public static final Entry ProbeNotAudio = e("ProbeNotAudio",
        "not an audio file that FFmpeg recognises",
        "不是 FFmpeg 认得的音频文件");
    public static final Entry ProbeNoStreamInfo = e("ProbeNoStreamInfo",
        "the stream information can't be read",
        "读不出流信息");
    public static final Entry ProbeNoAudioTrack = e("ProbeNoAudioTrack",
        "no audio track",
        "没有音轨");
    public static final Entry ProbeNoDecoder = e("ProbeNoDecoder",
        "the decoder can't be opened",
        "解码器打不开");
    public static final Entry ProbeNoMd5 = e("ProbeNoMd5",
        "the MD5 of the system can't be used",
        "系统的 MD5 用不了");
    public static final Entry ProbeCancelled = e("ProbeCancelled",
        "cancelled",
        "已取消");
    public static final Entry LayIo = e("LayIo",
        "the file can't be read",
        "读不了文件");
    public static final Entry LayId3TooLong = e("LayId3TooLong",
        "the ID3v2 length exceeds the file",
        "ID3v2 的长度超出文件");
    public static final Entry LayNotFlac = e("LayNotFlac",
        "not a FLAC file",
        "不是 FLAC 文件");
    public static final Entry LayHeaderCut = e("LayHeaderCut",
        "a metadata block header is incomplete",
        "元数据块的头不全");
    public static final Entry LayBadType = e("LayBadType",
        "invalid metadata block type",
        "元数据块类型不合法");
    public static final Entry LayFirstNotStreamInfo = e("LayFirstNotStreamInfo",
        "the first metadata block is not STREAMINFO",
        "第一个元数据块不是 STREAMINFO");
    public static final Entry LayTwoStreamInfo = e("LayTwoStreamInfo",
        "two STREAMINFO blocks",
        "有两个 STREAMINFO");
    public static final Entry LayBlockBeyondFile = e("LayBlockBeyondFile",
        "a metadata block exceeds the file",
        "元数据块超出文件");
    public static final Entry LayTooManyBlocks = e("LayTooManyBlocks",
        "too many metadata blocks",
        "元数据块太多");
    public static final Entry LayMetaTooBig = e("LayMetaTooBig",
        "the metadata area is too large",
        "元数据区太大");
    public static final Entry LayRateZero = e("LayRateZero",
        "the sample rate in STREAMINFO is 0",
        "STREAMINFO 的采样率是 0");
    public static final Entry LayNoFrameAfterMeta = e("LayNoFrameAfterMeta",
        "no audio frame follows the metadata",
        "元数据之后不是音频帧");
    public static final Entry LayNoAudio = e("LayNoAudio",
        "no audio frames",
        "没有音频帧");
    public static final Entry LayMetaUnreadable = e("LayMetaUnreadable",
        "the metadata area can't be read",
        "读不了元数据区");
    public static final Entry LayBadComment = e("LayBadComment",
        "the comment block is damaged",
        "注释块损坏");
    public static final Entry FixInUse = e("FixInUse",
        "the file is in use by another program; the file is as it was",
        "文件正被别的程序占用,没有改动");
    public static final Entry FixComplete = e("FixComplete",
        "tags are complete; nothing changed",
        "标签完整,没有改动");
    public static final Entry FixReadOnly = e("FixReadOnly",
        "the file is read-only; the file is as it was",
        "文件是只读的,没有改动");
    public static final Entry FixAccessDenied = e("FixAccessDenied",
        "no permission to write this file; the file is as it was",
        "没有写这个文件的权限,没有改动");
    public static final Entry FixOpenFailed = e("FixOpenFailed",
        "the file can't be opened; the file is as it was",
        "打不开:{0}");
    public static final Entry FixReadFailed = e("FixReadFailed",
        "the disk could not be read or written; the file is as it was",
        "读不了文件,没有改动");
    public static final Entry FixTooBig = e("FixTooBig",
        "the tags would exceed the FLAC block size limit; the file is as it was",
        "标签写进去会超过 FLAC 元数据块 16 MB 的上限,没有改动");
    public static final Entry FixMetaDamaged = e("FixMetaDamaged",
        "the metadata can't be parsed; the file is as it was",
        "元数据区解析不了,没有改动");
    public static final Entry FixInternalLength = e("FixInternalLength",
        "internal error; the file is as it was",
        "内部错误(元数据长度对不上),没有改动");
    public static final Entry FixInternalCheck = e("FixInternalCheck",
        "internal error; the file is as it was",
        "内部错误(算出的元数据自己核对不过),没有改动");
    public static final Entry FixDecodeFailed = e("FixDecodeFailed",
        "the audio has bad frames; the file is as it was",
        "解码不了({0}),没有改动");
    public static final Entry FixAudioBadFrames = e("FixAudioBadFrames",
        "the audio has bad frames; the file is as it was",
        "音频不完整或有坏帧(解出 {0} 个采样,文件头写的是 {1};解码报错 {2} 处),没有改动");
    public static final Entry FixAudioShort = e("FixAudioShort",
        "the audio is incomplete; the file is as it was",
        "音频不完整或有坏帧(解出 {0} 个采样,文件头写的是 {1};解码报错 {2} 处),没有改动");
    public static final Entry FixDecodeErrors = e("FixDecodeErrors",
        "the audio has bad frames; the file is as it was",
        "音频解码报错 {0} 处(文件可能损坏),没有改动");
    public static final Entry FixWouldFix = e("FixWouldFix",
        "can be repaired (dry run, nothing changed): {0}",
        "可以修复(试运行,没有改动):{0}");
    public static final Entry FixWriteFailed = e("FixWriteFailed",
        "the disk could not be read or written",
        "写入失败({0})");
    public static final Entry FixTailRestored = e("FixTailRestored",
        "; the file is as it was",
        ";已把原来的字节写回去,文件保持原样");
    public static final Entry FixTailNotRestored = e("FixTailNotRestored",
        "; the original bytes could not be written back, the tag area of this file may be damaged (audio data was not touched)",
        ";原来的字节也没能写回去,这个文件的标签区可能已经损坏(音频数据没有动过)");
    public static final Entry FixTailTempGone = e("FixTailTempGone",
        "; the file is as it was",
        ";临时文件已删,原文件没有动");
    public static final Entry FixTempCreate = e("FixTempCreate",
        "the disk could not be read or written; the file is as it was",
        "旁边建不了临时文件({0}),没有改动");
    public static final Entry FixTempWrite = e("FixTempWrite",
        "the disk could not be read or written; the file is as it was",
        "写临时文件失败({0});临时文件已删,原文件没有动");
    public static final Entry FixGuardOpen = e("FixGuardOpen",
        "the disk could not be read or written; the file is as it was",
        "换入之前打不开原文件({0}),没有改动");
    public static final Entry FixChanged = e("FixChanged",
        "another program changed the file meanwhile; the file is as it was",
        "文件在换入之前被别的程序改了,没有改动");
    public static final Entry FixSwapRestored = e("FixSwapRestored",
        "the new file could not be swapped in; the file is as it was",
        "换入新文件没成功({0});原文件已经放回原来的名字,内容没有动;临时文件已删");
    public static final Entry FixSwapAtBackup = e("FixSwapAtBackup",
        "the swap-in stopped half-way; the file is now at {1}",
        "换入新文件没成功({0});原文件的内容没有动,但它现在的名字是 {1},请把它改回 {2}");
    public static final Entry FixSwapLost = e("FixSwapLost",
        "the swap-in stopped half-way; no file is left under the original name and the new file can't be found",
        "换入新文件没成功({0});原来的名字下已经没有文件,写好的新文件也找不到了");
    public static final Entry FixSwapAtTemp = e("FixSwapAtTemp",
        "the swap-in stopped half-way; the file is now at {1}",
        "换入新文件没成功({0});原来的名字下已经没有文件,修好并核对过的文件留在 {1},请把它改名为 {2}");
    public static final Entry FixSwapFailed = e("FixSwapFailed",
        "the new file could not be swapped in; the file is as it was",
        "换入新文件没成功({0});临时文件已删,原文件没有动");
    public static final Entry FixAfterSwapDiffers = e("FixAfterSwapDiffers",
        "the read-back check failed: the swapped-in file differs from what was written; please check this file",
        "换入之后的文件与写好的不一致,请核对这个文件");
    public static final Entry FixCancelled = e("FixCancelled",
        "cancelled; the file is as it was",
        "已取消,这个文件没有改动");
    public static final Entry FixCancelledTemp = e("FixCancelledTemp",
        "cancelled; the file is as it was",
        "已取消;临时文件已删,原文件没有动");
    public static final Entry VerLayout = e("VerLayout",
        "the read-back check failed: the metadata can't be parsed ({0})",
        "写完之后元数据区走不通({0})");
    public static final Entry VerAudioStart = e("VerAudioStart",
        "the read-back check failed: the audio starts at the wrong offset",
        "写完之后音频起点不对");
    public static final Entry VerAudioLength = e("VerAudioLength",
        "the read-back check failed: the audio length changed",
        "写完之后音频长度变了");
    public static final Entry VerStreamInfo = e("VerStreamInfo",
        "the read-back check failed: STREAMINFO changed",
        "写完之后 STREAMINFO 变了");
    public static final Entry VerPrefix = e("VerPrefix",
        "the read-back check failed: the bytes before fLaC changed",
        "写完之后文件开头(fLaC 之前)变了");
    public static final Entry VerMeta = e("VerMeta",
        "the read-back check failed: the metadata differs from what was to be written",
        "写完之后元数据区与要写的不一致");
    public static final Entry VerTags = e("VerTags",
        "the read-back check failed: the tags read back are wrong",
        "读回的标签不对");
    public static final Entry VerTag = e("VerTag",
        "the read-back check failed: {0} read back is wrong",
        "读回的 {0} 不对");
    public static final Entry VerSha = e("VerSha",
        "the read-back check failed: the SHA-256 of the audio data differs from before",
        "音频数据的 SHA-256 与修之前不同");
    public static final Entry VerNoMarker = e("VerNoMarker",
        "the read-back check failed: FFmpeg no longer finds the marker",
        "FFmpeg 读回:认不出 MQA 了");
    public static final Entry VerFfTag = e("VerFfTag",
        "the read-back check failed: {0} as read by FFmpeg is wrong",
        "FFmpeg 读回的 {0} 不对");
    public static final Entry VerPcm = e("VerPcm",
        "the read-back check failed: the samples decoded by FFmpeg differ from before",
        "FFmpeg 解出的样本与修之前不同");
    public static final Entry CopyCreated = e("CopyCreated",
        "saved {0}: {1} bit, {2} samples per channel, every sample identical to the original",
        "已另存 {0}:{1} bit,每声道 {2} 个采样,逐个样本与原文件相同");
    public static final Entry CopyCreated24 = e("CopyCreated24",
        "saved {0}: 24 bit, the {1} 16-bit samples per channel of the original, each with eight zero bits added below",
        "已另存 {0}:24 bit,原文件每声道 {1} 个 16 bit 采样,每个低位补了 8 个零");
    public static final Entry CopyWould = e("CopyWould",
        "would save {0} (dry run, nothing written)",
        "会另存 {0}(试运行,什么都没有写)");
    public static final Entry CopySkipNotWav = e("CopySkipNotWav",
        "no FLAC copy: {0}",
        "不另存为 FLAC:{0}");
    public static final Entry CopySkipNot16 = e("CopySkipNot16",
        "no 24-bit copy: this is not a 16-bit MQA file that can be copied ({0})",
        "不另存 24 bit:这不是能另存的 16 bit MQA 文件({0})");
    public static final Entry CopyTargetExists = e("CopyTargetExists",
        "{0} already exists; an existing file is never overwritten, nothing was written",
        "{0} 已经存在;已有的文件从不覆盖,什么都没有写");
    public static final Entry CopySourceInUse = e("CopySourceInUse",
        "the original is being written by another program; nothing was written",
        "原文件正被别的程序写着,没有另存");
    public static final Entry CopySourceOpen = e("CopySourceOpen",
        "the original can't be opened; nothing was written",
        "原文件打不开:{0};没有另存");
    public static final Entry CopyNoEncoder = e("CopyNoEncoder",
        "this FFmpeg has no usable FLAC encoder; nothing was written",
        "这份 FFmpeg 里没有能用的 FLAC 编码器;没有另存");
    public static final Entry CopyDecode = e("CopyDecode",
        "the original can't be decoded completely; nothing was kept",
        "原文件解码不完整({0});没有留下新文件");
    public static final Entry CopyEncode = e("CopyEncode",
        "encoding failed; nothing was kept",
        "编码失败;没有留下新文件");
    public static final Entry CopyTempCreate = e("CopyTempCreate",
        "the temporary file can't be created next to the original; nothing was written",
        "旁边建不了临时文件({0}),没有另存");
    public static final Entry CopyTempWrite = e("CopyTempWrite",
        "writing the temporary file failed; it was deleted, nothing was kept",
        "写临时文件失败({0});临时文件已删,没有留下新文件");
    public static final Entry CopyVerifyFailed = e("CopyVerifyFailed",
        "the new file failed the check against the original ({0}); it was deleted, nothing was kept",
        "新文件和原文件核对没通过({0});已删掉,没有留下新文件");
    public static final Entry CopyRename = e("CopyRename",
        "the new file could not be given its final name; it was deleted, nothing was kept",
        "新文件改不成正式的名字({0});已删掉,没有留下新文件");
    public static final Entry CopyCancelled = e("CopyCancelled",
        "cancelled; no new file was kept",
        "已取消;没有留下新文件");
    public static final Entry CvLayout = e("CvLayout",
        "its structure can't be parsed",
        "结构解析不了");
    public static final Entry CvFormat = e("CvFormat",
        "sample rate, channel count or bit depth is wrong",
        "采样率、声道数或位深不对");
    public static final Entry CvCount = e("CvCount",
        "the number of samples differs",
        "采样个数不同");
    public static final Entry CvHeaderMd5 = e("CvHeaderMd5",
        "the MD5 in its header does not match the samples of the original",
        "文件头里的 MD5 和原文件的采样对不上");
    public static final Entry CvScan = e("CvScan",
        "it does not scan as MQA with complete tags",
        "扫描出来不是 MQA 标签完整");
    public static final Entry CvTags = e("CvTags",
        "the tags read back are wrong",
        "读回的标签不对");
    public static final Entry CvDecode = e("CvDecode",
        "it can't be decoded without errors",
        "解码有错");
    public static final Entry CvSamples = e("CvSamples",
        "the decoded samples differ from the original",
        "解出的采样和原文件不同");
    public static final Entry CvLowBits = e("CvLowBits",
        "the low 8 bits are not all zero, or the upper 16 bits differ from the original",
        "低 8 位不全是零,或者高 16 位和原文件不同");
    public static final Entry OutCopies = e("OutCopies",
        "{0} copies",
        "共 {0} 份");
    public static final Entry OutCarrier = e("OutCarrier",
        "carrier {0} / {1} bit; original sample rate in the stream: {2}",
        "载波 {0} / {1} bit;码流里的原始采样率:{2}");
    public static final Entry OutActions = e("OutActions",
        "possible here: {0}",
        "这里可以做:{0}");
    public static final Entry ActRepair = e("ActRepair",
        "repair the tags in place",
        "原地修复标签");
    public static final Entry ActWavToFlac = e("ActWavToFlac",
        "save a FLAC copy with --wav-to-flac",
        "用 --wav-to-flac 另存为 FLAC");
    public static final Entry ActCopy24 = e("ActCopy24",
        "save a 24-bit copy with --copy-24bit",
        "用 --copy-24bit 另存 24 bit 的拷贝");
    public static final Entry OutFixed = e("OutFixed",
        "repaired, {0}",
        "已修复,{0}");
    public static final Entry ModeInPlace = e("ModeInPlace",
        "written in place",
        "原地改写");
    public static final Entry ModeRewrite = e("ModeRewrite",
        "whole file rewritten, then swapped in",
        "整文件重写后换入");
    public static final Entry OutAudioSame = e("OutAudioSame",
        "audio data untouched: {0} bytes, SHA-256 {1}…; MD5 of the decoded samples {2}, the same before and after",
        "音频数据未动:{0} 字节,SHA-256 {1}…;解码样本 MD5 {2},修之前、修之后相同");
    public static final Entry OutBackupAt = e("OutBackupAt",
        "the original is kept at {0}",
        "原文件留在 {0}");
    public static final Entry OutStaleRemoved = e("OutStaleRemoved",
        "removed {0} temporary file(s) left behind by an interrupted earlier run",
        "顺手删掉了上次被打断留下的临时文件 {0} 个");
    public static final Entry OutWouldMode = e("OutWouldMode",
        "{0}; method: {1}",
        "{0};方式:{1}");
    public static final Entry SumScan = e("SumScan",
        "{0} file(s): MQA with complete tags {1}, can be repaired {2}, WAV that can be saved as FLAC {3}, MQA but cannot be handled here {4}, not MQA {5}, not lossless PCM {6}, cannot be opened {7}",
        "共 {0} 个文件:MQA 标签完整 {1},可以修复 {2},可以另存为 FLAC 的 WAV {3},MQA 但这里处理不了 {4},不是 MQA {5},不是无损 PCM {6},打不开 {7}");
    public static final Entry SumFix = e("SumFix",
        "{0} file(s): repaired {1}, complete and untouched {2}, skipped {3}, refused {4}, failed {5}",
        "共 {0} 个文件:已修复 {1},标签完整没动 {2},不属于能修的 {3},拒绝 {4},失败 {5}");
    public static final Entry SumFixDry = e("SumFixDry",
        "{0} file(s): can be repaired {1}, complete and untouched {2}, skipped {3}, refused {4}, failed {5}",
        "共 {0} 个文件:可以修复 {1},标签完整没动 {2},不属于能修的 {3},拒绝 {4},失败 {5}");
    public static final Entry SumCopy = e("SumCopy",
        "{0}: saved {1}, not applicable {2}, refused {3}, failed {4}",
        "{0}:新存 {1},不适用 {2},拒绝 {3},失败 {4}");
    public static final Entry SumCopyDry = e("SumCopyDry",
        "{0}: would save {1}, not applicable {2}, refused {3}, failed {4}",
        "{0}:会新存 {1},不适用 {2},拒绝 {3},失败 {4}");
    public static final Entry KindWavToFlac = e("KindWavToFlac",
        "WAV to FLAC",
        "WAV 另存为 FLAC");
    public static final Entry KindCopy24 = e("KindCopy24",
        "24-bit copies",
        "另存 24 bit");
    public static final Entry PgScan = e("PgScan",
        "Scanning",
        "检查");
    public static final Entry PgRepair = e("PgRepair",
        "Repairing",
        "修复");
    public static final Entry PgSave = e("PgSave",
        "Saving",
        "另存");
    public static final Entry CancelScan = e("CancelScan",
        "Cancelled. Scanned {0} of {1}; nothing was changed.",
        "已取消。{1} 个里检查了 {0} 个;什么都没有改。");
    public static final Entry CancelRepair = e("CancelRepair",
        "Cancelled. Repaired {0} of {1}",
        "已取消。{1} 个里修好了 {0} 个");
    public static final Entry CancelSave = e("CancelSave",
        "Cancelled. Saved {0} of {1}",
        "已取消。{1} 个里存好了 {0} 个");
    public static final Entry CancelPartSkipped = e("CancelPartSkipped",
        "; {0} skipped (nothing to do for them)",
        ",{0} 个不用处理");
    public static final Entry CancelPartNotDone = e("CancelPartNotDone",
        "; {0} could not be done, see above",
        ",{0} 个没有做成,原因见上面");
    public static final Entry CancelPartStopped = e("CancelPartStopped",
        "; {0} stopped part-way and left as it was",
        ",{0} 个做到一半停下、保持原样");
    public static final Entry CancelPartNotStarted = e("CancelPartNotStarted",
        "; {0} not started.",
        ",{0} 个没有开始。");
    public static final Entry CancelAtPrompt = e("CancelAtPrompt",
        "Cancelled. Nothing was changed.",
        "已取消。什么都没有改。");
    public static final Entry CancelAtPromptAfter = e("CancelAtPromptAfter",
        "Cancelled. Nothing else was changed.",
        "已取消。别的什么都没有改。");
    public static final Entry AskScanning = e("AskScanning",
        "Scanning {0} file(s)…",
        "正在检查 {0} 个文件……");
    public static final Entry AskScanned = e("AskScanned",
        "Scanned {0} file(s):",
        "检查了 {0} 个文件:");
    public static final Entry AskComplete = e("AskComplete",
        "MQA, tags complete: {0}",
        "MQA,标签完整:{0} 个");
    public static final Entry AskRepairable = e("AskRepairable",
        "MQA, tags missing or incomplete; can be repaired in place: {0}",
        "MQA,标签缺失或不完整,可以原地修复:{0} 个");
    public static final Entry AskWav = e("AskWav",
        "WAV: can be saved as a new FLAC file next to the original, the WAV is not changed: {0}",
        "WAV:可以在原文件旁边另存一个新的 FLAC 文件,WAV 不改:{0} 个");
    public static final Entry Ask16 = e("Ask16",
        "16-bit: tags can be repaired; some devices recognise only 24-bit files — a 24-bit copy can be saved next to the original: {0}",
        "16 bit:标签可以修;有的设备只认 24 bit 的文件,可以在原文件旁边另存一个 24 bit 的拷贝:{0} 个");
    public static final Entry AskAlreadyThere = e("AskAlreadyThere",
        "already there, will not be written again: {0}",
        "已经有了,不会再写:{0}");
    public static final Entry AskNoMarker = e("AskNoMarker",
        "Not MQA: {0}",
        "不是 MQA:{0} 个");
    public static final Entry AskCannot = e("AskCannot",
        "Cannot be handled: {0}",
        "处理不了:{0} 个");
    public static final Entry AskNothingToDo = e("AskNothingToDo",
        "Nothing to do. Nothing was changed.",
        "没有需要处理的文件,什么都没有改。");
    public static final Entry AskRepairExplain = e("AskRepairExplain",
        "Repair writes three tags, ENCODER / MQAENCODER / ORIGINALSAMPLERATE, into the tag area of each file; the audio data is not changed. Everything is checked after writing, and the file is put back as it was if anything differs.",
        "修复只往文件的标签区写三个标签:ENCODER / MQAENCODER / ORIGINALSAMPLERATE,音频数据不动;写完会核对,有一处对不上就恢复原样。");
    public static final Entry AskRepair = e("AskRepair",
        "Repair these {0} file(s) in place? Y + Enter = start, anything else = leave them",
        "原地修复这 {0} 个文件吗?输入 Y 再按回车就开始,别的回答都算不修");
    public static final Entry AskWavExplain = e("AskWavExplain",
        "A new FLAC file is written next to each WAV: the same sample values, losslessly encoded, with the three tags. The WAV file is not changed, and an existing file is never overwritten.",
        "在每个 WAV 旁边新写一个 FLAC 文件:采样值一模一样、无损编码,带上三个标签。WAV 文件不改,已有的文件也从不覆盖。");
    public static final Entry AskWavQuestion = e("AskWavQuestion",
        "Save {0} WAV file(s) as new FLAC files? Y + Enter = start, anything else = leave them",
        "把这 {0} 个 WAV 另存为新的 FLAC 文件吗?输入 Y 再按回车就开始,别的回答都算不存");
    public static final Entry Ask24Explain = e("Ask24Explain",
        "A 24-bit FLAC copy is written next to each 16-bit file: every sample keeps its value and gets eight zero bits below it; nothing is recomputed. The original is not changed, and an existing file is never overwritten.",
        "在每个 16 bit 的文件旁边新写一个 24 bit 的 FLAC:每个采样值不变,只在低位补 8 个零,不做任何重新计算。原文件不改,已有的文件也从不覆盖。");
    public static final Entry Ask24Question = e("Ask24Question",
        "Save 24-bit copies of these {0} file(s)? Y + Enter = start, anything else = leave them",
        "给这 {0} 个文件另存 24 bit 的拷贝吗?输入 Y 再按回车就开始,别的回答都算不存");
    public static final Entry StartRepair = e("StartRepair",
        "Starting: repairing {0} file(s)…",
        "开始修复 {0} 个文件……");
    public static final Entry StartWav = e("StartWav",
        "Starting: saving {0} WAV file(s) as FLAC…",
        "开始把 {0} 个 WAV 另存为 FLAC……");
    public static final Entry Start24 = e("Start24",
        "Starting: saving 24-bit copies of {0} file(s)…",
        "开始给 {0} 个文件另存 24 bit 的拷贝……");
    public static final Entry SkipRepair = e("SkipRepair",
        "Not repaired. Nothing was changed.",
        "没有修复,什么都没有改。");
    public static final Entry SkipWav = e("SkipWav",
        "No FLAC file was saved. Nothing was changed.",
        "没有另存 FLAC,什么都没有改。");
    public static final Entry Skip24 = e("Skip24",
        "No 24-bit copy was saved. Nothing was changed.",
        "没有另存 24 bit 的拷贝,什么都没有改。");
    public static final Entry SkipWavAfter = e("SkipWavAfter",
        "No FLAC file was saved. Nothing else was changed.",
        "没有另存 FLAC,别的什么都没有改。");
    public static final Entry Skip24After = e("Skip24After",
        "No 24-bit copy was saved. Nothing else was changed.",
        "没有另存 24 bit 的拷贝,别的什么都没有改。");
    public static final Entry DoneRepair = e("DoneRepair",
        "Repaired {0}; already complete and untouched {1}; not repaired {2}",
        "已修复 {0} 个,已经完整没动 {1} 个,没修成 {2} 个");
    public static final Entry DoneSave = e("DoneSave",
        "Saved {0} new file(s); not saved {1}",
        "新存了 {0} 个文件,没存成 {1} 个");
    public static final Entry DoneSeeAbove = e("DoneSeeAbove",
        "; the reason is on each file's line above",
        ";原因见上面各自的那一行");
    public static final Entry TestPass = e("TestPass",
        "PASS",
        "通过");
    public static final Entry TestFail = e("TestFail",
        "FAIL",
        "失败");
    public static final Entry TestFolder = e("TestFolder",
        "Self-test folder: {0}",
        "自检目录:{0}");
    public static final Entry TestCannotWrite = e("TestCannotWrite",
        "self-test: can't write {0}",
        "自检:写不了 {0}");
    public static final Entry TestSummary = e("TestSummary",
        "passed {0}, failed {1}",
        "通过 {0},失败 {1}");
    public static final Entry TestNoCompression = e("TestNoCompression",
        "this disk does not take the compression attribute; only \"can be repaired\" was checked",
        "这个盘设不了压缩属性,这一条只验了能修好");
    public static final Entry TestWhyParse = e("TestWhyParse",
        "the repaired file can't be parsed",
        "修完的文件解析不了");
    public static final Entry TestWhyAudio = e("TestWhyAudio",
        "the audio bytes changed",
        "音频字节变了");
    public static final Entry TestWhyStreamInfo = e("TestWhyStreamInfo",
        "STREAMINFO changed",
        "STREAMINFO 变了");
    public static final Entry TestWhyPrefix = e("TestWhyPrefix",
        "the bytes before fLaC changed",
        "fLaC 之前的字节变了");
    public static final Entry TestWhyKeys = e("TestWhyKeys",
        "the three keys are not once each, or a value is unacceptable",
        "三个键不是各一份,或者值不合格");
    public static final Entry TestWhyValue = e("TestWhyValue",
        "wrong value: {0} = {1}",
        "值不对:{0} = {1}");
    public static final Entry TestWhyRescan = e("TestWhyRescan",
        "scanning again does not say complete: {0}",
        "修完再扫描不是「完整」:{0}");
    public static final Entry TestWhyFfmpeg = e("TestWhyFfmpeg",
        "{0} as read by FFmpeg is wrong",
        "FFmpeg 读回的 {0} 不对");
    public static final Entry T1_1 = e("T1_1",
        "scan: MQA, original rate 352.8 kHz, tags incomplete, can be repaired",
        "扫描:MQA、原始采样率 352.8 kHz、标签不完整、可以修复");
    public static final Entry T1_2 = e("T1_2",
        "repair: three tags written; audio frames to the end of the file byte-identical; audio offset and file size unchanged",
        "修复:三个标签写进去,音频帧到文件末尾逐字节相同,音频起点与文件大小不变");
    public static final Entry T1_3 = e("T1_3",
        "other comments and their order, vendor string, seek table and picture untouched; encoder=Lavf… replaced where it was, no extra copy",
        "别的注释与顺序、厂商串、定位表、图片都没动;encoder=Lavf… 在原位换掉、没有多一份");
    public static final Entry T1_4 = e("T1_4",
        "repair again: nothing happens, the file is byte-identical",
        "再修一次:不动,文件逐字节相同");
    public static final Entry T2_1 = e("T2_1",
        "no PADDING: whole file rewritten and swapped in, audio byte-identical (its offset moved)",
        "没有 PADDING:整文件重写后换入,音频逐字节相同(起点往后挪了)");
    public static final Entry T2_2 = e("T2_2",
        "creation time kept; no temporary file left",
        "建立时间保留;旁边不留临时文件");
    public static final Entry T2_3 = e("T2_3",
        "repair again: nothing happens, the file is byte-identical",
        "再修一次:不动,文件逐字节相同");
    public static final Entry T3 = e("T3",
        "no comment block: one is created before PADDING; application block and picture kept as they are",
        "没有注释块:新建一个,放在 PADDING 之前;应用块、图片原样留着");
    public static final Entry T4 = e("T4",
        "two comment blocks: the three keys once each in the first; old values in the second removed, its other comments kept",
        "两个注释块:三个键只在第一个块里各一份,第二个块里的旧值删掉、别的注释留着");
    public static final Entry T5 = e("T5",
        "ID3v2 in front (310 bytes) and ID3v1 at the end kept as they are; FFmpeg still reads the three tags",
        "前面的 ID3v2(310 字节)与末尾的 ID3v1 原样保留,FFmpeg 照样读得到三个标签");
    public static final Entry T6 = e("T6",
        "comment block would exceed the 16 MB limit: refused, the file is byte-identical",
        "注释块会超过 16 MB 上限:拒绝,文件逐字节没动");
    public static final Entry T7_1 = e("T7_1",
        "read-only file: refused, says read-only, file untouched",
        "只读文件:拒绝,说明是只读,文件没动");
    public static final Entry T7_2 = e("T7_2",
        "open in another program, even one that shares everything: refused, says in use, file untouched; no temporary file left",
        "别的程序开着,哪怕它什么都肯共享:拒绝,说明被占用,文件没动;旁边不留临时文件");
    public static final Entry T7_3 = e("T7_3",
        "can be repaired once it is no longer in use",
        "占用解除后能修");
    public static final Entry T8 = e("T8",
        "cut inside the metadata / cut inside the audio / one bit flipped in the audio / bad block length / junk after the metadata: never repaired, files byte-identical",
        "截断在元数据里 / 截断在音频里 / 音频里坏一位 / 块长写坏 / 元数据后面夹着杂字节:一律不修,文件逐字节没动");
    public static final Entry T9_1 = e("T9_1",
        "FLAC that is not MQA, with no tags at all: \"not MQA\", fix writes nothing, file byte-identical",
        "不是 MQA 的 FLAC,标签一个都没有:判「不是 MQA」,fix 不写,文件逐字节没动");
    public static final Entry T9_2 = e("T9_2",
        "WAV: MQA audio is \"can be saved as a FLAC copy\", other audio is \"not MQA\"; fix without a copy option writes and creates nothing",
        "WAV:码流是 MQA 的判「可以另存为 FLAC」,不是的判「不是 MQA」;fix 不带另存选项时不写、也不新建任何文件");
    public static final Entry T9_3 = e("T9_3",
        "lossy audio (μ-law WAV): \"not lossless PCM\"",
        "有损编码(μ-law WAV):判「不是无损 PCM」");
    public static final Entry T9_4 = e("T9_4",
        "a .flac file that is not audio: not called MQA, not written; a file that does not exist: \"can't be opened\"",
        "扩展名是 .flac 但不是音频的文件:不判成 MQA、不写;不存在的文件:判「打不开」");
    public static final Entry T10 = e("T10",
        "ORIGINALSAMPLERATE differs from the stream (96000 vs 352800): only that key is changed, official ENCODER / MQAENCODER kept",
        "ORIGINALSAMPLERATE 与码流不符(96000 ↔ 352800):只改这一个,官方的 ENCODER / MQAENCODER 原样留着");
    public static final Entry T11_1 = e("T11_1",
        "--dry-run: says it can be repaired and how; file byte-identical",
        "--dry-run:说可以修、怎么修,文件逐字节没动");
    public static final Entry T11_2 = e("T11_2",
        "--keep-backup: repaired, the original is kept unchanged as .bak",
        "--keep-backup:修好,原文件原样留在 .bak");
    public static final Entry T12_1 = e("T12_1",
        "the swap-in step fails (original opened by someone else): reported as failed, temporary file deleted, original byte-identical",
        "换入新文件那一步失败(原文件被别人开着):报失败,临时文件删掉,原文件逐字节没动");
    public static final Entry T12_2 = e("T12_2",
        "in-place write reads back wrong: reported as failed, the original bytes are written back, file byte-identical to before",
        "原地写完读回不对:报失败,把原来的字节写回去,文件逐字节与修之前相同");
    public static final Entry T12_3 = e("T12_3",
        "SHA-256 of the audio data differs before / after: failed (verify_failed), file byte-identical to before, no temporary file",
        "修之前、修之后音频数据的 SHA-256 对不上:报失败(verify_failed),文件逐字节与修之前相同,不留临时文件");
    public static final Entry T12_4 = e("T12_4",
        "MD5 of the samples decoded by FFmpeg differs before / after: the same",
        "修之前、修之后 FFmpeg 解出的样本 MD5 对不上:同上");
    public static final Entry T12_5 = e("T12_5",
        "disk write fails (disk full): failed (io, not verify_failed), file byte-identical to before, no temporary file",
        "写盘失败(磁盘满):报失败(io,不是 verify_failed),文件逐字节与修之前相同,不留临时文件");
    public static final Entry T12_6 = e("T12_6",
        "swap-in stops half-way and the new file can't get the original name: the repaired file is kept (the only copy), its path is reported",
        "换入做到一半、新文件怎么也改不成原名:修好的文件原样留着(它是仅有的一份,不删),结果里写明它在哪");
    public static final Entry T12_7 = e("T12_7",
        "swap-in stops half-way, renaming blocked twice: retried until it works, counts as repaired",
        "换入做到一半、改名头两次被挡住:多试几次改成了,算修好");
    public static final Entry T12_8 = e("T12_8",
        "with a backup, swap-in stops half-way and neither name can be restored: the original sits unchanged under the backup name, its path is reported; temporary file deleted",
        "留备份时换入做到一半、两个名字都改不回去:原文件原样在备份名下,结果里写明它在哪;临时文件删掉");
    public static final Entry T12_9 = e("T12_9",
        "with a backup, swap-in stops half-way and the new file can't come in: the original is renamed back, byte-identical, no backup and no temporary file left",
        "留备份时换入做到一半、新文件进不来:把原文件从备份名改回原名,文件逐字节没动,不留备份与临时文件");
    public static final Entry T12_10 = e("T12_10",
        "file system without ReplaceFile: falls back to rename-over, still repaired, creation time restored; with a backup, the backup is a byte-identical copy of the original",
        "文件系统不支持 ReplaceFile:退回改名覆盖,照样修好,建立时间改回原来的;留备份时备份是原文件的逐字节拷贝");
    public static final Entry T12_11 = e("T12_11",
        "file changed by another program between unlock and swap-in: not swapped in (the other change is not overwritten), temporary file deleted",
        "放掉独占锁之后、换入之前文件被别的程序改了:不换入(不把别人的改动盖掉),临时文件删掉");
    public static final Entry T12_12 = e("T12_12",
        "first swap-in blocked while the original is still there: checked unchanged, swapped again, repaired",
        "第一次换入被挡住、原文件还在:核对它没变之后再换一次,修好");
    public static final Entry T13 = e("T13",
        "48 kHz carrier (original 192 kHz) and 16-bit carrier (original 88.2 kHz): ORIGINALSAMPLERATE is the value in each stream",
        "48 kHz 载波(原始 192 kHz)与 16 bit 载波(原始 88.2 kHz):ORIGINALSAMPLERATE 写的是各自码流里的值");
    public static final Entry T14 = e("T14",
        "marker at 9.9 s is found, at 10.2 s it is not (only the first 10 seconds are examined); what is not found is not written",
        "同步在 9.9 秒处认得出,在 10.2 秒处不认(只看开头 10 秒);不认的不写");
    public static final Entry T14_2 = e("T14_2",
        "first record fails its check, a good one follows at 3 s: still found, the original rate comes from the later record (176.4 kHz)",
        "第一条记录的校验对不上、3 秒处还有一条好的:照样认出来,原始采样率读的是后面那条(176.4 kHz)");
    public static final Entry T15 = e("T15",
        "path longer than 260 characters, with Chinese: still repaired (whole-file rewrite and swap-in)",
        "路径超过 260 个字符、带中文:照样能修(这里走的是整文件重写 + 换入)");
    public static final Entry T16 = e("T16",
        "in-place writing disallowed (sector rule off): whole-file rewrite gives a file byte-identical to the in-place result of item 1",
        "不许原地改写时(扇区条件关掉):改走整文件重写,出来的文件与第 1 项原地改的逐字节相同");
    public static final Entry T17_1 = e("T17_1",
        "change crossing a sector boundary: whole-file rewrite and swap-in (file size unchanged); change inside one sector: written in place",
        "改动跨过扇区边界的:整文件重写后换入(文件大小不变);全在一个扇区里的:原地改写");
    public static final Entry T17_2 = e("T17_2",
        "file with the compression attribute: not written in place, whole-file rewrite and swap-in",
        "带压缩属性的文件:不原地改写,整文件重写后换入");
    public static final Entry T18 = e("T18",
        "existing .tagfix.tmp and similar files next to it are neither overwritten nor deleted; only leftovers of an interrupted run are removed (name and content match, nobody has them open)",
        "旁边已有的 .tagfix.tmp 等文件一个不盖、不删;只清掉上次被打断留下的(名字、内容都对得上,且没人开着)");
    public static final Entry T19_1 = e("T19_1",
        "--encoder-tag: the given value is written (an equals sign reads back unchanged); repairing again with the same rule changes nothing; the default rule judges by its own prefix",
        "--encoder-tag:写进去的是给的值(带等号也原样读回);同一规则再修不动;默认规则照自己的前缀判");
    public static final Entry T19_2 = e("T19_2",
        "--accept-prefix with an empty text: existing non-empty values are kept (ENCODER=Lavf… untouched), only empty and missing ones are written",
        "--accept-prefix 给空串:已有的非空值留着(ENCODER=Lavf… 不动),空值与缺的才写");
    public static final Entry T20_1 = e("T20_1",
        "one acceptable encoder key (the other absent / Lavf… / duplicated) plus an acceptable sample rate: \"tags complete\"; fix and --dry-run change nothing, files byte-identical",
        "只有一个编码器键合格(另一个不在 / 是 Lavf… / 重复)+ 采样率合格:判「标签完整」,fix 与 --dry-run 都不动,文件逐字节相同");
    public static final Entry T20_2 = e("T20_2",
        "both encoder keys unacceptable, or the sample rate wrong: \"can be repaired\"; the repair makes all three keys acceptable, an acceptable official ENCODER is kept",
        "两个编码器键都不合格、或采样率不对:判「可以修复」;修的时候三个键都补到合格,原来合格的官方 ENCODER 留着");
    public static final Entry T21_1 = e("T21_1",
        "cancel while scanning / locking / hashing / decoding (before anything is written): stops at once, no later phase entered, file byte-identical, no temporary file",
        "在扫描 / 加锁 / 算指纹 / 解码时取消(还没写任何东西):立刻停,后面的阶段一个都没进,文件逐字节没动,不留临时文件");
    public static final Entry T21_2 = e("T21_2",
        "cancel during the in-place write: that file is finished and verified, completely repaired",
        "原地写的那一下取消:这个文件写完并核对,完整修好");
    public static final Entry T21_3 = e("T21_3",
        "cancel while the temporary file is being written or verified: stops at once, temporary file deleted, original byte-identical",
        "在写临时文件 / 核对临时文件时取消:立刻停,临时文件删掉,原文件逐字节没动");
    public static final Entry T21_4 = e("T21_4",
        "cancel once the swap-in has begun: that file is finished, completely repaired, no temporary file",
        "换入已经开始之后取消:这个文件做完,完整修好,不留临时文件");
    public static final Entry T21_5 = e("T21_5",
        "cancel already requested before a file starts: nothing is opened for writing, file byte-identical",
        "开始之前就已经取消:不去开文件写,文件逐字节没动");
    public static final Entry T22_1 = e("T22_1",
        "MQA WAV, 16 and 24 bit, 44.1 and 48 kHz: scan says \"can be saved as a FLAC copy\"; --wav-to-flac writes <name>.flac with the same rate, channels and bit depth; the WAV is byte-identical",
        "带标记的 WAV,16 / 24 bit、44.1 / 48 kHz:扫描说「可以另存为 FLAC」;--wav-to-flac 写出 <文件名>.flac,采样率、声道数、位深相同;WAV 逐字节没动");
    public static final Entry T22_2 = e("T22_2",
        "the new FLAC: every sample equals the WAV's (compared here sample by sample from the raw bytes); it scans as MQA with complete tags; the MD5 in its header is that of the samples",
        "新的 FLAC:每个采样都等于 WAV 里的(这里从原始字节逐个比过);扫描是 MQA 标签完整;文件头里的 MD5 就是这些采样的");
    public static final Entry T22_3 = e("T22_3",
        "ordinary tags of the WAV (title, artist…) are carried over; the three tags are ours; no temporary file left",
        "WAV 里的普通标签(标题、艺人……)带过去;三个识别标签是我们写的;不留临时文件");
    public static final Entry T22_4 = e("T22_4",
        "target <name>.flac already exists: refused before anything is encoded, the existing file is byte-identical",
        "<文件名>.flac 已经存在:还没开始编码就拒绝,已有的文件逐字节没动");
    public static final Entry T22_5 = e("T22_5",
        "target appears while encoding: the final rename does not overwrite it; the existing file is byte-identical, temporary file deleted",
        "编码的时候目标名下冒出一个文件:最后改名那一步不覆盖它;已有的文件逐字节没动,临时文件删掉");
    public static final Entry T22_6 = e("T22_6",
        "a WAV without the marker is never converted; neither is a FLAC, a lossy WAV, or a file that can't be opened",
        "没有标记的 WAV 绝不转;FLAC、有损的 WAV、打不开的文件也不转");
    public static final Entry T22_7 = e("T22_7",
        "a deliberately corrupted copy (one byte of audio changed after writing) fails the check: deleted, nothing kept",
        "故意弄坏的拷贝(写完之后改掉音频里一个字节)核对不过:删掉,什么都不留");
    public static final Entry T22_8 = e("T22_8",
        "a copy with a wrong tag, or with one sample changed and a matching frame checksum, fails the check too: deleted",
        "标签不对的拷贝、改了一个采样而且帧校验对得上的拷贝,同样核对不过:删掉");
    public static final Entry T22_9 = e("T22_9",
        "cancel while encoding: stops at once, the check is never started, no new file and no temporary file; cancel during the check: the same",
        "编码的时候取消:立刻停,不进核对那一步,不留新文件、不留临时文件;核对的时候取消:一样");
    public static final Entry T22_10 = e("T22_10",
        "--dry-run: says what would be saved, creates nothing; the original in use by a writer: refused",
        "--dry-run:说会另存什么,不新建任何文件;原文件正被别的程序写着:拒绝");
    public static final Entry T23_1 = e("T23_1",
        "16-bit FLAC to a 24-bit copy: <name>.24bit.flac is 24 bit; each sample is the 16-bit sample shifted left by 8, the low 8 bits all zero (compared here sample by sample); the original is byte-identical",
        "16 bit 的 FLAC 另存 24 bit:<文件名>.24bit.flac 是 24 bit;每个采样 = 16 bit 的采样左移 8 位,低 8 位全是零(这里逐个比过);原文件逐字节没动");
    public static final Entry T23_2 = e("T23_2",
        "the 24-bit copy still contains the marker with the same original rate, scans as complete, and keeps the original's comments and picture",
        "24 bit 的拷贝里标记还在、原始采样率相同,扫描是标签完整;原文件的注释与图片带过去");
    public static final Entry T23_3 = e("T23_3",
        "16-bit WAV to a 24-bit copy: the same; a 24-bit file is not copied; existing <name>.24bit.flac: refused",
        "16 bit 的 WAV 另存 24 bit:同样;24 bit 的文件不另存;<文件名>.24bit.flac 已经存在:拒绝");
    public static final Entry T23_4 = e("T23_4",
        "a 16-bit FLAC with complete tags, and one whose tags were never repaired, both get a copy with complete tags; the originals stay as they were",
        "标签完整的 16 bit FLAC、标签没修过的 16 bit FLAC,另存出来的拷贝标签都是完整的;原文件各自保持原样");

    // ---------------------------------------------------------------------------
    // 安卓版自己的(Windows 版里没有的说法:没有 FFmpeg、文件经系统的文件选择器给进来)
    // ---------------------------------------------------------------------------
    public static final Entry AOnlyFlacWav = e("AOnlyFlacWav",
        "the Android version reads FLAC and WAV files only",
        "安卓版只读 FLAC 和 WAV 文件");
    public static final Entry AWavUnsupported = e("AWavUnsupported",
        "this kind of WAV file can't be read ({0})",
        "这种 WAV 读不了({0})");
    public static final Entry AVerNoMarker = e("AVerNoMarker",
        "the read-back check failed: the marker is no longer found",
        "读回:认不出 MQA 了");
    public static final Entry AVerPcm = e("AVerPcm",
        "the read-back check failed: the decoded samples differ from before",
        "读回:解出的样本与修之前不同");
    public static final Entry AVerWhole = e("AVerWhole",
        "the read-back check failed: the file differs from the checked new version",
        "写回之后的文件与核对过的新版本不一致");
    public static final Entry AFixNoWrite = e("AFixNoWrite",
        "no permission to write this file; the file is as it was",
        "没有写这个文件的权限,没有改动");
    public static final Entry AFixChanged = e("AFixChanged",
        "the file was changed meanwhile; the file is as it was",
        "文件在写回之前被改动了,没有改动");
    public static final Entry AFixOverwriteRetried = e("AFixOverwriteRetried",
        "writing the repaired version back failed ({0}); the repaired and checked file is kept by this app and will be written back the next time it starts",
        "把修好的版本写回去没成功({0});修好并核对过的文件由本应用留着,下次打开本应用时会再写回去");
    public static final Entry AWriteBackDone = e("AWriteBackDone",
        "a repair that was interrupted last time has been finished: {0}",
        "上次被打断的修复已经写完:{0}");
    public static final Entry AWriteBackFailed = e("AWriteBackFailed",
        "a repair that was interrupted last time could not be finished: {0} ({1})",
        "上次被打断的修复没能写完:{0}({1})");
    public static final Entry ACopyNoFolder = e("ACopyNoFolder",
        "no folder was chosen for the new files; nothing was written",
        "没有选择新文件放在哪个文件夹;什么都没有写");
    public static final Entry ACopyCreateFailed = e("ACopyCreateFailed",
        "the new file can't be created in the chosen folder; nothing was kept",
        "在选的文件夹里建不了新文件({0});没有留下新文件");

    // ---- 安卓版的界面 ----
    public static final Entry AUiPickFiles = e("AUiPickFiles", "Choose files…", "选文件……");
    public static final Entry AUiPickFolder = e("AUiPickFolder", "Choose a folder…", "选文件夹……");
    public static final Entry AUiNotice = e("AUiNotice", "Notice", "声明");
    public static final Entry AUiLicense = e("AUiLicense", "License", "许可");
    public static final Entry AUiCancel = e("AUiCancel", "Cancel", "取消");
    public static final Entry AUiStart = e("AUiStart", "Start", "开始");
    public static final Entry AUiNo = e("AUiNo", "No", "不做");
    public static final Entry AUiClose = e("AUiClose", "Close", "关闭");
    public static final Entry AUiRepairBtn = e("AUiRepairBtn", "Repair {0} file(s) in place", "原地修复 {0} 个文件");
    public static final Entry AUiWavBtn = e("AUiWavBtn", "Save {0} WAV file(s) as new FLAC files", "把 {0} 个 WAV 另存为 FLAC");
    public static final Entry AUi24Btn = e("AUi24Btn", "Save 24-bit copies of {0} file(s)", "给 {0} 个文件另存 24 bit 的拷贝");
    public static final Entry AUiChooseOutFolder = e("AUiChooseOutFolder",
        "Choose the folder that holds the original file(s): the new file is written next to the original (an existing file is never overwritten)",
        "请选原文件所在的那个文件夹:新文件写在原文件旁边(已有的文件从不覆盖)");
    public static final Entry AUiHow = e("AUiHow",
        "Choose music files or a whole folder (subfolders included). It scans first and lists what it found; it changes or creates a file only after you tap Start.",
        "选音乐文件,或者整个文件夹(连里面的子文件夹)。它先检查,把结果列出来;你点「开始」之后才动手。");
    public static final Entry AUiPlayNote = e("AUiPlayNote",
        "It only repairs the MQA tags in a file. Whether a device plays the file as MQA depends on that device itself (decoder / DAC).",
        "它只修复文件里的 MQA 标签。本机能不能按 MQA 播放,取决于设备本身(解码器 / DAC)。");
    public static final Entry AUiAndroidNote7 = e("AUiAndroidNote7",
        "Android version: item 7 of the notice does not apply — no FFmpeg is loaded; reading, decoding and encoding use this tool's own Java code.",
        "安卓版:声明第七条不适用 —— 不加载 FFmpeg,读文件、解码、编码用的是本工具自己写的 Java 代码。");
    public static final Entry AUiKeepOpen = e("AUiKeepOpen",
        "Working — please keep this screen open. Cancel stops after the file at hand is finished or put back.",
        "正在处理 —— 请停在这个界面。点取消会在手上这个文件写完或者恢复原样之后停下。");
    public static final Entry AUiSelfTest = e("AUiSelfTest", "Self-test: {0} passed, {1} failed", "自检:通过 {0} 项,没通过 {1} 项");
    public static final Entry AUiSaveRepaired = e("AUiSaveRepaired", "Save the repaired file…", "另存修好的文件……");
    public static final Entry AUiLater = e("AUiLater", "Later", "以后再说");
    public static final Entry AUiNoFiles = e("AUiNoFiles", "No audio files were found.", "没有找到音频文件。");
    public static final Entry AUiSavedTo = e("AUiSavedTo", "the repaired file was saved: {0}", "修好的文件已另存:{0}");
    public static final Entry AModeRewriteBack = e("AModeRewriteBack", "whole file rewritten and checked, then written back", "整文件重写、核对后写回");
    public static final Entry AMetaTooBig = e("AMetaTooBig", "the metadata area is larger than 64 MB; the Android version does not handle it", "元数据区超过 64 MB,安卓版不处理");
    public static final Entry AUiBusy = e("AUiBusy", "Another job is still running in this app; try again when it has finished.", "本应用里还有一件事在做,做完再来。");
    public static final Entry AUiResuming = e("AUiResuming", "Finishing a repair that was interrupted last time…", "正在把上次被打断的修复写完……");
}
