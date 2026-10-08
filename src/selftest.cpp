// =============================================================================
// tagrepair selftest —— 自己造文件(synth_flac.*),把"会不会把用户的文件改坏"的每一条在真文件上验一遍
// =============================================================================
// 这里验的是纯函数验不了的那一半:真的写盘、真的换入、真的让 FFmpeg 读回与解码、真的编出新文件。
// 每一项都另拿文件的原始字节对一遍(不只信 fixFile / copyFile 自己报的哈希):音频帧到文件末尾逐字节相同、STREAMINFO 不变;
// 另存出来的文件把采样解出来,和造文件时的那些数逐个比。
// 文件全放在临时目录里一个自己建的文件夹下,跑完删掉(--keep 留着看)。
// 每一项的标题在 messages.h 里(T… 那一组),两种语言各写一次。
// =============================================================================
#include "copy_flac.h"
#include "runctl.h"
#include "tagfile.h"
#include "test_files.h"
#include "win_util.h"

#include <bcrypt.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tagrepair;
using namespace testfiles;

namespace {

int g_pass = 0, g_fail = 0;

void check(const wchar_t* number, const msg::Entry& title, bool ok, const msg::Text& note = msg::Text()) {
    if (ok) ++g_pass; else ++g_fail;
    std::wstring line = msg::of(ok ? msg::TestPass : msg::TestFail).both() + L"  " + number + L"  " + msg::of(title).both();
    if (!note.empty()) line += L"  — " + note.both();
    wu::outLine(line);
}

// 几条说明接成一条(中间用 | 隔开)
msg::Text join(const msg::Text& a, const msg::Text& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + msg::Text(L" | ") + b;
}

// 期望值从 tag_rule.h 的两个常量来(默认写的值、认可的前缀),不在这里再抄一份
const std::string kOurs = kDefaultEncoderTag;
const std::string kOfficial = std::string(kDefaultAcceptPrefix) + " v1.1, 0.0.0+000 (0000000), 00000000-0000-0000-0000-000000000000, Jan 01 2000 00:00:00";

bool fileExists(const std::wstring& p) { return GetFileAttributesW(wu::longPath(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

// f 旁边有没有留下临时文件(<名字>.tagfix.…;文件名太长时用的 ~tagfix.…)
bool anyTemp(const std::wstring& f) {
    const std::wstring lp = wu::longPath(f);
    const size_t slash = lp.find_last_of(L"\\/");
    const std::wstring pats[2] = { lp + L".tagfix.*", lp.substr(0, slash + 1) + L"~tagfix.*" };
    for (const std::wstring& p : pats) {
        WIN32_FIND_DATAW fd{};
        const HANDLE h = FindFirstFileW(p.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
            return true;
        }
    }
    return false;
}

// 修完之后的通用核对:音频逐字节相同、STREAMINFO 不变、三个键各一份且值对、FFmpeg 读回一致、扫描说完整
bool repaired(const std::wstring& path, const Bytes& before, const std::string want[kKeys], int origRate, msg::Text& why, const Rule& rule = defaultRule()) {
    const Bytes after = readFile(path);
    const Parsed a = parse(before), b = parse(after);
    if (!a.ok || !b.ok) { why = msg::of(msg::TestWhyParse); return false; }
    if (a.audio != b.audio) { why = msg::of(msg::TestWhyAudio); return false; }
    if (memcmp(a.lay.streamInfo, b.lay.streamInfo, 34) != 0) { why = msg::of(msg::TestWhyStreamInfo); return false; }
    if (Bytes(before.begin(), before.begin() + (std::ptrdiff_t)a.lay.metaAt) != Bytes(after.begin(), after.begin() + (std::ptrdiff_t)b.lay.metaAt)) {
        why = msg::of(msg::TestWhyPrefix);
        return false;
    }
    if (problems(b.seen, origRate, rule) != 0) { why = msg::of(msg::TestWhyKeys); return false; }
    for (int k = 0; k < kKeys; ++k)
        if (b.seen.value[k] != want[k]) { why = msg::of(msg::TestWhyValue, { wu::wide(keyName(k)), wu::wide(b.seen.value[k]) }); return false; }
    Scan s;
    scanFile(path, s, rule);
    if (s.state != State::Complete) { why = msg::of(msg::TestWhyRescan, { stateMsg(s) }); return false; }
    for (int k = 0; k < kKeys; ++k)
        if (!s.info.has[k] || s.info.value[k] != want[k]) { why = msg::of(msg::TestWhyFfmpeg, { wu::wide(keyName(k)) }); return false; }
    return true;
}

// 原来的注释(除了三个键)还在、顺序没变
bool othersKept(const Bytes& before, const Bytes& after) {
    const Parsed a = parse(before), b = parse(after);
    auto strip = [](const Parsed& p) {
        std::vector<std::string> v;
        for (const flacmeta::Comments& c : p.vcs) {
            v.push_back("<vendor>" + c.vendor);
            for (const std::string& it : c.items) {
                std::string key, val;
                bool ours = false;
                if (flacmeta::splitItem(it, key, val))
                    for (int k = 0; k < kKeys; ++k) ours = ours || keyEquals(key, keyName(k));
                if (!ours) v.push_back(it);
            }
        }
        return v;
    };
    auto rest = [](const Parsed& p) {
        std::vector<std::pair<int, Bytes>> v;
        for (const flacmeta::MetaBlock& x : p.blocks)
            if (x.type != flacmeta::kVorbisComment && x.type != flacmeta::kPadding) v.push_back({ x.type, x.body });
        return v;
    };
    return a.ok && b.ok && strip(a) == strip(b) && rest(a) == rest(b);
}

std::wstring g_dir;
std::vector<std::wstring> g_made;

std::wstring mk(const std::wstring& name, const Bytes& d) {
    const std::wstring p = g_dir + L"\\" + name;
    if (!writeFile(p, d)) wu::outLine(msg::of(msg::TestCannotWrite, { p }).both());
    g_made.push_back(p);
    return p;
}

// 自检结束要删的文件(另存出来的新文件)
void made(const std::wstring& p) { g_made.push_back(p); }

// 一个文件的全部采样(交错、左对齐 int32),用 FFmpeg 解;解不了返回空
std::vector<int32_t> decodeSamples(const std::wstring& path) {
    std::vector<int32_t> all;
    const HANDLE h = CreateFileW(wu::longPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
    if (h == INVALID_HANDLE_VALUE) return all;
    probe::Watch watch;
    watch.sink = [&](const int32_t* lr, size_t samples) { all.insert(all.end(), lr, lr + samples); };
    probe::Pcm pcm;
    msg::Text err;
    if (!probe::decodeAll(h, wu::fileName(path), pcm, err, &watch) || pcm.errors) all.clear();
    CloseHandle(h);
    return all;
}

// 造文件时的那些数(右对齐、bits 位)→ 左对齐 int32,再左移 pad 位(16 bit 的放进 24 bit 的格子)
std::vector<int32_t> leftJustified(const std::vector<int32_t>& lr, int bits) {
    std::vector<int32_t> o(lr.size());
    for (size_t i = 0; i < lr.size(); ++i) o[i] = (int32_t)((uint32_t)lr[i] << (32 - bits));
    return o;
}

// 这些数按 bits 位小端排出来的 MD5(FLAC 文件头里的那个)
Bytes md5Of(const std::vector<int32_t>& lr, int bits) {
    Bytes raw, md5(16, 0);
    for (int32_t v : lr)
        for (int b = 0; b < bits / 8; ++b) raw.push_back((uint8_t)((uint32_t)v >> (8 * b)));
    wu::Hash h(BCRYPT_MD5_ALGORITHM);
    h.update(raw.data(), raw.size());
    h.raw(md5.data(), 16);
    return md5;
}

// 带 LIST / INFO 块的 WAV(标题、艺人之类的普通标签;块放在 data 之前)
Bytes wavWithInfo(const std::vector<int32_t>& lr, int rate, int bits, const std::vector<std::pair<std::string, std::string>>& info) {
    const Bytes plain = wav(lr, rate, bits);                  // "RIFF" 长度 "WAVE" + fmt 块(共 36 字节)+ data 块
    Bytes list = { 'I', 'N', 'F', 'O' };
    auto le32 = [](Bytes& o, uint32_t v) {
        for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i)));
    };
    for (const auto& kv : info) {
        std::string value = kv.second;
        value.push_back('\0');
        if (value.size() & 1) value.push_back('\0');          // 块长要是偶数
        list.insert(list.end(), kv.first.begin(), kv.first.end());
        le32(list, (uint32_t)value.size());
        list.insert(list.end(), value.begin(), value.end());
    }
    Bytes o(plain.begin(), plain.begin() + 36);
    o.insert(o.end(), { 'L', 'I', 'S', 'T' });
    le32(o, (uint32_t)list.size());
    o.insert(o.end(), list.begin(), list.end());
    o.insert(o.end(), plain.begin() + 36, plain.end());
    const uint32_t riff = (uint32_t)(o.size() - 8);
    for (int i = 0; i < 4; ++i) o[4 + (size_t)i] = (uint8_t)(riff >> (8 * i));
    return o;
}

// 记下走过的阶段;走到 stopAt 时立起取消标志
struct PhaseLog {
    std::vector<std::string> seen;
    std::string stopAt;
    std::function<void(const char*)> hook() {
        return [this](const char* name) {
            seen.push_back(name);
            if (stopAt == name) runctl::requestCancel();
        };
    }
    bool has(const char* name) const {
        for (const std::string& s : seen)
            if (s == name) return true;
        return false;
    }
    bool endsWith(const char* name) const { return !seen.empty() && seen.back() == name; }
};

} // namespace

int runSelftest(bool keepFiles) {
    wchar_t tmp[MAX_PATH + 2] = {};
    GetTempPathW(MAX_PATH + 1, tmp);
    g_dir = std::wstring(tmp) + L"tagrepair_selftest_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(g_dir.c_str(), nullptr);
    wu::outLine(msg::of(msg::TestFolder, { g_dir }).both());

    const std::vector<int32_t> p441 = pcm(44100, 24, 3.0, 5000, 352800, 11);
    const std::vector<int32_t> plain = pcm(44100, 24, 3.0, -1, 0, 11);
    const std::string wantOurs352[kKeys] = { kOurs, kOurs, "352800" };
    const std::vector<std::string> lost = { "TITLE=合成曲线", "ARTIST=Nobody", "encoder=Lavf58.76.100", "DATE=2026" };
    const Bytes seek = [] { Bytes s(18, 0); for (int i = 0; i < 8; ++i) s[(size_t)i] = 0xFF; return s; }();     // 一个占位的定位点
    FixOptions def;
    msg::Text why;

    // ---- 1. 丢了标签(还留着 encoder=Lavf…),有 PADDING:原地改写;文件名带中文与符号 ----
    Bytes fixedOnce;
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kSeekTable, seek }, { flacmeta::kVorbisComment, vc("reference libFLAC 1.3.2 20170101", lost) },
                                                   { flacmeta::kPicture, picture() }, { flacmeta::kPadding, zeros(4096) } });
        const std::wstring f = mk(L"丢了标签 ♪ é.flac", orig);
        Scan s;
        scanFile(f, s);
        check(L"1.1", msg::T1_1,
              s.state == State::Repairable && s.info.marker && s.info.origRate == 352800 && s.bad == 7 && s.seen.count[0] == 1 &&
                  s.seen.value[0] == "Lavf58.76.100" && s.info.rate == 44100 && s.info.bits == 24,
              stateMsg(s));
        const FixReport r = fixFile(f, def);
        const Bytes after = readFile(f);
        const bool ok = r.outcome == Outcome::Fixed && r.inPlace && repaired(f, orig, wantOurs352, 352800, why);
        const Parsed a = parse(orig), b = parse(after);
        check(L"1.2", msg::T1_2, ok && a.lay.audioAt == b.lay.audioAt && orig.size() == after.size(), ok ? msg::Text() : join(r.why, why));
        const std::vector<std::string> want = { "TITLE=合成曲线", "ARTIST=Nobody", std::string("ENCODER=") + kOurs, "DATE=2026",
                                                std::string("MQAENCODER=") + kOurs, "ORIGINALSAMPLERATE=352800" };
        check(L"1.3", msg::T1_3,
              b.ok && b.vcs.size() == 1 && b.vcs[0].items == want && b.vcs[0].vendor == "reference libFLAC 1.3.2 20170101" && othersKept(orig, after));
        const FixReport r2 = fixFile(f, def);
        check(L"1.4", msg::T1_4, r2.outcome == Outcome::Unchanged && readFile(f) == after, r2.why);
        fixedOnce = after;
    }
    // ---- 2. 没有 PADDING:整文件重写后换入;建立时间保留;临时文件不留 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        const std::wstring f = mk(L"no_padding.flac", orig);
        FILETIME made{};
        SYSTEMTIME st{};
        st.wYear = 2001; st.wMonth = 2; st.wDay = 3; st.wHour = 4;
        SystemTimeToFileTime(&st, &made);
        HANDLE h = CreateFileW(f.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        SetFileTime(h, &made, nullptr, nullptr);
        CloseHandle(h);
        const FixReport r = fixFile(f, def);
        const Bytes after = readFile(f);
        const Parsed a = parse(orig), b = parse(after);
        FILETIME made2{};
        h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        GetFileTime(h, &made2, nullptr, nullptr);
        CloseHandle(h);
        const bool ok = r.outcome == Outcome::Fixed && !r.inPlace && repaired(f, orig, wantOurs352, 352800, why);
        check(L"2.1", msg::T2_1, ok && b.lay.audioAt > a.lay.audioAt && othersKept(orig, after), ok ? msg::Text() : join(r.why, why));
        check(L"2.2", msg::T2_2, CompareFileTime(&made, &made2) == 0 && !anyTemp(f));
        const FixReport r2 = fixFile(f, def);
        check(L"2.3", msg::T2_3, r2.outcome == Outcome::Unchanged && readFile(f) == after);
    }
    // ---- 3. 一个注释块都没有 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kApplication, Bytes{ 't', 'e', 's', 't', 1, 2, 3, 4 } }, { flacmeta::kPicture, picture() },
                                                   { flacmeta::kPadding, zeros(1000) } });
        const std::wstring f = mk(L"no_comment_block.flac", orig);
        const FixReport r = fixFile(f, def);
        const Bytes after = readFile(f);
        const Parsed b = parse(after);
        const bool ok = r.outcome == Outcome::Fixed && repaired(f, orig, wantOurs352, 352800, why);
        check(L"3", msg::T3,
              ok && b.vcs.size() == 1 && b.vcs[0].items.size() == 3 && b.blocks.size() == 5 && b.blocks[1].type == flacmeta::kApplication &&
                  b.blocks[1].body == Bytes({ 't', 'e', 's', 't', 1, 2, 3, 4 }) && b.blocks[2].type == flacmeta::kPicture &&
                  b.blocks[2].body == picture() && b.blocks[3].type == flacmeta::kVorbisComment && b.blocks[4].type == flacmeta::kPadding,
              ok ? msg::Text() : join(r.why, why));
    }
    // ---- 4. 两个注释块:第二个里的旧值不许留 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("first", { "TITLE=T" }) },
                                                   { flacmeta::kVorbisComment, vc("second", { "ENCODER=Lavf", "COMMENT=keep", "ORIGINALSAMPLERATE=44100" }) },
                                                   { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"two_comment_blocks.flac", orig);
        const FixReport r = fixFile(f, def);
        const Bytes after = readFile(f);
        const Parsed b = parse(after);
        const bool ok = r.outcome == Outcome::Fixed && repaired(f, orig, wantOurs352, 352800, why);
        check(L"4", msg::T4,
              ok && b.vcs.size() == 2 && b.vcs[1].items == std::vector<std::string>{ "COMMENT=keep" } && b.vcs[1].vendor == "second" &&
                  b.seen.count[0] == 1 && b.seen.count[2] == 1,
              ok ? msg::Text() : join(r.why, why));
    }
    // ---- 5. "fLaC" 前面有 ID3v2、文件末尾有 ID3v1 ----
    {
        Bytes tail(128, ' ');
        tail[0] = 'T'; tail[1] = 'A'; tail[2] = 'G';
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } },
                                synth::id3v2(300), tail);
        const std::wstring f = mk(L"id3_front_and_back.flac", orig);
        Scan s;
        scanFile(f, s);
        const FixReport r = fixFile(f, def);
        const Bytes after = readFile(f);
        const Parsed b = parse(after);
        const bool ok = r.outcome == Outcome::Fixed && repaired(f, orig, wantOurs352, 352800, why);
        check(L"5", msg::T5,
              s.state == State::Repairable && ok && b.lay.id3Bytes == 310 && after.size() == orig.size() &&
                  Bytes(after.end() - 128, after.end()) == tail && Bytes(after.begin(), after.begin() + 310) == Bytes(orig.begin(), orig.begin() + 310),
              ok ? msg::Text() : join(stateMsg(s), join(r.why, why)));
    }
    // ---- 6. 写进去会超过块长上限:拒绝 ----
    {
        const std::string huge(flacmeta::kMaxBlockBody - 40, 'p');
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "P=" + huge }) } });
        const std::wstring f = mk(L"comment_16mb.flac", orig);
        const FixReport r = fixFile(f, def);
        check(L"6", msg::T6, r.outcome == Outcome::Refused && r.code == "too_big" && readFile(f) == orig && !anyTemp(f), r.why);
    }
    // ---- 7. 只读;被别的程序开着 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"read_only.flac", orig);
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_READONLY);
        const FixReport r = fixFile(f, def);
        check(L"7.1", msg::T7_1, r.outcome == Outcome::Refused && r.code == "read_only" && readFile(f) == orig, r.why);
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
        const std::wstring g = mk(L"in_use.flac", orig);
        HANDLE reader = CreateFileW(g.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        const FixReport r2 = fixFile(g, def);
        CloseHandle(reader);
        HANDLE locker = CreateFileW(g.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        const FixReport r3 = fixFile(g, def);
        CloseHandle(locker);
        check(L"7.2", msg::T7_2,
              r2.outcome == Outcome::Refused && r2.code == "in_use" && r3.outcome == Outcome::Refused && r3.code == "in_use" && readFile(g) == orig &&
                  !anyTemp(g),
              r2.why);
        const FixReport r4 = fixFile(g, def);
        check(L"7.3", msg::T7_3, r4.outcome == Outcome::Fixed && repaired(g, orig, wantOurs352, 352800, why), join(r4.why, why));
    }
    // ---- 8. 截断 / 损坏 ----
    {
        const Bytes good = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T", "COMMENT=" + std::string(200, 'c') }) },
                                                   { flacmeta::kPadding, zeros(600) } });
        const Parsed g = parse(good);
        const Bytes cutMeta(good.begin(), good.begin() + (std::ptrdiff_t)(g.lay.metaAt + 38 + 60));            // 断在注释块里
        const Bytes cutAudio(good.begin(), good.end() - 9000);                                                 // 最后一帧不全
        Bytes flipped(good);
        flipped[(size_t)g.lay.audioAt + 30000] ^= 0x40;                                                        // 音频帧里翻一位(帧 CRC 对不上)
        Bytes badLen(good);
        badLen[(size_t)g.lay.metaAt + 38 + 3] ^= 0x08;                                                         // 注释块的长度写坏
        Bytes junk(good.begin(), good.begin() + (std::ptrdiff_t)g.lay.audioAt);                                // 元数据与第一帧之间多出 3 个字节:
        junk.insert(junk.end(), { 0x00, 0x11, 0x22 });                                                         // FFmpeg 会跳过去照样放,我们的解析器不认
        junk.insert(junk.end(), good.begin() + (std::ptrdiff_t)g.lay.audioAt, good.end());
        const struct { const wchar_t* name; const Bytes* d; } cases[5] = { { L"cut_in_metadata.flac", &cutMeta }, { L"cut_in_audio.flac", &cutAudio },
                                                                           { L"bit_flip_in_audio.flac", &flipped }, { L"bad_block_length.flac", &badLen },
                                                                           { L"junk_before_audio.flac", &junk } };
        bool all = true;
        msg::Text notes;
        for (const auto& c : cases) {
            const std::wstring f = mk(c.name, *c.d);
            const FixReport r = fixFile(f, def);
            const bool ok = (r.outcome == Outcome::Refused || r.outcome == Outcome::Skipped) && readFile(f) == *c.d && !anyTemp(f);
            all = all && ok;
            notes = join(notes, msg::Text(std::wstring(c.name) + L": ") + r.why);
        }
        Scan sj;
        scanFile(g_dir + L"\\junk_before_audio.flac", sj);
        check(L"8", msg::T8, all && sj.state == State::Damaged && sj.info.marker, notes);
    }
    // ---- 9. 没有同步标记的、别的容器、有损 ----
    {
        const Bytes noMarker = flac(plain, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"plain.flac", noMarker);
        Scan s;
        scanFile(f, s);
        const FixReport r = fixFile(f, def);
        check(L"9.1", msg::T9_1, s.state == State::NoMarker && s.info.sniffed && !s.info.marker && r.outcome == Outcome::Skipped && readFile(f) == noMarker,
              stateMsg(s));
        const Bytes wMarked = wav(p441, 44100, 24), wPlain = wav(plain, 44100, 24);
        const std::wstring fw = mk(L"carrier.wav", wMarked), fp = mk(L"plain.wav", wPlain);
        Scan sw, sp;
        scanFile(fw, sw);
        scanFile(fp, sp);
        const FixReport rw = fixFile(fw, def);
        check(L"9.2", msg::T9_2,
              sw.state == State::Convertible && sw.canWavToFlac && !sw.canRepair && sw.info.marker && sw.info.origRate == 352800 &&
                  sw.info.container == "wav" && sp.state == State::NoMarker && !sp.canWavToFlac && rw.outcome == Outcome::Skipped &&
                  rw.code == "convertible" && readFile(fw) == wMarked && readFile(fp) == wPlain && !fileExists(g_dir + L"\\carrier.flac") && !anyTemp(fw),
              join(stateMsg(sw), stateMsg(sp)));
        std::vector<int32_t> mu = synth::noise(44100, 16, 5);
        for (int32_t& v : mu) v &= 0xFF;
        const std::wstring fm = mk(L"mulaw.wav", wav(mu, 44100, 8, 7));
        Scan sm;
        scanFile(fm, sm);
        check(L"9.3", msg::T9_3, sm.state == State::NotLossless, stateMsg(sm));
        const Bytes junk = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
        const std::wstring ft = mk(L"not_audio.flac", junk);
        Scan st;
        scanFile(ft, st);
        const FixReport rt = fixFile(ft, def);
        Scan sn;
        scanFile(g_dir + L"\\no_such_file.flac", sn);
        check(L"9.4", msg::T9_4,
              (st.state == State::Error || st.state == State::NoMarker) && rt.outcome == Outcome::Skipped && readFile(ft) == junk && sn.state == State::Error,
              join(stateMsg(st), stateMsg(sn)));
    }
    // ---- 10. ORIGINALSAMPLERATE 写错了:只改它,官方的 ENCODER / MQAENCODER 留着 ----
    {
        const std::vector<std::string> items = { "TITLE=T", std::string("ENCODER=") + kOfficial, std::string("MQAENCODER=") + kOfficial,
                                                 "ORIGINALSAMPLERATE=96000" };
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", items) }, { flacmeta::kPadding, zeros(100) } });
        const std::wstring f = mk(L"wrong_rate.flac", orig);
        Scan s;
        scanFile(f, s);
        const FixReport r = fixFile(f, def);
        const std::string want[kKeys] = { kOfficial, kOfficial, "352800" };
        const bool ok = r.outcome == Outcome::Fixed && r.wrote == kBadOrigRate && repaired(f, orig, want, 352800, why);
        check(L"10", msg::T10, s.state == State::Repairable && s.bad == kBadOrigRate && ok, ok ? msg::Text() : join(r.why, why));
    }
    // ---- 11. 试运行;留备份 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"dry_run.flac", orig);
        FixOptions dry;
        dry.dryRun = true;
        const FixReport r = fixFile(f, dry);
        check(L"11.1", msg::T11_1, r.outcome == Outcome::WouldFix && r.inPlace && readFile(f) == orig, r.why);
        FixOptions bak;
        bak.keepBackup = true;
        const FixReport r2 = fixFile(f, bak);
        g_made.push_back(f + L".bak");
        check(L"11.2", msg::T11_2,
              r2.outcome == Outcome::Fixed && !r2.inPlace && repaired(f, orig, wantOurs352, 352800, why) && wu::shownPath(r2.backup) == f + L".bak" &&
                  readFile(f + L".bak") == orig,
              join(r2.why, why));
    }
    // ---- 12. 中途失败:换入失败时临时文件删掉、原文件不动;原地写完读回不对就写回去 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        const std::wstring f = mk(L"swap_fails.flac", orig);
        HANDLE blocker = INVALID_HANDLE_VALUE;
        FixOptions o;
        o.test.beforeSwap = [&] { blocker = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); };
        const FixReport r = fixFile(f, o);
        if (blocker != INVALID_HANDLE_VALUE) CloseHandle(blocker);
        check(L"12.1", msg::T12_1, r.outcome == Outcome::Failed && r.code == "swap_failed" && readFile(f) == orig && !anyTemp(f), r.why);
        const Bytes orig2 = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring g = mk(L"verify_fails.flac", orig2);
        const Parsed p2 = parse(orig2);
        FixOptions o2;
        o2.test.afterWrite = [&](HANDLE h) {                   // 模拟写坏:把注释块的长度改掉
            const uint8_t bad = 0x7F;
            wu::writeAt(h, p2.lay.metaAt + 38 + 2, &bad, 1);
        };
        const FixReport r2 = fixFile(g, o2);
        check(L"12.2", msg::T12_2, r2.outcome == Outcome::Failed && r2.code == "verify_failed" && readFile(g) == orig2, r2.why);
    }
    // ---- 12.3–12.5 前后两个指纹各自对不上、写盘失败:都拦住,文件不动(原地、整文件重写两条路各验一次)----
    {
        const Bytes withPad = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const Bytes noPad = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        bool okBy[3] = { true, true, true };
        msg::Text notes[3];
        for (int route = 0; route < 2; ++route)               // 0 原地改写 / 1 整文件重写
            for (int what = 0; what < 3; ++what) {            // 0 SHA-256 对不上 / 1 样本 MD5 对不上 / 2 写盘失败
                const Bytes& orig = route == 0 ? withPad : noPad;
                const std::wstring f = mk(L"guard_" + std::to_wstring(route) + std::to_wstring(what) + L".flac", orig);
                FixOptions o;
                if (what == 2) o.test.writeOk = [] { return false; };
                else o.test.expect = [what](std::string& sha, std::string& md5) {
                    std::string& x = what == 0 ? sha : md5;
                    x[0] = x[0] == '0' ? '1' : '0';
                };
                const FixReport r = fixFile(f, o);
                // 原因说的就是那一条(拿 messages.h 的条目比,不在这里抄文字):SHA-256 对不上 / 样本对不上;写盘失败看原因代码
                const msg::Text expectWhy = msg::of(what == 0 ? msg::VerSha : msg::VerPcm);
                const bool saysIt = what == 2 || (r.why.zh.find(expectWhy.zh) == 0 && r.why.en.find(expectWhy.en) == 0);
                const bool ok = r.outcome == Outcome::Failed && r.code == (what == 2 ? "io" : "verify_failed") && r.inPlace == (route == 0) && saysIt &&
                                readFile(f) == orig && !anyTemp(f);
                okBy[what] = okBy[what] && ok;
                notes[what] = join(notes[what], r.why);
            }
        check(L"12.3", msg::T12_3, okBy[0], notes[0]);
        check(L"12.4", msg::T12_4, okBy[1], notes[1]);
        check(L"12.5", msg::T12_5, okBy[2], notes[2]);
    }
    // ---- 12.6–12.9 换入做到一半(ReplaceFileW 的 1176 / 1177:原来的名字下已经没有文件了)----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        // 1176:没给备份名 —— 原文件已经没了,新文件还在临时名下。1177:给了备份名 —— 原文件被改名成备份,新文件还在临时名下
        const auto half = [](const wchar_t* replaced, const wchar_t*, const wchar_t* backup, DWORD& err) {
            if (backup) MoveFileExW(replaced, backup, 0);
            else DeleteFileW(replaced);
            err = backup ? ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 : ERROR_UNABLE_TO_MOVE_REPLACEMENT;
            return false;
        };
        const auto isTemp = [](const wchar_t* p) {
            const std::wstring s = p;
            return s.size() > 4 && s.compare(s.size() - 4, 4, L".tmp") == 0;
        };
        {
            const std::wstring f = mk(L"swap_half_stuck.flac", orig);
            FixOptions o;
            o.test.replaceFile = half;
            o.test.moveFile = [](const wchar_t*, const wchar_t*, DWORD, DWORD& err) { err = ERROR_ACCESS_DENIED; return false; };
            const FixReport r = fixFile(f, o);
            const Bytes left = r.leftAt.empty() ? Bytes() : readFile(r.leftAt);
            const Parsed a = parse(orig), b = parse(left);
            const bool ok = r.outcome == Outcome::Failed && r.code == "swap_incomplete" && !fileExists(f) && b.ok && a.audio == b.audio &&
                            problems(b.seen, 352800) == 0 && r.why.zh.find(wu::shownPath(r.leftAt)) != std::wstring::npos &&
                            r.why.en.find(wu::shownPath(r.leftAt)) != std::wstring::npos && isTempNameFor(L"swap_half_stuck.flac", wu::fileName(r.leftAt));
            check(L"12.6", msg::T12_6, ok, r.why);
            if (!r.leftAt.empty()) g_made.push_back(r.leftAt);
        }
        {
            const std::wstring f = mk(L"swap_half_retry.flac", orig);
            int calls = 0;
            FixOptions o;
            o.test.replaceFile = half;
            o.test.moveFile = [&calls](const wchar_t* from, const wchar_t* to, DWORD flags, DWORD& err) {
                if (++calls <= 2) { err = ERROR_SHARING_VIOLATION; return false; }
                if (MoveFileExW(from, to, flags)) { err = 0; return true; }
                err = GetLastError();
                return false;
            };
            const FixReport r = fixFile(f, o);
            check(L"12.7", msg::T12_7, r.outcome == Outcome::Fixed && calls == 3 && repaired(f, orig, wantOurs352, 352800, why) && !anyTemp(f), join(r.why, why));
        }
        {
            const std::wstring f = mk(L"swap_half_backup_stuck.flac", orig);
            FixOptions o;
            o.keepBackup = true;
            o.test.replaceFile = half;
            o.test.moveFile = [](const wchar_t*, const wchar_t*, DWORD, DWORD& err) { err = ERROR_ACCESS_DENIED; return false; };
            const FixReport r = fixFile(f, o);
            const bool ok = r.outcome == Outcome::Failed && r.code == "swap_incomplete" && !fileExists(f) && wu::shownPath(r.leftAt) == f + L".bak" &&
                            readFile(f + L".bak") == orig && r.why.zh.find(f + L".bak") != std::wstring::npos &&
                            r.why.en.find(f + L".bak") != std::wstring::npos && !anyTemp(f);
            check(L"12.8", msg::T12_8, ok, r.why);
            g_made.push_back(f + L".bak");
        }
        {
            const std::wstring f = mk(L"swap_half_backup_back.flac", orig);
            FixOptions o;
            o.keepBackup = true;
            o.test.replaceFile = half;
            o.test.moveFile = [&isTemp](const wchar_t* from, const wchar_t* to, DWORD flags, DWORD& err) {
                if (isTemp(from)) { err = ERROR_ACCESS_DENIED; return false; }
                if (MoveFileExW(from, to, flags)) { err = 0; return true; }
                err = GetLastError();
                return false;
            };
            const FixReport r = fixFile(f, o);
            check(L"12.9", msg::T12_9,
                  r.outcome == Outcome::Failed && r.code == "swap_failed" && readFile(f) == orig && !fileExists(f + L".bak") && !anyTemp(f) && r.backup.empty(),
                  r.why);
        }
    }
    // ---- 12.10 文件系统不支持 ReplaceFile:退回改名覆盖;12.11 放掉锁到换入之间文件被别人改了 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        const auto unsupported = [](const wchar_t*, const wchar_t*, const wchar_t*, DWORD& err) { err = ERROR_INVALID_FUNCTION; return false; };
        const std::wstring f = mk(L"no_replacefile.flac", orig), g = mk(L"no_replacefile_backup.flac", orig);
        // 建立时间:NTFS 上改名覆盖会自己把旧文件的建立时间带过来,所以这里让"改名"之后的文件拿到一个新的建立时间
        // (别的文件系统就是这样),看修复有没有把它改回去
        const auto setCreated = [](const std::wstring& p, const FILETIME& t) {
            const HANDLE h = CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return;
            SetFileTime(h, &t, nullptr, nullptr);
            CloseHandle(h);
        };
        const auto createdOf = [](const std::wstring& p) {
            FILETIME t{};
            const HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                GetFileTime(h, &t, nullptr, nullptr);
                CloseHandle(h);
            }
            return t;
        };
        FILETIME made{}, later{};
        SYSTEMTIME st{};
        st.wYear = 2001; st.wMonth = 2; st.wDay = 3; st.wHour = 4;
        SystemTimeToFileTime(&st, &made);
        st.wYear = 2015;
        SystemTimeToFileTime(&st, &later);
        setCreated(f, made);
        FixOptions o;
        o.test.replaceFile = unsupported;
        o.test.moveFile = [&](const wchar_t* from, const wchar_t* to, DWORD flags, DWORD& err) {
            if (!MoveFileExW(from, to, flags)) { err = GetLastError(); return false; }
            setCreated(to, later);
            err = 0;
            return true;
        };
        const FixReport r = fixFile(f, o);
        const FILETIME madeAfter = createdOf(f);
        const bool ok1 = r.outcome == Outcome::Fixed && !r.inPlace && repaired(f, orig, wantOurs352, 352800, why) && !anyTemp(f) &&
                         CompareFileTime(&made, &madeAfter) == 0;
        o.test.moveFile = nullptr;
        o.keepBackup = true;
        const FixReport r2 = fixFile(g, o);
        g_made.push_back(g + L".bak");
        const bool ok2 = r2.outcome == Outcome::Fixed && repaired(g, orig, wantOurs352, 352800, why) && readFile(g + L".bak") == orig && !anyTemp(g);
        check(L"12.10", msg::T12_10, ok1 && ok2, join(r.why, join(r2.why, why)));

        // 12.12 原文件被我们自己拿着的那一把挡住了换入(有的文件系统 / 旧系统会这样):放掉那一把、核对没变,再换一次
        const std::wstring h2 = mk(L"swap_blocked_by_guard.flac", orig);
        int calls = 0;
        FixOptions og;
        og.test.replaceFile = [&calls](const wchar_t* replaced, const wchar_t* replacement, const wchar_t* backup, DWORD& err) {
            if (++calls == 1) { err = ERROR_ACCESS_DENIED; return false; }
            if (ReplaceFileW(replaced, replacement, backup, 0, nullptr, nullptr)) { err = 0; return true; }
            err = GetLastError();
            return false;
        };
        const FixReport rg = fixFile(h2, og);
        check(L"12.12", msg::T12_12, rg.outcome == Outcome::Fixed && calls == 2 && repaired(h2, orig, wantOurs352, 352800, why) && !anyTemp(h2),
              join(rg.why, why));

        const std::wstring c = mk(L"changed_before_swap.flac", orig);
        Bytes edited(orig);
        edited.push_back(0x55);
        FixOptions oc;
        oc.test.beforeSwap = [&] { writeFile(c, edited); };  // 别的程序在这个空当里把文件改了(这里是在末尾多写一个字节)
        const FixReport rc = fixFile(c, oc);
        check(L"12.11", msg::T12_11, rc.outcome == Outcome::Refused && rc.code == "changed" && readFile(c) == edited && !anyTemp(c), rc.why);
    }
    // ---- 13. 48 kHz;16 bit ----
    {
        const std::vector<int32_t> p48 = pcm(48000, 24, 2.0, 100, 192000, 21);
        const Bytes o48 = flac(p48, 48000, 24, { { flacmeta::kVorbisComment, vc("v", {}) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"carrier_48k.flac", o48);
        const FixReport r = fixFile(f, def);
        const std::string want48[kKeys] = { kOurs, kOurs, "192000" };
        const bool ok48 = r.outcome == Outcome::Fixed && r.scan.info.rate == 48000 && repaired(f, o48, want48, 192000, why);
        const std::vector<int32_t> p16 = pcm(44100, 16, 2.0, 2000, 88200, 31);
        const Bytes o16 = flac(p16, 44100, 16, { { flacmeta::kVorbisComment, vc("v", {}) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring g = mk(L"carrier_16bit.flac", o16);
        const FixReport r16 = fixFile(g, def);
        const std::string want16[kKeys] = { kOurs, kOurs, "88200" };
        const bool ok16 = r16.outcome == Outcome::Fixed && r16.scan.info.bits == 16 && repaired(g, o16, want16, 88200, why);
        check(L"13", msg::T13, ok48 && ok16, join(r.why, join(r16.why, why)));
    }
    // ---- 14. 找同步标记的范围:开头 10 秒 ----
    {
        const std::vector<int32_t> in = pcm(44100, 24, 11.0, 436000, 352800, 41);      // 9.89 秒处
        const std::vector<int32_t> out = pcm(44100, 24, 11.0, 450000, 352800, 41);     // 10.2 秒处
        const std::vector<synth::Block> b = { { flacmeta::kVorbisComment, vc("v", {}) }, { flacmeta::kPadding, zeros(600) } };
        const Bytes fo = flac(out, 44100, 24, b);
        const std::wstring fa = mk(L"sync_at_9.9s.flac", flac(in, 44100, 24, b)), fb = mk(L"sync_at_10.2s.flac", fo);
        Scan sa, sb;
        scanFile(fa, sa);
        scanFile(fb, sb);
        const FixReport r = fixFile(fb, def);
        check(L"14", msg::T14,
              sa.state == State::Repairable && sa.info.origRate == 352800 && sb.state == State::NoMarker && r.outcome == Outcome::Skipped && readFile(fb) == fo,
              join(stateMsg(sa), stateMsg(sb)));
        // 第一条记录坏了(校验对不上),3 秒处还有一条好的:找到标记之后接着往下看,采样率读后面那条
        std::vector<int32_t> two = pcm(44100, 24, 5.0, -1, 0, 43);
        std::vector<uint8_t> badRec = synth::startRecordBits(352800, 44100);
        badRec[83] ^= 1;
        synth::embedControl(two, 24, 5000, badRec);
        synth::embedControl(two, 24, 132300, synth::startRecordBits(176400, 44100));
        const std::wstring fc = mk(L"bad_record_then_good.flac", flac(two, 44100, 24, b));
        Scan sc;
        scanFile(fc, sc);
        check(L"14.2", msg::T14_2, sc.state == State::Repairable && sc.info.marker && sc.info.origRate == 176400, stateMsg(sc));
    }
    // ---- 15. 超过 260 个字符的路径 ----
    {
        std::wstring deep = g_dir;
        std::vector<std::wstring> dirs;
        for (int i = 0; i < 4; ++i) {
            deep += L"\\" + std::wstring(60, (wchar_t)(L'a' + i)) + L"_很长的文件夹名";
            CreateDirectoryW(wu::longPath(deep).c_str(), nullptr);
            dirs.push_back(deep);
        }
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        const std::wstring f = deep + L"\\深处的文件.flac";
        writeFile(f, orig);
        const FixReport r = fixFile(f, def);
        const bool ok = f.size() > 300 && r.outcome == Outcome::Fixed && !r.inPlace && repaired(f, orig, wantOurs352, 352800, why);
        check(L"15", msg::T15, ok && !anyTemp(f), ok ? msg::Text() : join(r.why, why));
        if (!keepFiles) {
            DeleteFileW(wu::longPath(f).c_str());
            for (size_t i = dirs.size(); i-- > 0;) RemoveDirectoryW(wu::longPath(dirs[i]).c_str());
        }
    }
    // ---- 16. 同一个文件不许原地改写:走整文件重写,结果与原地改的逐字节相同 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kSeekTable, seek }, { flacmeta::kVorbisComment, vc("reference libFLAC 1.3.2 20170101", lost) },
                                                   { flacmeta::kPicture, picture() }, { flacmeta::kPadding, zeros(4096) } });
        const std::wstring f = mk(L"span_limit.flac", orig);
        FixOptions o;
        o.sectorBytes = 0;
        const FixReport r = fixFile(f, o);
        check(L"16", msg::T16, r.outcome == Outcome::Fixed && !r.inPlace && readFile(f) == fixedOnce && !fixedOnce.empty(), r.why);
    }

    // ---- 17. 原地改写只在改动落在同一个扇区里时用(写到一半断电也不会留下半新半旧的元数据区)----
    {
        // 定位表的长短决定注释块落在文件的哪里:25 个定位点 → 注释块从 496 字节处开始,改动跨过 512 这条扇区边界;
        // 30 个 → 从 586 处开始,改动全在 512–1023 这一个扇区里
        const auto seekOf = [](int points) {                 // 占位的定位点:采样号全 1,其余为 0
            Bytes s((size_t)points * 18, 0);
            for (int p = 0; p < points; ++p)
                for (int i = 0; i < 8; ++i) s[(size_t)p * 18 + (size_t)i] = 0xFF;
            return s;
        };
        const auto build = [&](int points) {
            return flac(p441, 44100, 24, { { flacmeta::kSeekTable, seekOf(points) }, { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) },
                                           { flacmeta::kPadding, zeros(2000) } });
        };
        const Bytes crossing = build(25), inside = build(30);
        const std::wstring fc = mk(L"sector_crossing.flac", crossing), fi = mk(L"sector_inside.flac", inside);
        FixOptions dry;
        dry.dryRun = true;
        const FixReport dc = fixFile(fc, dry), di = fixFile(fi, dry);
        const FixReport rc = fixFile(fc, def), ri = fixFile(fi, def);
        const bool okC = rc.outcome == Outcome::Fixed && !rc.inPlace && !dc.inPlace && repaired(fc, crossing, wantOurs352, 352800, why) &&
                         readFile(fc).size() == crossing.size() && !anyTemp(fc);
        const bool okI = ri.outcome == Outcome::Fixed && ri.inPlace && di.inPlace && repaired(fi, inside, wantOurs352, 352800, why);
        check(L"17.1", msg::T17_1, okC && okI, join(msg::of(rc.inPlace ? msg::ModeInPlace : msg::ModeRewrite), join(msg::of(ri.inPlace ? msg::ModeInPlace : msg::ModeRewrite), why)));
        // 压缩文件(NTFS 的压缩属性):文件里的字节与磁盘扇区对不上,同样"全在一个扇区里"的改动也不原地改
        const std::wstring fz = mk(L"sector_inside_compressed.flac", inside);
        bool compressed = false;
        {
            const HANDLE h = CreateFileW(fz.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            USHORT format = COMPRESSION_FORMAT_DEFAULT;
            DWORD got = 0;
            if (h != INVALID_HANDLE_VALUE) {
                compressed = DeviceIoControl(h, FSCTL_SET_COMPRESSION, &format, sizeof format, nullptr, 0, &got, nullptr) != 0;
                CloseHandle(h);
            }
        }
        const FixReport rz = fixFile(fz, def);
        const bool okZ = rz.outcome == Outcome::Fixed && repaired(fz, inside, wantOurs352, 352800, why) && (!compressed || !rz.inPlace);
        check(L"17.2", msg::T17_2, okZ, compressed ? msg::of(rz.inPlace ? msg::ModeInPlace : msg::ModeRewrite) : msg::of(msg::TestNoCompression));
    }
    // ---- 18. 临时文件:名字每次新起,不盖掉旁边已有的东西;上次被打断留下的才清掉 ----
    {
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        const std::wstring f = mk(L"stale_temps.flac", orig);
        const Bytes foreign = { 'n', 'o', 't', ' ', 'o', 'u', 'r', 's' };
        const std::wstring oldStyle = mk(L"stale_temps.flac.tagfix.tmp", foreign);                      // 名字不是我们的格式:不碰
        const std::wstring notOurs = mk(L"stale_temps.flac.tagfix.0123abcd.tmp", foreign);              // 名字像,内容不是这个文件的拷贝:不碰
        const std::wstring cut = mk(L"stale_temps.flac.tagfix.89abcdef.tmp", Bytes(orig.begin(), orig.begin() + 5000));   // 没写完的拷贝:清掉
        const std::wstring whole = mk(L"stale_temps.flac.tagfix.00000001.tmp", orig);                   // 写完了没换入的:清掉
        const std::wstring held = mk(L"stale_temps.flac.tagfix.7777aaaa.tmp", Bytes(orig.begin(), orig.begin() + 5000));  // 有人开着:不碰
        const std::wstring other = mk(L"another.flac.tagfix.89abcdef.tmp", Bytes(orig.begin(), orig.begin() + 5000));     // 别的文件的:不碰
        const HANDLE holder = CreateFileW(held.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        const FixReport r = fixFile(f, def);
        CloseHandle(holder);
        const bool ok = r.outcome == Outcome::Fixed && !r.inPlace && r.staleRemoved == 2 && repaired(f, orig, wantOurs352, 352800, why) &&
                        readFile(oldStyle) == foreign && readFile(notOurs) == foreign && !fileExists(cut) && !fileExists(whole) && fileExists(held) &&
                        fileExists(other);
        check(L"18", msg::T18, ok, join(msg::num(r.staleRemoved), join(r.why, why)));
    }
    // ---- 19. 换一个要写的值(--encoder-tag)、换认可的前缀(--accept-prefix)----
    {
        Rule custom;
        custom.encoderTag = "Lab=7, bench tag repair (unsigned)";          // 带等号:原样写进去、原样读回
        const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f = mk(L"custom_value.flac", orig);
        FixOptions o;
        o.rule = custom;
        const FixReport r = fixFile(f, o);
        const std::string want[kKeys] = { custom.encoderTag, custom.encoderTag, "352800" };
        const bool ok = encoderTagProblem(custom.encoderTag) == nullptr && r.outcome == Outcome::Fixed && repaired(f, orig, want, 352800, why, custom);
        const Bytes after = readFile(f);
        const FixReport r2 = fixFile(f, o);
        Scan sd;
        scanFile(f, sd);                                                    // 默认规则看这个文件:值合不合格按默认的前缀判
        const bool defaultSays = sd.state == (encoderValueOk(custom.encoderTag) ? State::Complete : State::Repairable);
        check(L"19.1", msg::T19_1, ok && r2.outcome == Outcome::Unchanged && readFile(f) == after && defaultSays, join(r.why, join(why, stateMsg(sd))));

        Rule anyValue;
        anyValue.acceptPrefix.clear();                                      // 只要不是空值就合格
        const std::vector<std::string> items = { "ENCODER=Lavf58.76.100", "MQAENCODER=", "TITLE=T" };
        const Bytes orig2 = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", items) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring g = mk(L"accept_any.flac", orig2);
        FixOptions o2;
        o2.rule = anyValue;
        Scan sa;
        scanFile(g, sa, anyValue);
        const FixReport r3 = fixFile(g, o2);
        const std::string want2[kKeys] = { "Lavf58.76.100", kOurs, "352800" };
        check(L"19.2", msg::T19_2,
              sa.state == State::Repairable && sa.bad == (kBadFormatEncoder | kBadOrigRate) && r3.outcome == Outcome::Fixed &&
                  r3.wrote == (kBadFormatEncoder | kBadOrigRate) && repaired(g, orig2, want2, 352800, why, anyValue),
              join(r3.why, why));
    }
    // ---- 20. 标签完整的规则:采样率合格 + 两个编码器键至少一个合格。完整的文件一个字节都不动 ----
    {
        const std::string rate = "ORIGINALSAMPLERATE=352800";
        const struct {
            const wchar_t* name;
            std::vector<std::string> items;
        } complete[4] = {
            { L"two_tags_only.flac", { "TITLE=T", "ENCODER=" + kOfficial, rate } },                                 // 早期官方文件:没有 MQAENCODER
            { L"format_key_only.flac", { "TITLE=T", "MQAENCODER=" + kOfficial, rate } },                            // 没有 ENCODER
            { L"lavf_beside_good.flac", { "encoder=Lavf58.76.100", "MQAENCODER=" + kOfficial, rate } },             // ENCODER 是别的值
            { L"duplicate_other_key.flac", { "ENCODER=" + kOfficial, "MQAENCODER=x", "MQAENCODER=y", rate } },      // 另一个键重复、值也不对
        };
        bool all = true;
        msg::Text notes;
        for (const auto& c : complete) {
            const Bytes orig = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", c.items) }, { flacmeta::kPadding, zeros(600) } });
            const std::wstring f = mk(c.name, orig);
            Scan s;
            scanFile(f, s);
            FixOptions dry;
            dry.dryRun = true;
            const FixReport rd = fixFile(f, dry), r = fixFile(f, def);
            const bool ok = s.state == State::Complete && s.bad != 0 && tagsComplete(s.bad) && !s.canRepair && rd.outcome == Outcome::Unchanged &&
                            r.outcome == Outcome::Unchanged && readFile(f) == orig && !anyTemp(f);
            all = all && ok;
            notes = join(notes, msg::Text(std::wstring(c.name) + L": ") + stateMsg(s));
        }
        check(L"20.1", msg::T20_1, all, notes);

        // 不完整的:两个编码器键都不合格(采样率是对的);采样率不对(ENCODER 是官方的)—— 修的时候三个键都补到合格,原来合格的留着
        const Bytes o1 = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "ENCODER=Lavf58.76.100", rate }) }, { flacmeta::kPadding, zeros(600) } });
        const std::wstring f1 = mk(L"no_good_encoder.flac", o1);
        Scan s1;
        scanFile(f1, s1);
        const FixReport r1 = fixFile(f1, def);
        const bool ok1 = s1.state == State::Repairable && s1.bad == (kBadEncoder | kBadFormatEncoder) && r1.outcome == Outcome::Fixed &&
                         r1.wrote == (kBadEncoder | kBadFormatEncoder) && repaired(f1, o1, wantOurs352, 352800, why);
        const Bytes o2 = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "ENCODER=" + kOfficial, "ORIGINALSAMPLERATE=96000" }) },
                                                 { flacmeta::kPadding, zeros(600) } });
        const std::wstring f2 = mk(L"good_encoder_wrong_rate.flac", o2);
        Scan s2;
        scanFile(f2, s2);
        const FixReport r2 = fixFile(f2, def);
        const std::string want2[kKeys] = { kOfficial, kOurs, "352800" };
        const bool ok2 = s2.state == State::Repairable && s2.bad == (kBadFormatEncoder | kBadOrigRate) && r2.outcome == Outcome::Fixed &&
                         r2.wrote == (kBadFormatEncoder | kBadOrigRate) && repaired(f2, o2, want2, 352800, why);
        check(L"20.2", msg::T20_2, ok1 && ok2, join(stateMsg(s1), join(stateMsg(s2), join(r1.why, join(r2.why, why)))));
    }
    // ---- 21. 取消:每个阶段取消之后,文件要么没动过、要么完整修好,不留临时文件 ----
    {
        const Bytes withPad = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) }, { flacmeta::kPadding, zeros(600) } });
        const Bytes noPad = flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
        int serial = 0;
        // 在 phase 这一步取消,修 bytes 这个文件;返回报告,log 里是走过的阶段
        const auto run = [&](const Bytes& bytes, const char* phase, PhaseLog& log, std::wstring& path, const std::function<void(FixOptions&)>& more = nullptr) {
            path = mk(L"cancel_" + std::to_wstring(++serial) + L".flac", bytes);
            FixOptions o;
            log.stopAt = phase;
            o.test.phase = log.hook();
            if (more) more(o);
            runctl::resetCancel();
            const FixReport r = fixFile(path, o);
            runctl::resetCancel();
            return r;
        };
        // 21.1 还没写任何东西的阶段:两条路(原地 / 整文件重写)各试一遍
        bool early = true;
        msg::Text notes;
        for (const Bytes* bytes : { &withPad, &noPad })
            for (const char* phase : { "scan", "lock", "hash", "decode" }) {
                PhaseLog log;
                std::wstring f;
                const FixReport r = run(*bytes, phase, log, f);
                const bool ok = r.outcome == Outcome::Cancelled && r.code == "cancelled" && log.endsWith(phase) && readFile(f) == *bytes && !anyTemp(f);
                early = early && ok;
                if (!ok) notes = join(notes, msg::Text(wu::wide(phase) + L": ") + r.why);
            }
        {
            // 解码解到一半(进度报到 10%–50% 之间)时取消
            PhaseLog log;
            std::wstring f;
            const FixReport r = run(withPad, "", log, f, [](FixOptions& o) {
                o.progress = [](double done) {
                    if (done > 0.1 && done < 0.5) runctl::requestCancel();
                };
            });
            const bool ok = r.outcome == Outcome::Cancelled && log.endsWith("decode") && readFile(f) == withPad && !anyTemp(f);
            early = early && ok;
            if (!ok) notes = join(notes, r.why);
        }
        check(L"21.1", msg::T21_1, early, notes);
        // 21.2 原地写的那一下:做完
        {
            PhaseLog log;
            std::wstring f;
            const FixReport r = run(withPad, "write", log, f);
            check(L"21.2", msg::T21_2, r.outcome == Outcome::Fixed && r.inPlace && log.endsWith("write") && repaired(f, withPad, wantOurs352, 352800, why) && !anyTemp(f),
                  join(r.why, why));
        }
        // 21.3 整文件重写:建临时文件之前 / 往临时文件里拷的时候 / 核对临时文件之前 / 核对到一半 —— 都是立刻停、删掉临时文件
        {
            bool all = true;
            msg::Text n3;
            for (int how = 0; how < 4; ++how) {
                PhaseLog log;
                std::wstring f;
                const FixReport r = run(noPad, how == 0 ? "temp" : how == 2 ? "verify" : "", log, f, [how](FixOptions& o) {
                    if (how == 1) o.test.duringTempWrite = [] { runctl::requestCancel(); };
                    if (how == 3) o.progress = [](double done) {
                        if (done > 0.6 && done < 0.95) runctl::requestCancel();
                    };
                });
                const bool ok = r.outcome == Outcome::Cancelled && readFile(f) == noPad && !anyTemp(f) && !log.has("swap") &&
                                (how == 0 ? log.endsWith("temp") : how == 1 ? log.endsWith("temp") && !log.has("verify") : log.endsWith("verify"));
                all = all && ok;
                if (!ok) n3 = join(n3, msg::Text(std::to_wstring(how) + L": ") + r.why);
            }
            check(L"21.3", msg::T21_3, all, n3);
        }
        // 21.4 换入已经开始:做完
        {
            PhaseLog log;
            std::wstring f;
            const FixReport r = run(noPad, "swap", log, f);
            check(L"21.4", msg::T21_4, r.outcome == Outcome::Fixed && !r.inPlace && repaired(f, noPad, wantOurs352, 352800, why) && !anyTemp(f), join(r.why, why));
        }
        // 21.5 开始之前就已经取消
        {
            const std::wstring f = mk(L"cancel_before.flac", withPad);
            PhaseLog log;
            FixOptions o;
            o.test.phase = log.hook();
            runctl::requestCancel();
            const FixReport r = fixFile(f, o);
            runctl::resetCancel();
            check(L"21.5", msg::T21_5, r.outcome == Outcome::Cancelled && log.seen == std::vector<std::string>{ "scan" } && readFile(f) == withPad && !anyTemp(f),
                  r.why);
        }
    }
    // ---- 22. WAV → FLAC ----
    {
        CopyOptions cdef;
        // 22.1 / 22.2 四种 WAV:16 / 24 bit × 44.1 / 48 kHz
        bool made1 = true, same2 = true;
        msg::Text n1, n2;
        const struct { int rate, bits, orig; uint32_t seed; } kinds[4] = { { 44100, 16, 88200, 61 }, { 44100, 24, 352800, 62 }, { 48000, 16, 96000, 63 },
                                                                           { 48000, 24, 192000, 64 } };
        for (const auto& k : kinds) {
            const std::vector<int32_t> lr = pcm(k.rate, k.bits, 2.0, 3000, k.orig, k.seed);
            const Bytes w = wav(lr, k.rate, k.bits);
            const std::wstring stem = L"marked_" + std::to_wstring(k.rate / 1000) + L"k_" + std::to_wstring(k.bits) + L"bit";
            const std::wstring f = mk(stem + L".wav", w), target = g_dir + L"\\" + stem + L".flac";
            Scan s;
            scanFile(f, s);
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, cdef);
            made(target);
            const bool ok1 = s.state == State::Convertible && s.canWavToFlac && s.canCopy24 == (k.bits == 16) && s.info.bits == k.bits &&
                             r.outcome == CopyOutcome::Created && wu::shownPath(r.target) == target && r.bits == k.bits &&
                             r.frames == lr.size() / 2 && readFile(f) == w && !anyTemp(target);
            made1 = made1 && ok1;
            if (!ok1) n1 = join(n1, join(stateMsg(s), r.why));
            // 新文件:文件头、逐个采样、扫描
            const Bytes nf = readFile(target);
            const Parsed p = parse(nf);
            const std::vector<int32_t> got = decodeSamples(target);
            Scan sn;
            scanFile(target, sn);
            const std::string want[kKeys] = { kOurs, kOurs, std::to_string(k.orig) };
            bool tags = p.ok;
            for (int i = 0; tags && i < kKeys; ++i) tags = p.seen.count[i] == 1 && p.seen.value[i] == want[i] && sn.info.has[i] && sn.info.value[i] == want[i];
            const bool ok2 = p.ok && p.lay.sampleRate == k.rate && p.lay.channels == 2 && p.lay.bits == k.bits && p.lay.totalSamples == lr.size() / 2 &&
                             got == leftJustified(lr, k.bits) && Bytes(p.lay.streamInfo + 18, p.lay.streamInfo + 34) == md5Of(lr, k.bits) &&
                             sn.state == State::Complete && sn.info.marker && sn.info.origRate == k.orig && tags && p.vcs.size() == 1 &&
                             p.vcs[0].vendor == flacmeta::newVendor();
            same2 = same2 && ok2;
            if (!ok2) n2 = join(n2, msg::Text(stem + L": ") + stateMsg(sn));
        }
        check(L"22.1", msg::T22_1, made1, n1);
        check(L"22.2", msg::T22_2, same2, n2);

        const std::vector<int32_t> lr24 = pcm(44100, 24, 2.0, 3000, 352800, 65);
        const Bytes w24 = wav(lr24, 44100, 24);
        // 22.3 普通标签带过去
        {
            const Bytes w = wavWithInfo(lr24, 44100, 24, { { "INAM", "合成曲线" }, { "IART", "Nobody" }, { "ISFT", "Some Editor 1.0" } });
            const std::wstring f = mk(L"with_info.wav", w), target = g_dir + L"\\with_info.flac";
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, cdef);
            made(target);
            const Parsed p = parse(readFile(target));
            bool title = false, artist = false, software = false;
            if (p.ok && p.vcs.size() == 1)
                for (const std::string& it : p.vcs[0].items) {
                    title = title || it == "TITLE=合成曲线";
                    artist = artist || it == "ARTIST=Nobody";
                    software = software || it.find("Some Editor") != std::string::npos;      // WAV 里的 encoder 不带过去:ENCODER 是我们写的
                }
            check(L"22.3", msg::T22_3,
                  r.outcome == CopyOutcome::Created && title && artist && !software && p.seen.count[kEncoder] == 1 && p.seen.value[kEncoder] == kOurs &&
                      p.seen.value[kFormatEncoder] == kOurs && p.seen.value[kOrigRate] == "352800" && readFile(f) == w && !anyTemp(target),
                  r.why);
        }
        // 22.4 目标已经存在:开始之前就拒绝
        {
            const std::wstring f = mk(L"target_exists.wav", w24);
            const Bytes there = { 'k', 'e', 'e', 'p', ' ', 'm', 'e' };
            const std::wstring target = mk(L"target_exists.flac", there);
            PhaseLog log;
            CopyOptions o;
            o.test.phase = log.hook();
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
            check(L"22.4", msg::T22_4,
                  r.outcome == CopyOutcome::Refused && r.code == "target_exists" && !log.has("encode") && readFile(target) == there && readFile(f) == w24 &&
                      !anyTemp(target),
                  r.why);
        }
        // 22.5 编码的时候目标名下冒出一个文件:改名那一步不覆盖
        {
            const std::wstring f = mk(L"target_appears.wav", w24), target = g_dir + L"\\target_appears.flac";
            const Bytes there = { 'n', 'o', 't', ' ', 'y', 'o', 'u', 'r', 's' };
            CopyOptions o;
            o.test.phase = [&](const char* name) {
                if (std::string(name) == "verify") writeFile(target, there);
            };
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
            made(target);
            check(L"22.5", msg::T22_5, r.outcome == CopyOutcome::Refused && r.code == "target_exists" && readFile(target) == there && !anyTemp(target), r.why);
        }
        // 22.6 不该转的一律不转
        {
            const std::wstring fp = mk(L"no_marker.wav", wav(plain, 44100, 24));
            const std::wstring ff = mk(L"marked_but_flac.flac", flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", {}) }, { flacmeta::kPadding, zeros(600) } }));
            std::vector<int32_t> mu = synth::noise(44100, 16, 5);
            for (int32_t& v : mu) v &= 0xFF;
            const std::wstring fm = mk(L"lossy.wav", wav(mu, 44100, 8, 7));
            const CopyReport a = copyFile(fp, CopyKind::WavToFlac, cdef), b = copyFile(ff, CopyKind::WavToFlac, cdef), c = copyFile(fm, CopyKind::WavToFlac, cdef),
                             d = copyFile(g_dir + L"\\not_there.wav", CopyKind::WavToFlac, cdef);
            check(L"22.6", msg::T22_6,
                  a.outcome == CopyOutcome::Skipped && a.code == "no_marker" && b.outcome == CopyOutcome::Skipped && c.outcome == CopyOutcome::Skipped &&
                      d.outcome == CopyOutcome::Skipped && d.code == "error" && !fileExists(g_dir + L"\\no_marker.flac") &&
                      !fileExists(g_dir + L"\\lossy.flac") && !fileExists(g_dir + L"\\not_there.flac") && !anyTemp(g_dir + L"\\no_marker.flac"),
                  join(a.why, join(b.why, join(c.why, d.why))));
        }
        // 22.7 故意弄坏的拷贝:核对拦住
        {
            const std::wstring f = mk(L"corrupted_copy.wav", w24), target = g_dir + L"\\corrupted_copy.flac";
            CopyOptions o;
            o.test.afterWrite = [](HANDLE h) {                // 音频区里改一个字节(离文件末尾 5000 字节处)
                LARGE_INTEGER sz{};
                GetFileSizeEx(h, &sz);
                std::vector<uint8_t> one;
                wu::readRange(h, (uint64_t)sz.QuadPart - 5000, 1, one);
                one[0] ^= 0x10;
                wu::writeAt(h, (uint64_t)sz.QuadPart - 5000, one.data(), 1);
            };
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
            check(L"22.7", msg::T22_7, r.outcome == CopyOutcome::Failed && r.code == "verify_failed" && !fileExists(target) && !anyTemp(target) && readFile(f) == w24,
                  r.why);
        }
        // 22.8 标签写错了的拷贝;一个采样不对、但帧校验对得上的拷贝(整个换成另一份合成的 FLAC,文件头里的 MD5 照抄对的那个)
        {
            const std::wstring f = mk(L"wrong_tag_copy.wav", w24), target = g_dir + L"\\wrong_tag_copy.flac";
            CopyOptions o;
            o.test.tags = [](std::string fin[kKeys]) { fin[kEncoder] = "Lavf58.76.100"; };     // 另一个编码器键还是对的:扫描仍说完整,靠逐键比对拦住
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
            CopyOptions o2;
            o2.test.tags = [](std::string fin[kKeys]) { fin[kOrigRate] = "96000"; };           // 采样率与码流不符:扫描就不是完整
            const CopyReport r2 = copyFile(f, CopyKind::WavToFlac, o2);
            std::vector<int32_t> off = lr24;
            off[40001] ^= 0x10;                                                                // 一个采样的一位
            const std::string ours3[3] = { std::string("ENCODER=") + kOurs, std::string("MQAENCODER=") + kOurs, "ORIGINALSAMPLERATE=352800" };
            Bytes fake = flac(off, 44100, 24, { { flacmeta::kVorbisComment, vc("v", { ours3[0], ours3[1], ours3[2] }) } });
            const Bytes rightMd5 = md5Of(lr24, 24);
            const Parsed pf = parse(fake);
            for (size_t i = 0; i < 16; ++i) fake[(size_t)pf.lay.metaAt + 4 + 18 + i] = rightMd5[i];
            CopyOptions o3;
            o3.test.afterWrite = [&](HANDLE h) {
                wu::writeAt(h, 0, fake.data(), fake.size());
                LARGE_INTEGER to{};
                to.QuadPart = (LONGLONG)fake.size();
                SetFilePointerEx(h, to, nullptr, FILE_BEGIN);
                SetEndOfFile(h);
            };
            const CopyReport r3 = copyFile(f, CopyKind::WavToFlac, o3);
            const std::wstring samplesDiffer = msg::of(msg::CvSamples).zh, tagsWrong = msg::of(msg::CvTags).zh, notComplete = msg::of(msg::CvScan).zh;
            check(L"22.8", msg::T22_8,
                  r.outcome == CopyOutcome::Failed && r.code == "verify_failed" && r.why.zh.find(tagsWrong) != std::wstring::npos &&
                      r2.outcome == CopyOutcome::Failed && r2.why.zh.find(notComplete) != std::wstring::npos && r3.outcome == CopyOutcome::Failed &&
                      r3.code == "verify_failed" && r3.why.zh.find(samplesDiffer) != std::wstring::npos && pf.ok && !fileExists(target) && !anyTemp(target),
                  join(r.why, join(r2.why, r3.why)));
        }
        // 22.9 取消:编码时 / 核对之前 / 核对到一半 —— 立刻停、什么都不留;改名那一步已经开始 —— 做完
        {
            bool all = true;
            msg::Text n9;
            for (int how = 0; how < 4; ++how) {
                const std::wstring stem = L"cancel_copy_" + std::to_wstring(how);
                const std::wstring f = mk(stem + L".wav", w24), target = g_dir + L"\\" + stem + L".flac";
                PhaseLog log;
                CopyOptions o;
                log.stopAt = how == 1 ? "verify" : how == 3 ? "rename" : "";
                o.test.phase = log.hook();
                if (how == 0) o.test.duringEncode = [] { runctl::requestCancel(); };
                if (how == 2) o.progress = [](double done) {
                    if (done > 0.6 && done < 0.9) runctl::requestCancel();
                };
                runctl::resetCancel();
                const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
                runctl::resetCancel();
                made(target);
                const bool ok = how == 3 ? r.outcome == CopyOutcome::Created && fileExists(target) && !anyTemp(target)
                                         : r.outcome == CopyOutcome::Cancelled && r.code == "cancelled" && !fileExists(target) && !anyTemp(target) &&
                                               !log.has("rename") && (how == 0 ? log.endsWith("encode") && !log.has("verify") : log.endsWith("verify"));
                all = all && ok && readFile(f) == w24;
                if (!ok) n9 = join(n9, msg::Text(std::to_wstring(how) + L": ") + r.why);
            }
            check(L"22.9", msg::T22_9, all, n9);
        }
        // 22.10 试运行;原文件正被别的程序写着
        {
            const std::wstring f = mk(L"dry_copy.wav", w24), target = g_dir + L"\\dry_copy.flac";
            CopyOptions o;
            o.dryRun = true;
            const CopyReport r = copyFile(f, CopyKind::WavToFlac, o);
            const HANDLE writer = CreateFileW(f.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                              0, nullptr);
            const CopyReport r2 = copyFile(f, CopyKind::WavToFlac, cdef);
            CloseHandle(writer);
            check(L"22.10", msg::T22_10,
                  r.outcome == CopyOutcome::WouldCreate && wu::shownPath(r.target) == target && r2.outcome == CopyOutcome::Refused && r2.code == "in_use" &&
                      !fileExists(target) && !anyTemp(target) && readFile(f) == w24,
                  join(r.why, r2.why));
        }
    }
    // ---- 23. 16 bit → 24 bit 的拷贝 ----
    {
        CopyOptions cdef;
        const std::vector<int32_t> lr16 = pcm(44100, 16, 2.0, 2000, 88200, 71);
        std::vector<int32_t> want24(lr16.size());             // 每个采样左移 8 位(右对齐的 24 bit 数),再左对齐成 int32
        for (size_t i = 0; i < lr16.size(); ++i) want24[i] = (int32_t)((uint32_t)(lr16[i] * 256) << 8);
        const std::string wantTags[kKeys] = { kOurs, kOurs, "88200" };
        // 23.1 / 23.2 16 bit 的 FLAC(标签没修过,带一张图)
        {
            const Bytes orig = flac(lr16, 44100, 16, { { flacmeta::kSeekTable, seek }, { flacmeta::kVorbisComment, vc("some ripper", { "TITLE=Disc Track", "encoder=Lavf58.76.100" }) },
                                                       { flacmeta::kPicture, picture() }, { flacmeta::kPadding, zeros(600) } });
            const std::wstring f = mk(L"disc_16bit.flac", orig), target = g_dir + L"\\disc_16bit.24bit.flac";
            Scan s;
            scanFile(f, s);
            const CopyReport r = copyFile(f, CopyKind::To24Bit, cdef);
            made(target);
            const Parsed p = parse(readFile(target));
            const std::vector<int32_t> got = decodeSamples(target);
            bool lowZero = !got.empty(), highSame = got.size() == lr16.size();
            for (size_t i = 0; i < got.size() && highSame; ++i) {
                const int32_t v24 = got[i] >> 8;              // 新文件里的 24 bit 数
                lowZero = lowZero && (v24 & 0xFF) == 0;
                highSame = (v24 >> 8) == lr16[i];
            }
            check(L"23.1", msg::T23_1,
                  s.state == State::Repairable && s.canCopy24 && s.info.bits == 16 && r.outcome == CopyOutcome::Created &&
                      wu::shownPath(r.target) == target && r.bits == 24 && p.ok && p.lay.bits == 24 && p.lay.sampleRate == 44100 &&
                      p.lay.totalSamples == lr16.size() / 2 && lowZero && highSame && got == want24 && readFile(f) == orig && !anyTemp(target),
                  join(stateMsg(s), r.why));
            Scan sn;
            scanFile(target, sn);
            bool tags = p.ok, title = false, picture1 = false, seekCarried = false;
            for (int i = 0; tags && i < kKeys; ++i) tags = p.seen.count[i] == 1 && p.seen.value[i] == wantTags[i];
            if (p.ok && !p.vcs.empty())
                for (const std::string& it : p.vcs[0].items) title = title || it == "TITLE=Disc Track";
            for (const flacmeta::MetaBlock& b : p.blocks) {
                picture1 = picture1 || (b.type == flacmeta::kPicture && b.body == picture());
                seekCarried = seekCarried || b.type == flacmeta::kSeekTable;
            }
            Scan again;
            scanFile(f, again);
            check(L"23.2", msg::T23_2,
                  sn.state == State::Complete && sn.info.marker && sn.info.origRate == 88200 && sn.info.bits == 24 && !sn.canCopy24 && tags && title &&
                      picture1 && !seekCarried && Bytes(p.lay.streamInfo + 18, p.lay.streamInfo + 34) == md5Of(std::vector<int32_t>([&] {
                          std::vector<int32_t> v(lr16.size());
                          for (size_t i = 0; i < v.size(); ++i) v[i] = lr16[i] * 256;
                          return v;
                      }()), 24) &&
                      again.state == State::Repairable,
                  stateMsg(sn));
        }
        // 23.3 16 bit 的 WAV;24 bit 的不另存;目标已经存在
        {
            const Bytes w = wav(lr16, 44100, 16);
            const std::wstring f = mk(L"disc_16bit_wav.wav", w), target = g_dir + L"\\disc_16bit_wav.24bit.flac";
            const CopyReport r = copyFile(f, CopyKind::To24Bit, cdef);
            made(target);
            const std::vector<int32_t> got = decodeSamples(target);
            Scan sn;
            scanFile(target, sn);
            const std::wstring f24 = mk(L"already_24bit.flac", flac(p441, 44100, 24, { { flacmeta::kVorbisComment, vc("v", {}) }, { flacmeta::kPadding, zeros(600) } }));
            const CopyReport r24 = copyFile(f24, CopyKind::To24Bit, cdef);
            const CopyReport again = copyFile(f, CopyKind::To24Bit, cdef);                     // 第二次:目标已经在了
            const Bytes kept = readFile(target);
            check(L"23.3", msg::T23_3,
                  r.outcome == CopyOutcome::Created && got == want24 && sn.state == State::Complete && sn.info.bits == 24 && sn.info.origRate == 88200 &&
                      readFile(f) == w && r24.outcome == CopyOutcome::Skipped && r24.code == "not_16bit" &&
                      !fileExists(g_dir + L"\\already_24bit.24bit.flac") && again.outcome == CopyOutcome::Refused && again.code == "target_exists" &&
                      !kept.empty() && !anyTemp(target),
                  join(r.why, join(r24.why, again.why)));
        }
        // 23.4 标签完整的 16 bit FLAC:拷贝里的标签就是原来那些合格的值;没修过的:拷贝是完整的,原文件还是没修过的样子
        {
            const std::vector<std::string> official = { "TITLE=T", "ENCODER=" + kOfficial, "ORIGINALSAMPLERATE=88200" };
            const Bytes orig = flac(lr16, 44100, 16, { { flacmeta::kVorbisComment, vc("v", official) }, { flacmeta::kPadding, zeros(600) } });
            const std::wstring f = mk(L"complete_16bit.flac", orig), target = g_dir + L"\\complete_16bit.24bit.flac";
            Scan s;
            scanFile(f, s);
            const CopyReport r = copyFile(f, CopyKind::To24Bit, cdef);
            made(target);
            const Parsed p = parse(readFile(target));
            Scan sn;
            scanFile(target, sn);
            const Bytes bare = flac(lr16, 44100, 16, { { flacmeta::kVorbisComment, vc("v", { "TITLE=T" }) } });
            const std::wstring g = mk(L"unrepaired_16bit.flac", bare), target2 = g_dir + L"\\unrepaired_16bit.24bit.flac";
            const CopyReport r2 = copyFile(g, CopyKind::To24Bit, cdef);
            made(target2);
            Scan s2, sn2;
            scanFile(g, s2);
            scanFile(target2, sn2);
            check(L"23.4", msg::T23_4,
                  s.state == State::Complete && s.canCopy24 && r.outcome == CopyOutcome::Created && p.ok && p.seen.value[kEncoder] == kOfficial &&
                      p.seen.value[kFormatEncoder] == kOurs && p.seen.value[kOrigRate] == "88200" && sn.state == State::Complete && readFile(f) == orig &&
                      r2.outcome == CopyOutcome::Created && sn2.state == State::Complete && s2.state == State::Repairable && readFile(g) == bare,
                  join(r.why, r2.why));
        }
    }

    if (!keepFiles) {
        for (const std::wstring& p : g_made) {
            SetFileAttributesW(wu::longPath(p).c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(wu::longPath(p).c_str());
        }
        RemoveDirectoryW(g_dir.c_str());
    }
    wu::outLine(L"");
    wu::outLine(L"---- " + msg::of(msg::TestSummary, { msg::num(g_pass), msg::num(g_fail) }).both() + L" ----");
    return g_fail ? 1 : 0;
}
