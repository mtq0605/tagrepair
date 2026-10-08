// =============================================================================
// 命令行与一问一答共用的那一层:列文件、进度条、每个文件的结果怎么打(给人看的几行 / 给程序读的一行 JSON)、一批文件挨个做
// =============================================================================
#include "cli.h"
#include "json_out.h"
#include "runctl.h"
#include "win_util.h"

#include <algorithm>
#include <cstdio>

using namespace tagrepair;

namespace {

// 文件夹里认这些扩展名(直接给的文件不看扩展名)
const wchar_t* const kAudioExt[] = { L"flac", L"mp3", L"wav", L"m4a", L"aac", L"ogg", L"opus", L"wma", L"ape", L"alac", L"dsf", L"dff",
                                     L"aiff", L"aif", L"mka", L"ac3", L"eac3", L"ec3", L"dts", L"thd", L"mlp", L"wv", L"tta", L"tak",
                                     L"m4b", L"mp2", L"oga", L"w64" };

bool isAudioName(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot + 1);
    for (wchar_t& c : ext) c = (wchar_t)towlower(c);
    for (const wchar_t* e : kAudioExt)
        if (ext == e) return true;
    return false;
}

void collect(const std::wstring& dirLong, bool recursive, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW((dirLong + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> files, dirs;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) dirs.push_back(name);
        } else if (isAudioName(name)) {
            files.push_back(name);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
    for (const std::wstring& f : files) out.push_back(dirLong + L"\\" + f);
    for (const std::wstring& d : dirs) collect(dirLong + L"\\" + d, recursive, out);
}

std::wstring khz(int hz) {
    wchar_t b[32];
    if (hz % 1000 == 0) _snwprintf_s(b, _TRUNCATE, L"%d kHz", hz / 1000);
    else if (hz % 100 == 0) _snwprintf_s(b, _TRUNCATE, L"%.1f kHz", hz / 1000.0);
    else _snwprintf_s(b, _TRUNCATE, L"%.2f kHz", hz / 1000.0);
    return b;
}

// 标签的值是文件里的原样字节,不一定是合法的 UTF-8;进 JSON 之前过一遍(不合法的字节换成 U+FFFD),这一行才总是合法的 UTF-8
std::string cleanUtf8(const std::string& s) { return validUtf8(s) ? s : wu::utf8(wu::wide(s)); }

void tagsJson(jsonout::Writer& w, const Seen& s) {
    w.beginObj("tags");
    for (int k = 0; k < kKeys; ++k) w.key(keyName(k)).val(cleanUtf8(s.value[k]));
    w.endObj();
    w.beginObj("tagCount");
    for (int k = 0; k < kKeys; ++k) w.key(keyName(k)).val(s.count[k]);
    w.endObj();
}

std::vector<std::string> keyNames(unsigned mask) {
    std::vector<std::string> v;
    for (int k = 0; k < kKeys; ++k)
        if (mask & (1u << k)) v.push_back(keyName(k));
    return v;
}

void scanJson(jsonout::Writer& w, const Scan& s) {
    const unsigned bad = s.info.marker ? s.bad : 0;
    const msg::Text text = stateMsg(s);
    w.key("state").val(stateCode(s.state));
    w.key("text").val(wu::utf8(text.zh));
    w.key("textEn").val(wu::utf8(text.en));
    w.key("container").val(s.info.container);
    w.key("codec").val(s.info.codec);
    w.key("rate").val(s.info.rate);
    w.key("bits").val(s.info.bits);
    w.key("channels").val(s.info.channels);
    w.key("marker").val(s.info.marker);
    w.key("origRate").val(s.info.origRate);
    w.key("bad").arrOfStr(keyNames(bad));
    tagsJson(w, s.seen);
    // 每个键合不合格(在、只有一份、值对);修的话各个键会写成什么(只列要写的;不是"可以修复"的文件为空)
    w.beginObj("tagOk");
    for (int k = 0; k < kKeys; ++k) w.key(keyName(k)).val(s.info.marker && !(bad & (1u << k)));
    w.endObj();
    w.beginObj("write");
    if (s.state == State::Repairable)
        for (int k = 0; k < kKeys; ++k)
            if (bad & (1u << k)) w.key(keyName(k)).val(cleanUtf8(s.fin[k]));
    w.endObj();
    // 这个文件上能做的事:repair(fix 原地修复)、wav_to_flac(fix --wav-to-flac)、copy_24bit(fix --copy-24bit)
    std::vector<std::string> actions;
    if (s.canRepair) actions.push_back("repair");
    if (s.canWavToFlac) actions.push_back(copyKindCode(CopyKind::WavToFlac));
    if (s.canCopy24) actions.push_back(copyKindCode(CopyKind::To24Bit));
    w.key("actions").arrOfStr(actions);
}

void printTags(const Seen& s) {
    for (int k = 0; k < kKeys; ++k) {
        std::wstring line = L"  " + wu::wide(keyName(k));
        line.resize(22, L' ');
        line += s.count[k] ? wu::wide(s.value[k]) : std::wstring(L"-");
        if (s.count[k] > 1) line += L"  [" + msg::of(msg::OutCopies, { msg::num(s.count[k]) }).both() + L"]";
        wu::outLine(line);
    }
}

void emit(jsonout::Writer& w) { wu::outLineUtf8(w.end().text()); }

unsigned long long nowMs() { return GetTickCount64(); }

} // namespace

namespace cli {

std::vector<std::wstring> expandInputs(const std::vector<std::wstring>& inputs, bool recursive) {
    std::vector<std::wstring> files;
    for (const std::wstring& in : inputs) {
        const std::wstring lp = wu::longPath(in);
        const DWORD a = GetFileAttributesW(lp.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) collect(lp, recursive, files);
        else files.push_back(lp);
    }
    return files;
}

msg::Text noFfmpegText(const std::vector<std::wstring>& tried) {
    msg::Text names;
    std::wstring dirs;
    for (int i = 0; i < probe::kFfmpegDlls; ++i) {
        names.en += (i ? L", " : L"") + std::wstring(probe::ffmpegDllName(i));
        names.zh += (i ? L"、" : L"") + std::wstring(probe::ffmpegDllName(i));
    }
    for (const std::wstring& d : tried) dirs += (dirs.empty() ? L"" : L";") + d;
    return msg::of(msg::NoFfmpeg, { names, dirs });
}

void say(const msg::Text& t, int indent) { wu::outLine(std::wstring((size_t)indent, L' ') + t.both()); }
void sayBlank() { wu::outLine(L""); }

// ---------------------------------------------------------------------------
// 进度条
// ---------------------------------------------------------------------------
Progress::Progress(const msg::Entry& label, size_t total, bool enabled) : m_label(msg::of(label).both()), m_total(total) {
    m_on = enabled && total > 0 && wu::stdoutIsConsole();
}

void Progress::begin(size_t index, const std::wstring& shownPath) {
    m_index = index;
    m_within = 0;
    m_name = wu::fileName(shownPath);
    draw(true);
}

void Progress::within(double fraction) {
    if (fraction > m_within) m_within = fraction;             // 只往前走
    draw(false);
}

void Progress::end() {
    if (m_on) wu::clearStatus();
    m_on = false;
}

void Progress::draw(bool force) {
    if (!m_on) return;
    const double whole = ((double)m_index + (m_within > 1 ? 1 : m_within)) / (double)m_total;
    const int permille = (int)(whole * 1000);
    const unsigned long long now = nowMs();
    if (!force && (permille == m_lastPermille || now - m_lastTick < 50)) return;      // 没变、或者刚画过:不画
    m_lastPermille = permille;
    m_lastTick = now;
    wu::statusLine(progressText(m_label, m_index, m_total, m_within, m_name));
}

// ---------------------------------------------------------------------------
// 一个文件的结果
// ---------------------------------------------------------------------------
std::wstring fileHeader(size_t i, size_t n, const std::wstring& shownPath) {
    return L"[" + std::to_wstring(i + 1) + L"/" + std::to_wstring(n) + L"] " + shownPath;
}

void printScan(size_t i, size_t n, const std::wstring& shown, const Scan& s) {
    wu::outLine(fileHeader(i, n, shown));
    say(stateMsg(s), 2);
    if (!s.info.marker) return;
    say(msg::of(msg::OutCarrier, { khz(s.info.rate), msg::num(s.info.bits), s.info.origRate > 0 ? khz(s.info.origRate) : std::wstring(L"-") }), 2);
    printTags(s.seen);
    msg::Text can;
    const auto add = [&](const msg::Entry& e) {
        can.en += (can.en.empty() ? L"" : L"; ") + std::wstring(e.en);
        can.zh += (can.zh.empty() ? L"" : L";") + std::wstring(e.zh);
    };
    if (s.canRepair) add(msg::ActRepair);
    if (s.canWavToFlac) add(msg::ActWavToFlac);
    if (s.canCopy24) add(msg::ActCopy24);
    if (!can.empty()) say(msg::of(msg::OutActions, { can }), 2);
}

void jsonScan(const std::wstring& shown, const Scan& s) {
    jsonout::Writer w;
    w.begin().key("type").val("file").key("path").val(wu::utf8(shown));
    scanJson(w, s);
    emit(w);
}

void printFix(size_t i, size_t n, const std::wstring& shown, const FixReport& r) {
    wu::outLine(fileHeader(i, n, shown));
    const msg::Text mode = msg::of(r.inPlace ? msg::ModeInPlace : msg::ModeRewrite);
    switch (r.outcome) {
    case Outcome::Fixed:
        say(msg::of(msg::OutFixed, { mode }), 2);
        printTags(r.after);
        say(msg::of(msg::OutAudioSame, { msg::num((long long)r.audioBytes), wu::wide(r.audioSha256.substr(0, 16)), wu::wide(r.pcmMd5) }), 2);
        if (!r.backup.empty()) say(msg::of(msg::OutBackupAt, { wu::shownPath(r.backup) }), 2);
        if (r.staleRemoved) say(msg::of(msg::OutStaleRemoved, { msg::num(r.staleRemoved) }), 2);
        break;
    case Outcome::WouldFix:
        say(msg::of(msg::OutWouldMode, { r.why, mode }), 2);
        break;
    default:
        say(r.why, 2);
        break;
    }
}

void jsonFix(const std::wstring& shown, const FixReport& r) {
    const bool did = r.outcome == Outcome::Fixed || r.outcome == Outcome::WouldFix;
    jsonout::Writer w;
    w.begin().key("type").val("fix").key("path").val(wu::utf8(shown));
    w.key("outcome").val(outcomeCode(r.outcome)).key("code").val(r.code).key("reason").val(wu::utf8(r.why.zh));
    w.key("reasonEn").val(wu::utf8(r.why.en));
    w.key("mode").val(!did ? "" : r.inPlace ? "in_place" : "rewrite");
    w.key("wrote").arrOfStr(keyNames(did ? r.wrote : 0));
    w.key("audioSha256").val(r.audioSha256).key("audioBytes").val((long long)r.audioBytes);
    w.key("pcmMd5").val(r.pcmMd5).key("pcmFrames").val((long long)r.pcmFrames);
    w.key("backup").val(wu::utf8(wu::shownPath(r.backup)));
    w.key("leftAt").val(wu::utf8(wu::shownPath(r.leftAt))).key("staleRemoved").val(r.staleRemoved);
    w.beginObj("before");
    scanJson(w, r.scan);
    w.endObj();
    if (r.outcome == Outcome::Fixed) tagsJson(w, r.after);
    emit(w);
}

void printCopy(size_t i, size_t n, const std::wstring& shown, const CopyReport& r) {
    wu::outLine(fileHeader(i, n, shown));
    say(r.why, 2);
    if (r.outcome != CopyOutcome::Created) return;
    for (int k = 0; k < kKeys; ++k) {
        std::wstring line = L"  " + wu::wide(keyName(k));
        line.resize(22, L' ');
        wu::outLine(line + wu::wide(r.fin[k]));
    }
}

void jsonCopy(const std::wstring& shown, CopyKind kind, const CopyReport& r) {
    const bool made = r.outcome == CopyOutcome::Created;
    jsonout::Writer w;
    w.begin().key("type").val("copy").key("action").val(copyKindCode(kind)).key("path").val(wu::utf8(shown));
    w.key("outcome").val(copyOutcomeCode(r.outcome)).key("code").val(r.code).key("reason").val(wu::utf8(r.why.zh));
    w.key("reasonEn").val(wu::utf8(r.why.en));
    w.key("output").val(made || r.outcome == CopyOutcome::WouldCreate ? wu::utf8(wu::shownPath(r.target)) : std::string());
    w.key("bits").val(made ? r.bits : 0).key("pcmFrames").val((long long)(made ? r.frames : 0)).key("pcmMd5").val(made ? r.pcmMd5 : std::string());
    w.beginObj("before");
    scanJson(w, r.scan);
    w.endObj();
    if (made) {
        w.beginObj("tags");
        for (int k = 0; k < kKeys; ++k) w.key(keyName(k)).val(cleanUtf8(r.fin[k]));
        w.endObj();
    }
    emit(w);
}

// ---------------------------------------------------------------------------
// 一批文件挨个做
// ---------------------------------------------------------------------------
RepairTally repairBatch(const std::vector<std::wstring>& files, const FixOptions& opt, bool json) {
    RepairTally t;
    t.total = (int)files.size();
    Progress bar(msg::PgRepair, files.size(), !json);
    size_t i = 0;
    for (; i < files.size(); ++i) {
        runctl::testPause();
        if (runctl::cancelRequested()) break;                 // 文件与文件之间:看到取消就停,剩下的没有开始
        const std::wstring shown = wu::shownPath(files[i]);
        bar.begin(i, shown);
        FixOptions o = opt;
        o.progress = [&](double f) { bar.within(f); };
        const FixReport r = fixFile(files[i], o);
        ++t.n[(int)r.outcome];
        if (json) jsonFix(shown, r);
        else printFix(i, files.size(), shown, r);
    }
    bar.end();
    t.notStarted = (int)(files.size() - i);
    t.cancelled = runctl::cancelRequested();
    return t;
}

CopyTally copyBatch(const std::vector<std::wstring>& files, CopyKind kind, const CopyOptions& opt, bool json) {
    CopyTally t;
    t.total = (int)files.size();
    Progress bar(msg::PgSave, files.size(), !json);
    size_t i = 0;
    for (; i < files.size(); ++i) {
        runctl::testPause();
        if (runctl::cancelRequested()) break;
        const std::wstring shown = wu::shownPath(files[i]);
        bar.begin(i, shown);
        CopyOptions o = opt;
        o.progress = [&](double f) { bar.within(f); };
        const CopyReport r = copyFile(files[i], kind, o);
        ++t.n[(int)r.outcome];
        if (json) jsonCopy(shown, kind, r);
        else printCopy(i, files.size(), shown, r);
    }
    bar.end();
    t.notStarted = (int)(files.size() - i);
    t.cancelled = runctl::cancelRequested();
    return t;
}

} // namespace cli
