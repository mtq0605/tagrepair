// =============================================================================
// 不用命令行的用法:双击 tagrepair.exe,或者把音乐文件 / 文件夹拖到它上面
// =============================================================================
// Windows 把拖上来的东西当成参数交给程序(全是路径,没有命令);双击就是一个参数都没有。这两种情况走这里:
//   没有路径 —— 说明自己是什么、怎么用,等一下回车;
//   有路径   —— 先只读地检查(文件夹连子文件夹),把结果分开列出来:
//                 可以原地修复的 / WAV:可以另存为 FLAC 的 / 16 bit:可以另存一个 24 bit 拷贝的;
//              然后按这个顺序一样问一次,各有各的 Y / N。回答 Y:马上说"开始……",接着是进度;
//              别的回答(包括直接回车、输入结束):说一句没有做、什么都没有改(前面的问题已经做过事的,说"别的什么都没有改")。
//              没有对应的那个 Y,不写、也不新建任何东西。
// 做事的函数与命令行完全相同(cli::repairBatch / copyBatch,默认选项)。
// 随时可以按 Ctrl+C 取消(runctl.h):一开始就说明;取消之后说清楚做了多少、没做多少。
// 窗口是我们自己的(双击 / 拖放开出来的)时,结束前等一下回车,不然窗口一闪就没了;从已经开着的命令行窗口里启动的不等。
// =============================================================================
#include "cli.h"
#include "runctl.h"
#include "version.h"
#include "win_util.h"

#include <string>
#include <vector>

using namespace tagrepair;

namespace {

// 这个控制台窗口是不是只有我们一个程序在用(双击 / 拖放开出来的窗口:程序一结束窗口就关)
bool ownConsole() {
    DWORD ids[4];
    return wu::stdinIsConsole() && GetConsoleProcessList(ids, 4) <= 1;
}

// 读一行(控制台按宽字符读;被接了管道 / 重定向时按 UTF-8 读到换行)。读不到(没有输入了、读的时候按了 Ctrl+C)返回 false
bool readLine(std::wstring& out) {
    out.clear();
    const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (wu::stdinIsConsole()) {
        wchar_t buf[256];
        DWORD n = 0;
        SetLastError(0);
        const BOOL ok = ReadConsoleW(h, buf, 255, &n, nullptr);
        if (!ok || n == 0) {
            // 读的时候按了 Ctrl+C:读立刻返回,处理函数在另一个线程上立标志 —— 等它一下,调用方好分清"取消"和"没回答"
            if (GetLastError() == ERROR_OPERATION_ABORTED)
                for (int i = 0; i < 50 && !runctl::cancelRequested(); ++i) Sleep(10);
            return false;
        }
        out.assign(buf, n);
    } else {
        std::string s;
        char c = 0;
        DWORD n = 0;
        bool any = false;
        while (ReadFile(h, &c, 1, &n, nullptr) && n == 1) {
            any = true;
            if (c == '\n') break;
            s.push_back(c);
        }
        if (!any) return false;
        out = wu::wide(s);
    }
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return true;
}

// 回答算不算"是":第一个不是空白的字是 Y / y(全角的也算)。别的一律不算 —— 没有明说 Y 就不动手
bool saidYes(const std::wstring& answer) {
    for (wchar_t c : answer) {
        if (c == L' ' || c == L'\t' || c == 0x3000) continue;
        return c == L'Y' || c == L'y' || c == 0xFF39 || c == 0xFF59;
    }
    return false;
}

void waitEnter() {
    if (!ownConsole()) return;
    cli::sayBlank();
    cli::say(msg::of(msg::PressEnter));
    std::wstring line;
    readLine(line);
}

int finish(int code) {
    waitEnter();
    return code;
}

void intro(bool withCancelHint) {
    const msg::Text title(L"tagrepair " + wu::wide(kToolVersion) + L" — " + msg::Title.en, msg::Title.zh);
    cli::say(title);
    cli::say(msg::of(msg::IntroWhat));
    if (withCancelHint) cli::say(msg::of(msg::IntroCancel));
    cli::sayBlank();
}

enum class Answer { Yes, No, Cancelled };

// 问一次:先说明这件事做什么,再问。只有明说 Y 才算是
Answer ask(const msg::Text& explain, const msg::Text& question) {
    cli::sayBlank();
    cli::say(explain);
    cli::say(question);
    std::wstring answer;
    const bool got = readLine(answer);
    if (runctl::cancelRequested()) return Answer::Cancelled;
    return got && saidYes(answer) ? Answer::Yes : Answer::No;
}

struct CopyItem {
    std::wstring path;
    std::wstring targetName;      // 新文件的名字
};

void listCopies(const std::vector<CopyItem>& todo, const std::vector<CopyItem>& have) {
    for (const CopyItem& c : todo) wu::outLine(L"      " + wu::shownPath(c.path) + L"  ->  " + c.targetName);
    for (const CopyItem& c : have) wu::outLine(L"      " + wu::shownPath(c.path) + L"  [" + msg::of(msg::AskAlreadyThere, { c.targetName }).both() + L"]");
}

std::vector<std::wstring> pathsOf(const std::vector<CopyItem>& items) {
    std::vector<std::wstring> v;
    for (const CopyItem& c : items) v.push_back(c.path);
    return v;
}

} // namespace

namespace cli {

int runInteractive(const std::vector<std::wstring>& paths, const std::wstring& ffmpegDir) {
    runctl::install();
    intro(!paths.empty());
    if (paths.empty()) {
        say(msg::of(msg::HowDrop1));
        say(msg::of(msg::HowDrop2), 6);
        say(msg::of(msg::HowDrop3), 6);
        say(msg::of(msg::HowDrop4), 6);
        return finish(kExitUsage);
    }
    std::vector<std::wstring> tried;
    if (!probe::loadFfmpeg(ffmpegDir, &tried)) {
        say(noFfmpegText(tried));
        return finish(kExitNoFfmpeg);
    }

    // ---- 检查(只读)----
    const std::vector<std::wstring> files = expandInputs(paths, true);
    std::vector<std::wstring> repairable, cantLines;
    std::vector<CopyItem> wavTodo, wavHave, c24Todo, c24Have;
    int complete = 0, noMarker = 0;
    say(msg::of(msg::AskScanning, { msg::num((long long)files.size()) }));
    size_t scanned = 0;
    {
        Progress bar(msg::PgScan, files.size());
        for (; scanned < files.size(); ++scanned) {
            runctl::testPause();
            if (runctl::cancelRequested()) break;
            const std::wstring& f = files[scanned];
            bar.begin(scanned, wu::shownPath(f));
            if (!bar.drawing()) wu::outLine(fileHeader(scanned, files.size(), wu::shownPath(f)));      // 输出被接走了:每个文件一行
            Scan s;
            scanFile(f, s);
            switch (s.state) {
            case State::Complete:    ++complete; break;
            case State::Repairable:  repairable.push_back(f); break;
            case State::Convertible: break;                    // 列在 WAV 那一栏
            case State::NoMarker:
            case State::NotLossless: ++noMarker; break;
            default:                 cantLines.push_back(wu::shownPath(f) + L" — " + stateMsg(s).both()); break;
            }
            // 另存:目标名下已经有文件的不再问(从不覆盖),单独标出来
            const auto sort = [&](CopyKind kind, std::vector<CopyItem>& todo, std::vector<CopyItem>& have) {
                const std::wstring target = copyTargetPath(f, kind);
                (fileutil::exists(target) ? have : todo).push_back({ f, wu::fileName(target) });
            };
            if (s.canWavToFlac) sort(CopyKind::WavToFlac, wavTodo, wavHave);
            if (s.canCopy24) sort(CopyKind::To24Bit, c24Todo, c24Have);
        }
    }
    if (runctl::cancelRequested() && scanned < files.size()) {
        say(msg::of(msg::CancelScan, { msg::num((long long)scanned), msg::num((long long)files.size()) }));
        return finish(kExitCancelled);
    }
    say(msg::of(msg::AskScanned, { msg::num((long long)files.size()) }));
    say(msg::of(msg::AskComplete, { msg::num(complete) }), 2);
    say(msg::of(msg::AskRepairable, { msg::num((long long)repairable.size()) }), 2);
    for (const std::wstring& f : repairable) wu::outLine(L"      " + wu::shownPath(f));
    say(msg::of(msg::AskWav, { msg::num((long long)(wavTodo.size() + wavHave.size())) }), 2);
    listCopies(wavTodo, wavHave);
    say(msg::of(msg::Ask16, { msg::num((long long)(c24Todo.size() + c24Have.size())) }), 2);
    listCopies(c24Todo, c24Have);
    say(msg::of(msg::AskNoMarker, { msg::num(noMarker) }), 2);
    say(msg::of(msg::AskCannot, { msg::num((long long)cantLines.size()) }), 2);
    for (const std::wstring& l : cantLines) wu::outLine(L"      " + l);
    if (repairable.empty() && wavTodo.empty() && c24Todo.empty()) {
        sayBlank();
        say(msg::of(msg::AskNothingToDo));
        return finish(kExitOk);
    }

    // ---- 一样问一次,各有各的 Y;回答 Y 立刻说开始,然后做 ----
    // changedSoFar:前面的问题已经修过 / 存过文件。之后再说"没有做"、"已取消"时不能说"什么都没有改",要说"别的什么都没有改"
    int notDone = 0;
    bool changedSoFar = false;
    const auto cancelledAtPrompt = [&]() {
        sayBlank();
        say(msg::of(changedSoFar ? msg::CancelAtPromptAfter : msg::CancelAtPrompt));
        return finish(kExitCancelled);
    };
    if (!repairable.empty()) {
        const msg::Text count = msg::num((long long)repairable.size());
        const Answer a = ask(msg::of(msg::AskRepairExplain), msg::of(msg::AskRepair, { count }));
        if (a == Answer::Cancelled) return cancelledAtPrompt();
        if (a == Answer::Yes) {
            say(msg::of(msg::StartRepair, { count }));
            const RepairTally t = repairBatch(repairable, FixOptions(), false);
            const int fixed = t.n[(int)Outcome::Fixed], unchanged = t.n[(int)Outcome::Unchanged];
            const int failed = t.n[(int)Outcome::Skipped] + t.n[(int)Outcome::Refused] + t.n[(int)Outcome::Failed];
            changedSoFar = changedSoFar || fixed > 0;
            sayBlank();
            if (t.notStarted || t.n[(int)Outcome::Cancelled]) {
                say(cancelText(msg::CancelRepair, fixed, t.total, unchanged, failed, t.n[(int)Outcome::Cancelled], t.notStarted));
                return finish(kExitCancelled);
            }
            say(msg::of(msg::DoneRepair, { msg::num(fixed), msg::num(unchanged), msg::num(failed) }) + (failed ? msg::of(msg::DoneSeeAbove) : msg::Text()));
            notDone += failed;
        } else {
            say(msg::of(msg::SkipRepair));
        }
    }
    const auto copyStep = [&](const std::vector<CopyItem>& todo, CopyKind kind, const msg::Entry& explain, const msg::Entry& question, const msg::Entry& start,
                              const msg::Entry& skipped, const msg::Entry& skippedAfter) -> int {      // 返回 −1 = 接着往下;否则是退出码
        if (todo.empty()) return -1;
        const msg::Text count = msg::num((long long)todo.size());
        const Answer a = ask(msg::of(explain), msg::of(question, { count }));
        if (a == Answer::Cancelled) return cancelledAtPrompt();
        if (a != Answer::Yes) {
            say(msg::of(changedSoFar ? skippedAfter : skipped));
            return -1;
        }
        say(msg::of(start, { count }));
        const CopyTally t = copyBatch(pathsOf(todo), kind, CopyOptions(), false);
        const int made = t.n[(int)CopyOutcome::Created];
        const int failed = t.n[(int)CopyOutcome::Skipped] + t.n[(int)CopyOutcome::Refused] + t.n[(int)CopyOutcome::Failed];
        changedSoFar = changedSoFar || made > 0;
        sayBlank();
        if (t.notStarted || t.n[(int)CopyOutcome::Cancelled]) {
            say(cancelText(msg::CancelSave, made, t.total, 0, failed, t.n[(int)CopyOutcome::Cancelled], t.notStarted));
            return finish(kExitCancelled);
        }
        say(msg::of(msg::DoneSave, { msg::num(made), msg::num(failed) }) + (failed ? msg::of(msg::DoneSeeAbove) : msg::Text()));
        notDone += failed;
        return -1;
    };
    int rc = copyStep(wavTodo, CopyKind::WavToFlac, msg::AskWavExplain, msg::AskWavQuestion, msg::StartWav, msg::SkipWav, msg::SkipWavAfter);
    if (rc >= 0) return rc;
    rc = copyStep(c24Todo, CopyKind::To24Bit, msg::Ask24Explain, msg::Ask24Question, msg::Start24, msg::Skip24, msg::Skip24After);
    if (rc >= 0) return rc;
    return finish(notDone ? kExitNotDone : kExitOk);
}

} // namespace cli
