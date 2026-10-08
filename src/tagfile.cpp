#include "tagfile.h"

#include "runctl.h"
#include "win_util.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace tagrepair {

using fileutil::Handle;
using fileutil::TempFile;

namespace {

HANDLE openShared(const std::wstring& longPath) {
    return CreateFileW(longPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}

// 独占打开(谁都不许同时开着)。杀毒 / 索引器扫一下就走的那种占用等一小会儿;一直开不了就是真被占着
HANDLE openExclusive(const std::wstring& longPath, DWORD& err) {
    for (int attempt = 0;; ++attempt) {
        const HANDLE h = CreateFileW(longPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { err = 0; return h; }
        err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION || attempt >= 4) return INVALID_HANDLE_VALUE;
        Sleep(200);
    }
}

// 元数据区里的注释块全部解出来
bool commentsOf(const std::vector<uint8_t>& meta, std::vector<flacmeta::Comments>& vcs) {
    std::vector<flacmeta::MetaBlock> blocks;
    if (!flacmeta::splitBlocks(meta.data(), meta.size(), blocks)) return false;
    for (const flacmeta::MetaBlock& b : blocks) {
        if (b.type != flacmeta::kVorbisComment) continue;
        flacmeta::Comments c;
        if (!flacmeta::parseComments(b.body.data(), b.body.size(), c)) return false;
        vcs.push_back(std::move(c));
    }
    return true;
}

// FFmpeg 读回的值对不对:相等;FLAC 前面有 ID3v2 时,FFmpeg 会把 ID3v2 里同名的帧接在前面("旧值;我们的值")
bool ffValueMatches(const std::string& got, const std::string& want, bool hasId3) {
    if (got == want) return true;
    return hasId3 && got.size() > want.size() + 1 && got.compare(got.size() - want.size() - 1, std::string::npos, ";" + want) == 0;
}

msg::Text keyText(int k) { return msg::Text(wu::wide(keyName(k))); }
msg::Text sysError(DWORD code) { return wu::errorText(code); }

// 写完之后的文件应该是什么样
struct Expect {
    uint64_t metaAt = 0, audioAt = 0, audioBytes = 0;
    uint8_t streamInfo[34] = {};
    const std::vector<uint8_t>* prefix = nullptr;        // 文件开头到第一个块头之前(ID3v2 + "fLaC")
    const std::vector<uint8_t>* meta = nullptr;          // 整段元数据
    const Rule* rule = nullptr;
    std::string fin[kKeys];
    int origRate = 0;
    std::string audioSha, pcmMd5;
    uint64_t pcmFrames = 0;
    int pcmErrors = 0;                                   // 修之前解码器报了几次错(末尾有杂字节时会有;修之后要一样)
};

// 这个文件做到哪了、要不要停、自检的阶段记号
struct Steps {
    const FixOptions& opt;
    void phase(const char* name) const {
        if (opt.test.phase) opt.test.phase(name);
    }
    void at(double f) const {
        if (opt.progress) opt.progress(f);
    }
    static bool cancelled() { return runctl::cancelRequested(); }
};

// 核对写完的文件。stoppable = 核对的时候看到取消就停(只在核对临时文件时;原地写完之后的核对不停),停了 stopped = true。
// 进度报在 [from, to] 这一段
bool verify(HANDLE h, const std::wstring& name, const Expect& e, Seen& seenOut, msg::Text& why, const Steps& steps, bool stoppable, double from, double to,
            bool& stopped) {
    stopped = false;
    const wu::FileSource src(h);
    flacmeta::Layout l;
    const msg::Entry* w = nullptr;
    if (flacmeta::readLayout(src, l, w) != flacmeta::Err::Ok) { why = msg::of(msg::VerLayout, { w ? msg::of(*w) : msg::Text() }); return false; }
    if (l.metaAt != e.metaAt || l.audioAt != e.audioAt) { why = msg::of(msg::VerAudioStart); return false; }
    if (l.fileSize - l.audioAt != e.audioBytes) { why = msg::of(msg::VerAudioLength); return false; }
    if (std::memcmp(l.streamInfo, e.streamInfo, 34) != 0) { why = msg::of(msg::VerStreamInfo); return false; }
    std::vector<uint8_t> got;
    if (!wu::readRange(h, 0, (size_t)l.metaAt, got) || got != *e.prefix) { why = msg::of(msg::VerPrefix); return false; }
    if (!wu::readRange(h, l.metaAt, (size_t)(l.audioAt - l.metaAt), got) || got != *e.meta) { why = msg::of(msg::VerMeta); return false; }
    if (!flacmeta::seenInMeta(got.data(), got.size(), seenOut) || problems(seenOut, e.origRate, *e.rule) != 0) { why = msg::of(msg::VerTags); return false; }
    for (int k = 0; k < kKeys; ++k)
        if (seenOut.value[k] != e.fin[k]) { why = msg::of(msg::VerTag, { keyText(k) }); return false; }
    std::string sha;
    if (!wu::hashRange(h, l.audioAt, l.fileSize, sha) || sha != e.audioSha) { why = msg::of(msg::VerSha); return false; }
    probe::Info info;
    if (!probe::inspect(h, name, info) || !info.marker || info.origRate != e.origRate) { why = msg::of(msg::VerNoMarker); return false; }
    for (int k = 0; k < kKeys; ++k)
        if (!info.has[k] || !ffValueMatches(info.value[k], e.fin[k], l.id3Bytes > 0)) {
            why = msg::of(msg::VerFfTag, { keyText(k) });
            return false;
        }
    probe::Pcm pcm;
    msg::Text err;
    probe::Watch watch;
    watch.tick = [&](double f) {
        steps.at(from + (to - from) * f);
        return !(stoppable && Steps::cancelled());
    };
    const bool decoded = probe::decodeAll(h, name, pcm, err, &watch);
    if (pcm.cancelled) { stopped = true; return false; }
    if (!decoded || pcm.errors != e.pcmErrors || pcm.md5 != e.pcmMd5 || pcm.frames != e.pcmFrames) {
        why = msg::of(msg::VerPcm);
        return false;
    }
    return true;
}

// 把 [at, end) 拷进临时文件。每拷一段看一次取消:看到就停(stopped = true),调用方删掉临时文件
bool copyRange(HANDLE from, uint64_t at, uint64_t end, HANDLE to, uint64_t toAt, const Steps& steps, bool& stopped) {
    std::vector<uint8_t> buf(1u << 20);
    const wu::FileSource src(from);
    stopped = false;
    while (at < end) {
        const size_t k = (size_t)(end - at < buf.size() ? end - at : buf.size());
        if (!src.read(at, buf.data(), k) || !wu::writeAt(to, toAt, buf.data(), k)) return false;
        at += k;
        toAt += k;
        if (steps.opt.test.duringTempWrite) steps.opt.test.duringTempWrite();
        if (Steps::cancelled()) {
            stopped = true;
            return false;
        }
    }
    return true;
}

const wchar_t kTempMark[] = L".tagfix.";
const wchar_t kTempExt[] = L".tmp";

bool isHex8(const std::wstring& s) {
    if (s.size() != 8) return false;
    for (wchar_t c : s)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    return true;
}

// 上次修这个文件时被打断(进程被杀 / 断电)留下的临时文件。三条都满足才删:
//   名字正好是 <这个文件的名字>.tagfix.<8 位十六进制>.tmp;现在没有任何程序开着它;
//   它的内容是这个文件的一份(可能没写完的)拷贝 —— 开头到 fLaC 的字节、STREAMINFO 与这个文件的相同。
// 别的东西(哪怕名字像)一律不碰。调用时原文件已经被我们独占着,所以不会有另一个本程序正在用这些临时文件
int removeStaleTemps(const std::wstring& path, const std::vector<uint8_t>& prefix, const uint8_t streamInfo[34]) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return 0;
    const std::wstring dir = path.substr(0, slash + 1), base = path.substr(slash + 1);
    WIN32_FIND_DATAW fd{};
    const HANDLE find = FindFirstFileW((path + kTempMark + L"*" + kTempExt).c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;
    std::vector<std::wstring> names;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && isTempNameFor(base, fd.cFileName)) names.push_back(fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    int removed = 0;
    for (const std::wstring& n : names) {
        const std::wstring p = dir + n;
        bool ours = false;
        {
            Handle t;
            t.h = CreateFileW(p.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);       // 有人开着就开不了:不碰
            LARGE_INTEGER sz{};
            if (!t.ok() || !GetFileSizeEx(t.h, &sz)) continue;
            const uint64_t size = (uint64_t)sz.QuadPart, siAt = prefix.size() + 4;
            std::vector<uint8_t> head;
            const size_t n1 = (size_t)std::min<uint64_t>(size, prefix.size());
            ours = wu::readRange(t.h, 0, n1, head) && std::equal(head.begin(), head.end(), prefix.begin());
            if (ours && size >= siAt + 34) ours = wu::readRange(t.h, siAt, 34, head) && std::memcmp(head.data(), streamInfo, 34) == 0;
        }
        if (ours && DeleteFileW(p.c_str())) ++removed;
    }
    return removed;
}

std::wstring backupName(const std::wstring& path) {
    std::wstring b = path + L".bak";
    for (int i = 2; fileutil::exists(b) && i < 1000; ++i) b = path + L".bak" + std::to_wstring(i);
    return b;
}

FixReport refuse(FixReport& r, Outcome o, const char* code, const msg::Text& why) {
    r.outcome = o;
    r.code = code;
    r.why = why;
    r.reason = why.zh;
    return r;
}

// 自检的钩子说"这次写入当作失败"
bool writeAllowed(const FixOptions& opt) {
    if (!opt.test.writeOk || opt.test.writeOk()) return true;
    SetLastError(ERROR_DISK_FULL);
    return false;
}

} // namespace

namespace fileutil {

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool createTemp(const std::wstring& path, TempFile& tmp, DWORD& err) {
    static unsigned counter = 0;
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    unsigned seed = (unsigned)GetCurrentProcessId() * 2654435761u ^ (unsigned)qpc.LowPart ^ (++counter * 40503u);
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
    for (int form = 0; form < 2; ++form) {                    // 0:<文件名>.tagfix.<8 位十六进制>.tmp;1(文件名太长时):~tagfix.<…>.tmp
        for (int i = 0; i < 64; ++i, seed += 0x9E3779B9u) {
            wchar_t hex[16];
            _snwprintf_s(hex, _TRUNCATE, L"%08x", seed);
            const std::wstring p = form == 0 ? path + kTempMark + hex + kTempExt : dir + L"~tagfix." + hex + kTempExt;
            const HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                tmp.path = p;
                tmp.f.h = h;
                err = 0;
                return true;
            }
            err = GetLastError();
            if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) break;
        }
        if (err != ERROR_INVALID_NAME && err != ERROR_FILENAME_EXCED_RANGE && err != ERROR_PATH_NOT_FOUND) return false;
    }
    return false;
}

} // namespace fileutil

bool isTempNameFor(const std::wstring& baseName, const std::wstring& candidateName) {
    const size_t markLen = wcslen(kTempMark), extLen = wcslen(kTempExt);
    if (candidateName.size() != baseName.size() + markLen + 8 + extLen) return false;
    if (_wcsnicmp(candidateName.c_str(), baseName.c_str(), baseName.size()) != 0) return false;
    if (candidateName.compare(baseName.size(), markLen, kTempMark) != 0) return false;
    if (candidateName.compare(candidateName.size() - extLen, extLen, kTempExt) != 0) return false;
    return isHex8(candidateName.substr(baseName.size() + markLen, 8));
}

const char* outcomeCode(Outcome o) {
    switch (o) {
    case Outcome::Fixed:     return "fixed";
    case Outcome::Unchanged: return "unchanged";
    case Outcome::WouldFix:  return "would_fix";
    case Outcome::Skipped:   return "skipped";
    case Outcome::Refused:   return "refused";
    case Outcome::Failed:    return "failed";
    case Outcome::Cancelled: return "cancelled";
    }
    return "failed";
}

msg::Text keyList(unsigned mask) {
    msg::Text t;
    for (int k = 0; k < kKeys; ++k)
        if (mask & (1u << k)) {
            const std::wstring name = wu::wide(keyName(k));
            t.en += (t.en.empty() ? L"" : L", ") + name;
            t.zh += (t.zh.empty() ? L"" : L"、") + name;
        }
    return t;
}

msg::Text stateMsg(const Scan& s) {
    switch (s.state) {
    case State::Error:       return msg::of(msg::StError, { s.info.error.empty() ? msg::of(msg::WhyUnreadable) : s.info.error });
    case State::NotLossless: return msg::of(msg::StNotLossless, { wu::wide(s.info.codec) });
    case State::NoMarker:    return msg::of(msg::StNoMarker);
    case State::Complete:    return msg::of(msg::StComplete);
    case State::Repairable:
        return s.bad == (kBadEncoder | kBadFormatEncoder | kBadOrigRate) && !s.seen.count[0] && !s.seen.count[1] && !s.seen.count[2]
                   ? msg::of(msg::StRepairMissing)
                   : msg::of(msg::StRepairIncomplete, { keyList(s.bad) });
    case State::RateUnknown: return msg::of(msg::StRateUnknown);
    case State::Convertible: return msg::of(msg::StConvertible);
    case State::Unsupported:
        if (s.info.container == "wav")
            return s.fin[kOrigRate].empty() && (s.info.bits == 16 || s.info.bits == 24) ? msg::of(msg::StWavNoRate)
                                                                                         : msg::of(msg::StWavFormat, { wu::wide(s.info.codec) });
        return msg::of(msg::StUnsupported, { wu::wide(s.info.container) });
    case State::Damaged:     return msg::of(msg::StDamaged, { s.flacWhy ? msg::of(*s.flacWhy) : msg::Text() });
    }
    return msg::Text();
}

bool scanHandle(HANDLE file, const std::wstring& name, Scan& out, const Rule& rule) {
    out = Scan();
    probe::inspect(file, name, out.info);
    Facts f;
    f.opened = out.info.opened;
    f.losslessPcm = out.info.losslessPcm;
    f.marker = out.info.marker;
    out.nativeFlac = out.info.opened && out.info.container == "flac";
    f.nativeFlac = out.nativeFlac;
    if (out.nativeFlac) {
        const wu::FileSource src(file);
        std::vector<uint8_t> meta;
        std::vector<flacmeta::Comments> vcs;
        if (flacmeta::readLayout(src, out.layout, out.flacWhy) != flacmeta::Err::Ok) {
        } else if (!wu::readRange(file, out.layout.metaAt, (size_t)(out.layout.audioAt - out.layout.metaAt), meta)) {
            out.flacWhy = &msg::LayMetaUnreadable;
        } else if (!commentsOf(meta, vcs)) {
            out.flacWhy = &msg::LayBadComment;
        } else {
            out.flacOk = true;
            out.seen = flacmeta::seenIn(vcs);
            flacmeta::chooseFinal(vcs, out.info.origRate, out.fin, rule);
        }
        f.flacDamaged = !out.flacOk;
    } else {
        // 别的容器:三个键只有 FFmpeg 的元数据字典看得到。另存时写什么:字典里已有合格的就留它,否则用默认的值 / 码流里的采样率
        for (int k = 0; k < kKeys; ++k) {
            out.seen.count[k] = out.info.has[k] ? 1 : 0;
            out.seen.value[k] = out.info.value[k];
            if (out.info.has[k] && firstAcceptable(k, out.info.value[k], out.info.origRate, rule, out.fin[k])) continue;
            if (k != kOrigRate) out.fin[k] = rule.encoderTag;
            else if (out.info.origRate > 0) out.fin[k] = std::to_string(out.info.origRate);
        }
        f.wavPcm = out.info.container == "wav" && (out.info.codec == "pcm_s16le" || out.info.codec == "pcm_s24le") &&
                   (out.info.bits == 16 || out.info.bits == 24);
    }
    out.bad = problems(out.seen, out.info.origRate, rule);
    f.bad = out.bad;
    f.rateAvailable = !out.fin[kOrigRate].empty();
    out.state = classify(f);
    out.canRepair = out.state == State::Repairable;
    out.canWavToFlac = out.state == State::Convertible;
    out.canCopy24 = out.info.marker && out.info.bits == 16 &&
                    (out.state == State::Convertible || (out.flacOk && (out.state == State::Complete || out.state == State::Repairable)));
    return out.info.opened;
}

bool scanFile(const std::wstring& path, Scan& out, const Rule& rule) {
    out = Scan();
    const std::wstring lp = wu::longPath(path);
    Handle f;
    f.h = openShared(lp);
    if (!f.ok()) {
        out.openError = GetLastError();
        out.info.error = out.openError == ERROR_SHARING_VIOLATION ? msg::of(msg::WhyInUse) : wu::errorText(out.openError);
        return false;
    }
    return scanHandle(f.h, wu::fileName(lp), out, rule);
}

FixReport fixFile(const std::wstring& pathIn, const FixOptions& opt) {
    FixReport r;
    const runctl::Busy busy;                                  // 关窗口的处理函数等这个文件收尾(析构在最后:临时文件已经删了)
    const Steps steps{ opt };
    const std::wstring path = wu::longPath(pathIn);
    const std::wstring name = wu::fileName(path);
    const auto stop = [&](const msg::Entry& why) { return refuse(r, Outcome::Cancelled, "cancelled", msg::of(why)); };

    // ---- 1. 先不加锁看一眼:不是能修的那一类就到此为止,不碰它 ----
    steps.phase("scan");
    if (Steps::cancelled()) return stop(msg::FixCancelled);
    scanFile(path, r.scan, opt.rule);
    if (r.scan.openError == ERROR_SHARING_VIOLATION) return refuse(r, Outcome::Refused, "in_use", msg::of(msg::FixInUse));
    if (r.scan.state == State::Complete) return refuse(r, Outcome::Unchanged, "", msg::of(msg::FixComplete));
    if (r.scan.state != State::Repairable) return refuse(r, Outcome::Skipped, stateCode(r.scan.state), stateMsg(r.scan));
    steps.at(0.05);

    // ---- 2. 独占打开;之后的判断、核对、写入全在这把锁里 ----
    steps.phase("lock");
    if (Steps::cancelled()) return stop(msg::FixCancelled);
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) return refuse(r, Outcome::Refused, "read_only", msg::of(msg::FixReadOnly));
    Handle f;
    DWORD oerr = 0;
    f.h = openExclusive(path, oerr);
    if (!f.ok()) {
        if (oerr == ERROR_SHARING_VIOLATION || oerr == ERROR_LOCK_VIOLATION) return refuse(r, Outcome::Refused, "in_use", msg::of(msg::FixInUse));
        if (oerr == ERROR_ACCESS_DENIED) return refuse(r, Outcome::Refused, "access_denied", msg::of(msg::FixAccessDenied));
        return refuse(r, Outcome::Refused, "open_failed", msg::of(msg::FixOpenFailed, { sysError(oerr) }));
    }
    scanHandle(f.h, name, r.scan, opt.rule);                  // 拿到锁之后再判一遍(中间可能被别人改过)
    if (r.scan.state == State::Complete) return refuse(r, Outcome::Unchanged, "", msg::of(msg::FixComplete));
    if (r.scan.state != State::Repairable) return refuse(r, Outcome::Skipped, stateCode(r.scan.state), stateMsg(r.scan));
    const flacmeta::Layout lay = r.scan.layout;

    // ---- 3. 算出修好之后的元数据 ----
    std::vector<uint8_t> oldMeta, prefix;
    if (!wu::readRange(f.h, lay.metaAt, (size_t)(lay.audioAt - lay.metaAt), oldMeta) || !wu::readRange(f.h, 0, (size_t)lay.metaAt, prefix))
        return refuse(r, Outcome::Failed, "io", msg::of(msg::FixReadFailed));
    flacmeta::Plan plan;
    switch (flacmeta::planFix(oldMeta, r.scan.fin, lay.metaAt, opt.sectorBytes, plan)) {
    case flacmeta::PlanErr::Ok: break;
    case flacmeta::PlanErr::TooBig:
        return refuse(r, Outcome::Refused, "too_big", msg::of(msg::FixTooBig));
    case flacmeta::PlanErr::BadMeta:
    case flacmeta::PlanErr::BadComment:
        return refuse(r, Outcome::Refused, "damaged", msg::of(msg::FixMetaDamaged));
    default:
        return refuse(r, Outcome::Failed, "internal", msg::of(msg::FixInternalLength));
    }
    if (plan.mode == flacmeta::Mode::NoChange) return refuse(r, Outcome::Unchanged, "", msg::of(msg::FixComplete));
    if (plan.mode == flacmeta::Mode::InPlace) {
        // 原地改写不留后路,所以只在"改动落在同一个扇区里"时用(planFix 已经判过)。压缩 / 加密 / 稀疏文件的字节
        // 与磁盘扇区对不上,要留备份的本来就得有两份 —— 这几种一律整文件重写后换入
        BY_HANDLE_FILE_INFORMATION bi{};
        const bool plainFile = GetFileInformationByHandle(f.h, &bi) &&
                               !(bi.dwFileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_SPARSE_FILE));
        if (opt.keepBackup || !plainFile) plan.mode = flacmeta::Mode::Rewrite;
    }
    r.wrote = r.scan.bad;
    r.inPlace = plan.mode == flacmeta::Mode::InPlace;

    // ---- 4. 修之前的两个指纹:音频字节的 SHA-256、FFmpeg 整轨解出的样本 MD5 ----
    Expect e;
    e.metaAt = lay.metaAt;
    e.audioAt = lay.metaAt + plan.newMeta.size();
    e.audioBytes = lay.fileSize - lay.audioAt;
    std::memcpy(e.streamInfo, lay.streamInfo, 34);
    e.prefix = &prefix;
    e.meta = &plan.newMeta;
    e.rule = &opt.rule;
    for (int k = 0; k < kKeys; ++k) e.fin[k] = r.scan.fin[k];
    e.origRate = r.scan.info.origRate;
    steps.phase("hash");
    if (Steps::cancelled()) return stop(msg::FixCancelled);
    if (!wu::hashRange(f.h, lay.audioAt, lay.fileSize, e.audioSha)) return refuse(r, Outcome::Failed, "io", msg::of(msg::FixReadFailed));
    steps.at(0.1);
    steps.phase("decode");
    if (Steps::cancelled()) return stop(msg::FixCancelled);
    probe::Pcm pcm;
    msg::Text derr;
    {
        probe::Watch watch;
        watch.tick = [&](double done) {
            steps.at(0.1 + 0.4 * done);
            return !Steps::cancelled();
        };
        const bool decoded = probe::decodeAll(f.h, name, pcm, derr, &watch);
        if (pcm.cancelled) return stop(msg::FixCancelled);
        if (!decoded) return refuse(r, Outcome::Refused, "decode_error", msg::of(msg::FixDecodeFailed, { derr }));
    }
    // 音频要完整:文件头写了总采样数的,解出来的要正好这么多(帧 CRC 不对的帧解码器不出样本,数目就对不上;
    // 最后一帧之后的杂字节 —— 比如 ID3v1 —— 解码器会报一次错,但样本一个不少,这种不算);没写总数的,不许有任何解码错
    if (lay.totalSamples > 0 ? pcm.frames != lay.totalSamples : pcm.errors != 0) {
        if (lay.totalSamples > 0)
            return refuse(r, Outcome::Refused, pcm.errors ? "decode_error" : "incomplete_audio",
                          msg::of(pcm.errors ? msg::FixAudioBadFrames : msg::FixAudioShort,
                                  { msg::num((long long)pcm.frames), msg::num((long long)lay.totalSamples), msg::num(pcm.errors) }));
        return refuse(r, Outcome::Refused, "decode_error", msg::of(msg::FixDecodeErrors, { msg::num(pcm.errors) }));
    }
    e.pcmMd5 = pcm.md5;
    e.pcmFrames = pcm.frames;
    e.pcmErrors = pcm.errors;
    r.audioSha256 = e.audioSha;
    r.audioBytes = e.audioBytes;
    r.pcmMd5 = pcm.md5;
    r.pcmFrames = pcm.frames;
    if (opt.test.expect) opt.test.expect(e.audioSha, e.pcmMd5);

    // ---- 5. 写之前:要写的字节先用自己的解析器在内存里过一遍 ----
    {
        std::vector<uint8_t> head(prefix);
        head.insert(head.end(), plan.newMeta.begin(), plan.newMeta.end());
        head.push_back(0xFF);                                 // 后面接的是音频帧的同步码
        head.push_back(0xF8);
        const flacmeta::MemSource ms(head.data(), head.size());
        flacmeta::Layout nl;
        const msg::Entry* w = nullptr;
        Seen s;
        if (flacmeta::readLayout(ms, nl, w) != flacmeta::Err::Ok || nl.audioAt != e.audioAt || std::memcmp(nl.streamInfo, lay.streamInfo, 34) != 0 ||
            !flacmeta::seenInMeta(plan.newMeta.data(), plan.newMeta.size(), s) || problems(s, e.origRate, opt.rule) != 0 ||
            (plan.sameSize && e.audioAt != lay.audioAt))
            return refuse(r, Outcome::Failed, "internal", msg::of(msg::FixInternalCheck));
    }
    if (opt.dryRun) return refuse(r, Outcome::WouldFix, "", msg::of(msg::FixWouldFix, { keyList(r.wrote) }));

    bool stopped = false;
    if (r.inPlace) {
        // ---- 6a. 原地改写:只写变了的那一段(在同一个扇区里);读回不对就写回去 ----
        if (Steps::cancelled()) return stop(msg::FixCancelled);     // 最后一个能停的地方
        steps.phase("write");                                 // 从这里起不看取消:写、读回核对、对不上就写回去,做完这个文件
        const uint64_t at = lay.metaAt + plan.diffAt;
        const size_t n = plan.diffEnd - plan.diffAt;
        const bool wrote = writeAllowed(opt) && wu::writeAt(f.h, at, plan.newMeta.data() + plan.diffAt, n) && FlushFileBuffers(f.h);
        const DWORD werr = wrote ? 0 : GetLastError();
        if (wrote && opt.test.afterWrite) opt.test.afterWrite(f.h);
        msg::Text why;
        if (!wrote || !verify(f.h, name, e, r.after, why, steps, false, 0.55, 1.0, stopped)) {
            std::vector<uint8_t> back;
            const bool restored = wu::writeAt(f.h, lay.metaAt, oldMeta.data(), oldMeta.size()) && FlushFileBuffers(f.h) &&
                                  wu::readRange(f.h, lay.metaAt, oldMeta.size(), back) && back == oldMeta;
            const msg::Text tail = msg::of(restored ? msg::FixTailRestored : msg::FixTailNotRestored);
            if (!wrote) return refuse(r, Outcome::Failed, "io", msg::of(msg::FixWriteFailed, { sysError(werr) }) + tail);
            return refuse(r, Outcome::Failed, "verify_failed", why + tail);
        }
        steps.at(1.0);
        r.outcome = Outcome::Fixed;
        return r;
    }

    // ---- 6b. 整文件重写:旁边写一个完整的新文件 → 核对 → 换入 ----
    steps.phase("temp");
    if (Steps::cancelled()) return stop(msg::FixCancelled);
    FILETIME created{}, accessed{}, written{};
    const bool haveTimes = GetFileTime(f.h, &created, &accessed, &written) != 0;
    r.staleRemoved = removeStaleTemps(path, prefix, lay.streamInfo);
    TempFile tmp;
    DWORD terr = 0;
    if (!fileutil::createTemp(path, tmp, terr)) return refuse(r, Outcome::Failed, "io", msg::of(msg::FixTempCreate, { sysError(terr) }));
    if (!(writeAllowed(opt) && wu::writeAt(tmp.f.h, 0, prefix.data(), prefix.size()) &&
          wu::writeAt(tmp.f.h, prefix.size(), plan.newMeta.data(), plan.newMeta.size()) &&
          copyRange(f.h, lay.audioAt, lay.fileSize, tmp.f.h, e.audioAt, steps, stopped) && FlushFileBuffers(tmp.f.h))) {
        if (stopped) return stop(msg::FixCancelledTemp);      // 临时文件写到一半:删掉(TempFile 析构),原文件没动过
        const msg::Text werr = sysError(GetLastError());
        return refuse(r, Outcome::Failed, "io", msg::of(msg::FixTempWrite, { werr }));
    }
    steps.at(0.6);
    if (opt.test.afterTempWrite) opt.test.afterTempWrite(tmp.f.h);
    steps.phase("verify");
    if (Steps::cancelled()) return stop(msg::FixCancelledTemp);
    msg::Text why;
    if (!verify(tmp.f.h, name, e, r.after, why, steps, true, 0.6, 0.95, stopped)) {
        if (stopped) return stop(msg::FixCancelledTemp);
        return refuse(r, Outcome::Failed, "verify_failed", why + msg::of(msg::FixTailTempGone));
    }
    if (Steps::cancelled()) return stop(msg::FixCancelledTemp);     // 最后一个能停的地方
    steps.phase("swap");                                      // 从这里起不看取消:换入是一步到位的,做完这个文件

    // 换入要按名字重新开原文件,独占的这一把得先放掉。放掉之后马上换一把"别人只能读"的拿着,直到换完:
    // 这段时间别的程序改不了原文件。两把之间有一个极短的空当,所以拿到之后核对一遍 —— 大小、修改时间、
    // 开头到音频帧之前的字节都还是刚才读的那样才往下走。(临时文件此刻还被我们独占着,别人动不了它)
    f.close();
    if (opt.test.beforeSwap) opt.test.beforeSwap();
    Handle guard;
    guard.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (!guard.ok()) {
        const DWORD ge = GetLastError();
        if (ge == ERROR_SHARING_VIOLATION) return refuse(r, Outcome::Refused, "in_use", msg::of(msg::FixInUse));
        return refuse(r, Outcome::Failed, "io", msg::of(msg::FixGuardOpen, { sysError(ge) }));
    }
    auto unchanged = [&](HANDLE h) -> bool {                  // 原文件还是独占时读到的那个样子
        LARGE_INTEGER sz{};
        FILETIME w2{};
        std::vector<uint8_t> head;
        return GetFileSizeEx(h, &sz) && (uint64_t)sz.QuadPart == lay.fileSize &&
               (!haveTimes || (GetFileTime(h, nullptr, nullptr, &w2) && CompareFileTime(&w2, &written) == 0)) &&
               wu::readRange(h, 0, (size_t)lay.audioAt, head) && head.size() == prefix.size() + oldMeta.size() &&
               std::equal(prefix.begin(), prefix.end(), head.begin()) &&
               std::equal(oldMeta.begin(), oldMeta.end(), head.begin() + (std::ptrdiff_t)prefix.size());
    };
    if (!unchanged(guard.h)) return refuse(r, Outcome::Refused, "changed", msg::of(msg::FixChanged));
    tmp.f.close();

    auto doReplace = [&](const wchar_t* backup, DWORD& err) -> bool {
        if (opt.test.replaceFile) return opt.test.replaceFile(path.c_str(), tmp.path.c_str(), backup, err);
        if (ReplaceFileW(path.c_str(), tmp.path.c_str(), backup, 0, nullptr, nullptr)) { err = 0; return true; }
        err = GetLastError();
        return false;
    };
    auto doMove = [&](const std::wstring& from, const std::wstring& to, DWORD flags, DWORD& err) -> bool {
        if (opt.test.moveFile) return opt.test.moveFile(from.c_str(), to.c_str(), flags, err);
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) { err = 0; return true; }
        err = GetLastError();
        return false;
    };
    if (opt.keepBackup) r.backup = backupName(path);
    DWORD serr = 0;
    bool madeCopy = false;                                    // 备份是我们自己用 CopyFileW 拷出来的(只在下面的退路里)
    bool swapped = doReplace(opt.keepBackup ? r.backup.c_str() : nullptr, serr);
    if (!swapped && serr != ERROR_INVALID_FUNCTION && serr != ERROR_NOT_SUPPORTED && fileutil::exists(path)) {
        // 没换成、原文件还在原名下。有的文件系统 / 旧系统上,原文件哪怕只被我们自己这一把开着也换不进去:
        // 放掉这一把,再核对一遍它没变,立刻再换一次(这一次与换入之间没有东西拿着它,空当极短)
        guard.close();
        Handle look;
        look.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        const bool same = look.ok() && unchanged(look.h);
        look.close();
        if (same) swapped = doReplace(opt.keepBackup ? r.backup.c_str() : nullptr, serr);
    }
    if (!swapped && (serr == ERROR_INVALID_FUNCTION || serr == ERROR_NOT_SUPPORTED)) {
        // 这个盘的文件系统不支持 ReplaceFile:退回"改名覆盖"。改名覆盖要求没有人开着原文件,拿着的那一把先放掉
        guard.close();
        if (opt.keepBackup) {
            madeCopy = CopyFileW(path.c_str(), r.backup.c_str(), TRUE) != 0;
            if (!madeCopy) serr = GetLastError();
        }
        if (!opt.keepBackup || madeCopy) swapped = doMove(tmp.path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH, serr);
    }
    guard.close();
    const msg::Text swapErr = sysError(serr);
    if (!swapped && !fileutil::exists(path)) {
        // 换入做到一半:原来的名字下已经没有文件了(ReplaceFileW 的 1176 / 1177)。从这里起什么都不许删,先把名字补回去。
        // 新文件是完整核对过的,让它顶上;挡着它的多半是扫一下就走的杀毒 / 索引器,所以隔一小会儿多试几次
        DWORD e2 = 0;
        for (int i = 0; i < 5 && !swapped; ++i) {
            if (fileutil::exists(tmp.path) && doMove(tmp.path, path, MOVEFILE_WRITE_THROUGH, e2)) swapped = true;
            else Sleep(150);
        }
        if (!swapped) {
            // 新文件进不来。原文件要是在备份名下,把它改回原名;也改不回去,就原样留着并说清楚它在哪
            const bool haveBackup = !r.backup.empty() && fileutil::exists(r.backup);
            bool restored = false;
            for (int i = 0; haveBackup && i < 5 && !restored; ++i) {
                if (doMove(r.backup, path, MOVEFILE_WRITE_THROUGH, e2)) restored = true;
                else Sleep(150);
            }
            if (restored) {
                r.backup.clear();
                return refuse(r, Outcome::Failed, "swap_failed", msg::of(msg::FixSwapRestored, { swapErr }));
            }
            if (haveBackup) {
                r.leftAt = r.backup;                          // 原文件完好,只是名字变了;新文件不留(析构时删)
                return refuse(r, Outcome::Failed, "swap_incomplete", msg::of(msg::FixSwapAtBackup, { swapErr, wu::shownPath(r.backup), name }));
            }
            r.backup.clear();
            if (!fileutil::exists(tmp.path)) return refuse(r, Outcome::Failed, "swap_incomplete", msg::of(msg::FixSwapLost, { swapErr }));
            tmp.keep = true;                                  // 它是这首歌现在仅有的一份:不删
            r.leftAt = tmp.path;
            return refuse(r, Outcome::Failed, "swap_incomplete", msg::of(msg::FixSwapAtTemp, { swapErr, wu::shownPath(tmp.path), name }));
        }
    }
    if (!swapped) {
        // 原来的名字下的文件还在:换入没有发生,它就是原文件
        if (madeCopy) DeleteFileW(r.backup.c_str());
        r.backup.clear();
        return refuse(r, Outcome::Failed, "swap_failed", msg::of(msg::FixSwapFailed, { swapErr }));
    }
    tmp.keep = true;                                          // 已经改名成正式文件了
    // 换入之后再看一眼:大小、开头到音频帧之前的字节、建立时间
    {
        Handle g;
        g.h = CreateFileW(path.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, 0, nullptr);
        std::vector<uint8_t> head;
        LARGE_INTEGER sz{};
        const bool good = g.ok() && GetFileSizeEx(g.h, &sz) && (uint64_t)sz.QuadPart == e.audioAt + e.audioBytes &&
                          wu::readRange(g.h, e.metaAt, plan.newMeta.size(), head) && head == plan.newMeta;
        if (!good) return refuse(r, Outcome::Failed, "verify_failed", msg::of(msg::FixAfterSwapDiffers));
        FILETIME c2{};
        if (haveTimes && GetFileTime(g.h, &c2, nullptr, nullptr) && CompareFileTime(&c2, &created) != 0) SetFileTime(g.h, &created, nullptr, nullptr);
    }
    steps.at(1.0);
    r.outcome = Outcome::Fixed;
    return r;
}

} // namespace tagrepair
