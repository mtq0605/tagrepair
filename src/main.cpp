// =============================================================================
// tagrepair —— 标签修复(独立程序)
// =============================================================================
// 有的 MQA 编码的 FLAC 文件在下载、转存、改标签时把识别用的三个标签(ENCODER / MQAENCODER / ORIGINALSAMPLERATE)弄丢了:
// 音频数据没变(里面的同步标记还在),只看标签的程序和设备就把它当成普通 FLAC。
// 这个程序在音频数据里找同步标记,找到了就把缺的标签补上(值是我们自己的,不冒充官方编码器);音频数据一个字节都不动。
// 同样情况的 WAV(以及 16 bit 的文件,有的设备只认 24 bit):明说了才做 —— 在旁边另存一个带标签的 FLAC,原文件不改(copy_flac.h)。
//
// 不用命令行:把文件或文件夹拖到 tagrepair.exe 上(或者双击它看说明)—— interactive.cpp。
//
// 命令行:
//   tagrepair info [--json]                                   我是谁、哪一版、协议号、认哪些命令(不需要 FFmpeg)
//   tagrepair scan <文件或文件夹>… [--recursive] [--json]
//   tagrepair fix  <文件或文件夹>… [--recursive] [--dry-run] [--keep-backup] [--json]
//       [--wav-to-flac]            带标记的 WAV:在旁边另存 <文件名>.flac
//       [--copy-24bit]             带标记的 16 bit 文件:在旁边另存 <文件名>.24bit.flac
//       [--copy-only]              只做上面选的另存,不原地修
//       [--encoder-tag "<值>"]     写进 ENCODER / MQAENCODER 的值(默认见 tag_rule.h 的 kDefaultEncoderTag)
//       [--accept-prefix "<文字>"]  已有的值以它开头就算合格、留着不动(默认 kDefaultAcceptPrefix;给空串 = 不是空值就合格)
//     后两个选项 scan 也认(同一套规则判"完不完整")。
//   tagrepair selftest [--keep]
//   scan / fix / selftest 都认 --ffmpeg-dir <文件夹>:FFmpeg 的四个运行库在哪(不给就按 probe.h 里写的顺序找)
//
// --json:每个文件一行 JSON(UTF-8),最后一行汇总 —— 给别的程序读的(README「宿主协议」);不加就是给人看的文字,
// 一律「English (中文)」,每一句都在 messages.h 里。
// 退出码:0 正常;1 有文件没做成(拒绝 / 失败)或自检没过;2 用法不对;3 找不到 FFmpeg 运行库;4 被取消(Ctrl+C)。
// =============================================================================
#include "cli.h"
#include "json_out.h"
#include "runctl.h"
#include "version.h"
#include "win_util.h"

#include <string>
#include <vector>

int runSelftest(bool keepFiles);                              // selftest.cpp

using namespace tagrepair;

namespace {

// 命令行的宽字符 → UTF-8;落单的代理项(转不成合法的 UTF-8)算失败
bool toUtf8Strict(const std::wstring& w, std::string& out) {
    out.clear();
    if (w.empty()) return true;
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    out.resize((size_t)n);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), (int)w.size(), &out[0], n, nullptr, nullptr) == n;
}

// 命令的写法是数据(两种语言一样),各自的说明在 messages.h
int usage() {
    const std::wstring pad(6, L' ');
    wu::outLine(L"tagrepair — " + msg::of(msg::Title).both());
    cli::sayBlank();
    cli::say(msg::of(msg::UseNoCommandLine), 2);
    cli::say(msg::of(msg::IntroCancel), 2);
    cli::sayBlank();
    wu::outLine(L"  tagrepair scan <path>... [--recursive] [--json]");
    cli::say(msg::of(msg::UseScan), 6);
    wu::outLine(L"  tagrepair fix <path>... [--recursive] [--dry-run] [--keep-backup] [--json]");
    wu::outLine(L"                [--wav-to-flac] [--copy-24bit] [--copy-only]");
    wu::outLine(L"                [--encoder-tag \"<value>\"] [--accept-prefix \"<text>\"]");
    cli::say(msg::of(msg::UseFix), 6);
    cli::say(msg::of(msg::UseDryRun), 6);
    cli::say(msg::of(msg::UseWavToFlac), 6);
    cli::say(msg::of(msg::UseCopy24), 6);
    cli::say(msg::of(msg::UseCopyOnly), 6);
    cli::say(msg::of(msg::UseCopyRules), 6);
    cli::say(msg::of(msg::UseEncoderTag, { wu::wide(kDefaultEncoderTag), wu::wide(kWriterMark), wu::wide(kUnsignedMark) }), 6);
    cli::say(msg::of(msg::UseAcceptPrefix, { wu::wide(kDefaultAcceptPrefix) }), 6);
    wu::outLine(L"  tagrepair selftest");
    cli::say(msg::of(msg::UseSelftest), 6);
    wu::outLine(L"  tagrepair info [--json]");
    cli::say(msg::of(msg::UseInfo), 6);
    cli::sayBlank();
    cli::say(msg::of(msg::UseJson), 2);
    cli::say(msg::of(msg::UseFfmpegDir, { std::wstring(probe::ffmpegDllName(0)) }), 2);
    cli::say(msg::of(msg::UseFfmpegOrder), 6);
    cli::say(msg::of(msg::UseExitCodes), 2);
    return cli::kExitUsage;
}

void emit(jsonout::Writer& w) { wu::outLineUtf8(w.end().text()); }

bool existsOnDisk(const std::wstring& p) { return GetFileAttributesW(wu::longPath(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

void copyTallyJson(jsonout::Writer& w, const char* key, const cli::CopyTally& t) {
    w.beginObj(key);
    w.key("created").val(t.n[(int)CopyOutcome::Created]).key("wouldCreate").val(t.n[(int)CopyOutcome::WouldCreate]);
    w.key("skipped").val(t.n[(int)CopyOutcome::Skipped]).key("refused").val(t.n[(int)CopyOutcome::Refused]);
    w.key("failed").val(t.n[(int)CopyOutcome::Failed]).key("cancelled").val(t.n[(int)CopyOutcome::Cancelled]).key("notStarted").val(t.notStarted);
    w.endObj();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // 没给命令:双击启动(没有参数),或者把文件 / 文件夹拖到了程序上(参数全是路径)—— 在窗口里一问一答。
    // 第一个参数不是命令、也不是磁盘上有的东西:当成打错了,照旧给用法
    if (argc < 2) return cli::runInteractive({}, std::wstring());
    const std::wstring cmd = argv[1];
    const bool isScan = cmd == L"scan", isFix = cmd == L"fix", isSelftest = cmd == L"selftest", isInfo = cmd == L"info";
    const bool isHelp = cmd == L"--help" || cmd == L"-h" || cmd == L"/?";
    if (!isScan && !isFix && !isSelftest && !isInfo && !isHelp && cmd.compare(0, 2, L"--") != 0 && existsOnDisk(cmd)) {
        std::vector<std::wstring> paths;
        for (int i = 1; i < argc; ++i) paths.push_back(argv[i]);
        return cli::runInteractive(paths, std::wstring());
    }

    bool recursive = false, json = false, keep = false, wavToFlac = false, copy24 = false, copyOnly = false;
    FixOptions fo;
    msg::Text optError;                                       // 选项的值不能用:原因
    std::wstring ffmpegDir;
    std::vector<std::wstring> inputs;
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--recursive") recursive = true;
        else if (a == L"--json") json = true;
        else if (a == L"--dry-run") fo.dryRun = true;
        else if (a == L"--keep-backup") fo.keepBackup = true;
        else if (a == L"--keep") keep = true;
        else if (a == L"--wav-to-flac") wavToFlac = true;
        else if (a == L"--copy-24bit") copy24 = true;
        else if (a == L"--copy-only") copyOnly = true;
        else if (a == L"--ffmpeg-dir") {
            if (i + 1 >= argc) return usage();
            ffmpegDir = argv[++i];
            if (ffmpegDir.empty()) return usage();
        }
        else if (a == L"--encoder-tag" || a == L"--accept-prefix") {
            if (i + 1 >= argc) return usage();
            const std::wstring v = argv[++i];
            std::string u;
            const bool utf = toUtf8Strict(v, u);
            const bool isTag = a == L"--encoder-tag";
            const msg::Entry* why = !utf ? &msg::OptionNotText : isTag ? encoderTagProblem(u) : acceptPrefixProblem(u);
            if (why) optError = msg::of(msg::OptionBad, { a, msg::of(*why) });
            else if (isTag) fo.rule.encoderTag = u;
            else fo.rule.acceptPrefix = u;
        }
        else if (a.compare(0, 2, L"--") == 0) return usage();
        else inputs.push_back(a);
    }
    if (isHelp) { usage(); return cli::kExitOk; }
    if (!isScan && !isFix && !isSelftest && !isInfo) return usage();
    if (optError.empty() && isFix && copyOnly && !wavToFlac && !copy24) optError = msg::of(msg::OptionCopyOnlyAlone);
    if (optError.empty() && isFix && copyOnly && fo.keepBackup) optError = msg::of(msg::OptionBackupNoCopy);

    if (!optError.empty()) {
        if (json) {
            jsonout::Writer w;
            w.begin().key("type").val("error").key("code").val("bad_option").key("text").val(wu::utf8(optError.zh));
            w.key("textEn").val(wu::utf8(optError.en));
            emit(w);
        } else {
            cli::say(optError);
        }
        return cli::kExitUsage;
    }
    if (isInfo) {                                             // 不碰 FFmpeg:宿主拿它判断"这个程序在不在、是不是我认识的那一个"
        const std::vector<std::string> commands = { "info", "scan", "fix", "selftest" };
        if (json) {
            std::vector<std::string> dlls;
            for (int i = 0; i < probe::kFfmpegDlls; ++i) dlls.push_back(wu::utf8(probe::ffmpegDllName(i)));
            jsonout::Writer w;
            w.begin().key("type").val("info").key("id").val(kHostId).key("name").val(kToolName).key("version").val(kToolVersion);
            w.key("protocol").val(kProtocol).key("commands").arrOfStr(commands).key("ffmpeg").arrOfStr(dlls);
            emit(w);
        } else {
            cli::say(msg::of(msg::InfoLine, { wu::wide(kToolName), wu::wide(kToolVersion), wu::wide(kHostId), msg::num(kProtocol) }));
        }
        return cli::kExitOk;
    }
    std::vector<std::wstring> tried;
    if (!probe::loadFfmpeg(ffmpegDir, &tried)) {
        const msg::Text why = cli::noFfmpegText(tried);
        if (json) {
            jsonout::Writer w;
            w.begin().key("type").val("error").key("code").val("no_ffmpeg").key("text").val(wu::utf8(why.zh));
            w.key("textEn").val(wu::utf8(why.en));
            emit(w);
        } else {
            cli::say(why);
        }
        return cli::kExitNoFfmpeg;
    }
    if (isSelftest) return runSelftest(keep);
    if (inputs.empty()) return usage();

    runctl::install();                                        // 从这里起 Ctrl+C / 关窗口 = 取消(停到安全的地方),不是直接结束进程
    const std::vector<std::wstring> files = cli::expandInputs(inputs, recursive);
    const int total = (int)files.size();

    if (isScan) {
        int nState[kStates] = { 0 };
        cli::Progress bar(msg::PgScan, files.size(), !json);
        size_t i = 0;
        for (; i < files.size(); ++i) {
            runctl::testPause();
            if (runctl::cancelRequested()) break;
            const std::wstring shown = wu::shownPath(files[i]);
            bar.begin(i, shown);
            Scan s;
            scanFile(files[i], s, fo.rule);
            ++nState[(int)s.state];
            if (json) cli::jsonScan(shown, s);
            else cli::printScan(i, files.size(), shown, s);
        }
        bar.end();
        const int scanned = (int)i;
        const bool cancelled = runctl::cancelRequested() && scanned < total;      // 全部查完之后才按的不算
        const int complete = nState[(int)State::Complete], rep = nState[(int)State::Repairable], conv = nState[(int)State::Convertible];
        const int cant = nState[(int)State::RateUnknown] + nState[(int)State::Unsupported] + nState[(int)State::Damaged];
        if (json) {
            jsonout::Writer w;
            w.begin().key("type").val("summary").key("files").val(total).key("complete").val(complete).key("repairable").val(rep);
            w.key("unrepairable").val(cant).key("noMarker").val(nState[(int)State::NoMarker]).key("notLossless").val(nState[(int)State::NotLossless]);
            w.key("error").val(nState[(int)State::Error]).key("convertible").val(conv).key("notScanned").val(total - scanned);
            emit(w);
        } else {
            cli::sayBlank();
            cli::say(msg::of(msg::SumScan, { msg::num(scanned), msg::num(complete), msg::num(rep), msg::num(conv), msg::num(cant),
                                             msg::num(nState[(int)State::NoMarker]), msg::num(nState[(int)State::NotLossless]),
                                             msg::num(nState[(int)State::Error]) }));
            if (cancelled) cli::say(msg::of(msg::CancelScan, { msg::num(scanned), msg::num(total) }));
        }
        return cancelled ? cli::kExitCancelled : cli::kExitOk;
    }

    // fix:先原地修(除非 --copy-only),再做明说了的另存。三样各自把整批文件过一遍
    cli::RepairTally rt;
    cli::CopyTally wt, ct;
    if (!copyOnly) rt = cli::repairBatch(files, fo, json);
    CopyOptions co;
    co.dryRun = fo.dryRun;
    co.rule = fo.rule;
    if (wavToFlac) wt = cli::copyBatch(files, CopyKind::WavToFlac, co, json);
    if (copy24) ct = cli::copyBatch(files, CopyKind::To24Bit, co, json);
    // 被取消 = 有文件没有开始、或者做到一半停下了(全部做完之后才按的不算)
    const bool cancelled = rt.notStarted || rt.n[(int)Outcome::Cancelled] || wt.notStarted || wt.n[(int)CopyOutcome::Cancelled] || ct.notStarted ||
                           ct.n[(int)CopyOutcome::Cancelled];
    const int notDone = rt.n[(int)Outcome::Refused] + rt.n[(int)Outcome::Failed] + wt.n[(int)CopyOutcome::Refused] + wt.n[(int)CopyOutcome::Failed] +
                        ct.n[(int)CopyOutcome::Refused] + ct.n[(int)CopyOutcome::Failed];
    if (json) {
        jsonout::Writer w;
        w.begin().key("type").val("summary").key("files").val(total).key("fixed").val(rt.n[(int)Outcome::Fixed]);
        w.key("wouldFix").val(rt.n[(int)Outcome::WouldFix]).key("unchanged").val(rt.n[(int)Outcome::Unchanged]);
        w.key("skipped").val(rt.n[(int)Outcome::Skipped]).key("refused").val(rt.n[(int)Outcome::Refused]).key("failed").val(rt.n[(int)Outcome::Failed]);
        w.key("cancelled").val(rt.n[(int)Outcome::Cancelled]).key("notStarted").val(copyOnly ? 0 : rt.notStarted);
        if (wavToFlac) copyTallyJson(w, copyKindCode(CopyKind::WavToFlac), wt);
        if (copy24) copyTallyJson(w, copyKindCode(CopyKind::To24Bit), ct);
        emit(w);
    } else {
        cli::sayBlank();
        if (!copyOnly) {
            cli::say(msg::of(fo.dryRun ? msg::SumFixDry : msg::SumFix,
                             { msg::num(total), msg::num(fo.dryRun ? rt.n[(int)Outcome::WouldFix] : rt.n[(int)Outcome::Fixed]),
                               msg::num(rt.n[(int)Outcome::Unchanged]), msg::num(rt.n[(int)Outcome::Skipped]), msg::num(rt.n[(int)Outcome::Refused]),
                               msg::num(rt.n[(int)Outcome::Failed]) }));
            if (rt.cancelled && (rt.notStarted || rt.n[(int)Outcome::Cancelled]))
                cli::say(cli::cancelText(msg::CancelRepair, rt.n[(int)Outcome::Fixed], total,
                                         rt.n[(int)Outcome::Unchanged] + rt.n[(int)Outcome::Skipped],
                                         rt.n[(int)Outcome::Refused] + rt.n[(int)Outcome::Failed], rt.n[(int)Outcome::Cancelled], rt.notStarted));
        }
        const auto copySummary = [&](const msg::Entry& kind, const cli::CopyTally& t, bool asked) {
            if (!asked) return;
            cli::say(msg::of(fo.dryRun ? msg::SumCopyDry : msg::SumCopy,
                             { msg::of(kind), msg::num(fo.dryRun ? t.n[(int)CopyOutcome::WouldCreate] : t.n[(int)CopyOutcome::Created]),
                               msg::num(t.n[(int)CopyOutcome::Skipped]), msg::num(t.n[(int)CopyOutcome::Refused]), msg::num(t.n[(int)CopyOutcome::Failed]) }));
            if (t.cancelled && (t.notStarted || t.n[(int)CopyOutcome::Cancelled]))
                cli::say(cli::cancelText(msg::CancelSave, t.n[(int)CopyOutcome::Created], t.total,
                                         t.n[(int)CopyOutcome::Skipped],
                                         t.n[(int)CopyOutcome::Refused] + t.n[(int)CopyOutcome::Failed], t.n[(int)CopyOutcome::Cancelled], t.notStarted));
        };
        copySummary(msg::KindWavToFlac, wt, wavToFlac);
        copySummary(msg::KindCopy24, ct, copy24);
    }
    return cancelled ? cli::kExitCancelled : notDone ? cli::kExitNotDone : cli::kExitOk;
}
