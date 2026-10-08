#include "copy_flac.h"

#include "runctl.h"
#include "win_util.h"

#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace tagrepair {

using fileutil::Handle;
using fileutil::TempFile;

namespace {

constexpr size_t kNewPadding = 2048;                          // 新文件里留一块 PADDING:以后改标签不用整文件重写

// 值转成 UTF-8:本来就是合法的 UTF-8 原样用;不是的话按系统的 ANSI 代码页转(WAV 的 INFO 块按惯例就是这个编码);
// 两样都不是的不带过去(不往新文件里写乱码)
bool toUtf8Value(const std::string& v, std::string& out) {
    if (validUtf8(v)) { out = v; return true; }
    const int n = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, v.data(), (int)v.size(), nullptr, 0);
    if (n <= 0) return false;
    std::wstring w((size_t)n, L'\0');
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, v.data(), (int)v.size(), &w[0], n) != n) return false;
    out = wu::utf8(w);
    return !out.empty();
}

// 新文件里 STREAMINFO 之后的块:一个注释块(厂商串是我们的;源里的普通标签带过去,三个识别标签各一份、值为 fin),
// 源是 FLAC 时把它的图片、CUE 表原样带上(定位表、应用块不带:它们说的是原来那份编码),最后一块 PADDING
bool buildMeta(HANDLE src, const Scan& scan, const std::string fin[kKeys], std::vector<uint8_t>& out) {
    flacmeta::Comments c;
    c.vendor = flacmeta::newVendor();
    std::vector<flacmeta::MetaBlock> carried;
    if (scan.nativeFlac) {
        std::vector<uint8_t> meta;
        std::vector<flacmeta::MetaBlock> blocks;
        if (!scan.flacOk || !wu::readRange(src, scan.layout.metaAt, (size_t)(scan.layout.audioAt - scan.layout.metaAt), meta) ||
            !flacmeta::splitBlocks(meta.data(), meta.size(), blocks))
            return false;
        for (flacmeta::MetaBlock& b : blocks) {
            if (b.type == flacmeta::kVorbisComment) {
                flacmeta::Comments one;
                if (!flacmeta::parseComments(b.body.data(), b.body.size(), one)) return false;
                c.items.insert(c.items.end(), one.items.begin(), one.items.end());
            } else if (b.type == flacmeta::kPicture || b.type == flacmeta::kCueSheet) {
                carried.push_back(std::move(b));
            }
        }
    } else {
        for (const auto& kv : scan.info.meta) {
            const std::string key = commentKeyFor(kv.first);
            std::string value;
            if (key.empty() || kv.second.empty() || !toUtf8Value(kv.second, value)) continue;
            c.items.push_back(key + "=" + value);
        }
    }
    std::vector<flacmeta::Comments> vcs(1, c);
    flacmeta::applyTags(vcs, fin);
    std::vector<flacmeta::MetaBlock> blocks(1);
    blocks[0].type = flacmeta::kVorbisComment;
    blocks[0].body = flacmeta::buildComments(vcs[0]);
    if (blocks[0].body.size() > flacmeta::kMaxBlockBody) return false;
    for (flacmeta::MetaBlock& b : carried) blocks.push_back(std::move(b));
    flacmeta::MetaBlock pad;
    pad.type = flacmeta::kPadding;
    pad.body.assign(kNewPadding, 0);
    blocks.push_back(std::move(pad));
    out = flacmeta::joinBlocks(blocks);
    return true;
}

// 新文件应该是什么样
struct Want {
    int rate = 0, bits = 0, padBits = 0, origRate = 0;
    uint64_t frames = 0;
    std::string srcMd5, srcNativeMd5;
    const uint8_t* headerMd5 = nullptr;
    const std::string* fin = nullptr;
    const Rule* rule = nullptr;
};

// 把写好的临时文件从头到尾核对一遍。对不上:why = 哪一条。看到取消就停:stopped = true
bool verifyCopy(HANDLE h, const std::wstring& name, const Want& w, const CopyOptions& opt, const msg::Entry*& why, bool& stopped) {
    stopped = false;
    why = nullptr;
    const auto no = [&](const msg::Entry& e) {
        why = &e;
        return false;
    };
    // 文件头:自己的解析器走得通;采样率、声道数、位深、采样个数、MD5 都是该有的
    const wu::FileSource src(h);
    flacmeta::Layout lay;
    const msg::Entry* layWhy = nullptr;
    if (flacmeta::readLayout(src, lay, layWhy) != flacmeta::Err::Ok) return no(msg::CvLayout);
    if (lay.sampleRate != w.rate || lay.channels != 2 || lay.bits != w.bits) return no(msg::CvFormat);
    if (lay.totalSamples != w.frames) return no(msg::CvCount);
    if (std::memcmp(lay.streamInfo + 18, w.headerMd5, 16) != 0) return no(msg::CvHeaderMd5);
    // 扫描:有同步标记、原始采样率与源相同、标签完整;三个标签各一份、值就是要写的(自己的解析器与 FFmpeg 各读一遍)
    Scan s;
    scanHandle(h, name, s, *w.rule);
    if (s.state != State::Complete || !s.info.marker || s.info.origRate != w.origRate || s.info.bits != w.bits) return no(msg::CvScan);
    for (int k = 0; k < kKeys; ++k)
        if (s.seen.count[k] != 1 || s.seen.value[k] != w.fin[k] || !s.info.has[k] || s.info.value[k] != w.fin[k]) return no(msg::CvTags);
    // 整个解一遍:采样个数相同、全部采样的 MD5 与源相同;按新位深排出来的 MD5 就是文件头里写的那个。
    // 24 bit 的拷贝逐个采样看:低 8 位是零,高 16 位(右移 8 位)排出来的 MD5 与源按 16 bit 排的相同
    wu::Hash native(BCRYPT_MD5_ALGORITHM), high16(BCRYPT_MD5_ALGORITHM);
    if (!native.ok() || !high16.ok()) return no(msg::CvDecode);
    bool lowBitsZero = true;
    std::vector<uint8_t> raw, rawHigh;
    probe::Watch watch;
    watch.sink = [&](const int32_t* lr, size_t samples) {
        raw.clear();
        rawHigh.clear();
        for (size_t i = 0; i < samples; ++i) {
            const int32_t v = lr[i] >> (32 - w.bits);                 // 新文件里的那个数(右对齐)
            for (int b = 0; b < w.bits / 8; ++b) raw.push_back((uint8_t)((uint32_t)v >> (8 * b)));
            if (w.padBits) {
                if ((uint32_t)v & ((1u << w.padBits) - 1)) lowBitsZero = false;
                const int32_t hi = v >> w.padBits;
                rawHigh.push_back((uint8_t)hi);
                rawHigh.push_back((uint8_t)((uint32_t)hi >> 8));
            }
        }
        native.update(raw.data(), raw.size());
        if (w.padBits) high16.update(rawHigh.data(), rawHigh.size());
    };
    watch.tick = [&](double f) {
        if (opt.progress) opt.progress(0.55 + 0.4 * f);
        return !runctl::cancelRequested();
    };
    probe::Pcm pcm;
    msg::Text err;
    const bool decoded = probe::decodeAll(h, name, pcm, err, &watch);
    if (pcm.cancelled) { stopped = true; return false; }
    if (!decoded || pcm.errors != 0) return no(msg::CvDecode);
    if (pcm.frames != w.frames) return no(msg::CvCount);
    if (w.padBits && (!lowBitsZero || high16.hex() != w.srcNativeMd5)) return no(msg::CvLowBits);
    if (pcm.md5 != w.srcMd5) return no(msg::CvSamples);
    uint8_t md5[16];
    if (!native.raw(md5, 16) || std::memcmp(md5, lay.streamInfo + 18, 16) != 0) return no(msg::CvHeaderMd5);
    return true;
}

} // namespace

const char* copyOutcomeCode(CopyOutcome o) {
    switch (o) {
    case CopyOutcome::Created:     return "created";
    case CopyOutcome::WouldCreate: return "would_create";
    case CopyOutcome::Skipped:     return "skipped";
    case CopyOutcome::Refused:     return "refused";
    case CopyOutcome::Failed:      return "failed";
    case CopyOutcome::Cancelled:   return "cancelled";
    }
    return "failed";
}

CopyReport copyFile(const std::wstring& pathIn, CopyKind kind, const CopyOptions& opt) {
    CopyReport r;
    const runctl::Busy busy;                                  // 关窗口的处理函数等这个文件收尾(析构在最后:临时文件已经删了)
    const auto done = [&](CopyOutcome o, const char* code, const msg::Text& why) {
        r.outcome = o;
        r.code = code;
        r.why = why;
        return r;
    };
    const auto phase = [&](const char* name) {
        if (opt.test.phase) opt.test.phase(name);
    };
    const auto at = [&](double f) {
        if (opt.progress) opt.progress(f);
    };
    const auto stop = [&]() { return done(CopyOutcome::Cancelled, "cancelled", msg::of(msg::CopyCancelled)); };
    const std::wstring path = wu::longPath(pathIn), name = wu::fileName(path);

    // ---- 1. 源:只读打开(我们读的时候别人可以读、不许写),看它是哪一类 ----
    phase("scan");
    if (runctl::cancelRequested()) return stop();
    Handle src;
    src.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (!src.ok()) {
        r.scan.openError = GetLastError();
        if (r.scan.openError == ERROR_SHARING_VIOLATION) return done(CopyOutcome::Refused, "in_use", msg::of(msg::CopySourceInUse));
        r.scan.info.error = wu::errorText(r.scan.openError);
    } else {
        scanHandle(src.h, name, r.scan, opt.rule);
    }
    if (kind == CopyKind::WavToFlac && !r.scan.canWavToFlac)
        return done(CopyOutcome::Skipped, stateCode(r.scan.state), msg::of(msg::CopySkipNotWav, { stateMsg(r.scan) }));
    if (kind == CopyKind::To24Bit && !r.scan.canCopy24)
        return done(CopyOutcome::Skipped, r.scan.info.marker && r.scan.info.bits == 16 ? stateCode(r.scan.state) : "not_16bit",
                    msg::of(msg::CopySkipNot16, { stateMsg(r.scan) }));
    std::string write[kKeys];
    for (int k = 0; k < kKeys; ++k) r.fin[k] = write[k] = r.scan.fin[k];
    if (opt.test.tags) opt.test.tags(write);
    r.bits = kind == CopyKind::To24Bit ? 24 : r.scan.info.bits;

    // ---- 2. 目标:已有的文件从不覆盖 ----
    r.target = copyTargetPath(path, kind);
    const msg::Text targetName(wu::fileName(r.target));
    if (fileutil::exists(r.target)) return done(CopyOutcome::Refused, "target_exists", msg::of(msg::CopyTargetExists, { targetName }));
    if (opt.dryRun) return done(CopyOutcome::WouldCreate, "", msg::of(msg::CopyWould, { targetName }));
    std::vector<uint8_t> meta;
    if (!buildMeta(src.h, r.scan, write, meta)) return done(CopyOutcome::Failed, "io", msg::of(msg::CopyEncode));
    at(0.05);

    // ---- 3. 编码进旁边的临时文件 ----
    phase("encode");
    if (runctl::cancelRequested()) return stop();
    TempFile tmp;
    DWORD terr = 0;
    if (!fileutil::createTemp(r.target, tmp, terr)) return done(CopyOutcome::Failed, "io", msg::of(msg::CopyTempCreate, { wu::errorText(terr) }));
    probe::FlacWrite spec;
    spec.outBits = r.bits;
    spec.padBits = kind == CopyKind::To24Bit ? kPadTo24 : 0;
    spec.metaAfterStreamInfo = meta;
    probe::FlacWritten wr;
    {
        probe::Watch watch;
        watch.tick = [&](double f) {
            if (opt.test.duringEncode) opt.test.duringEncode();
            if (runctl::cancelRequested()) return false;      // 取消:立刻停,临时文件删掉(TempFile 析构)
            at(0.05 + 0.5 * f);
            return true;
        };
        if (!probe::writeFlacCopy(src.h, name, tmp.f.h, spec, wr, &watch)) {
            switch (wr.fail) {
            case probe::WriteFail::Cancelled: return stop();
            case probe::WriteFail::Source:    return done(CopyOutcome::Failed, "decode_error", msg::of(msg::CopyDecode, { wr.sourceError }));
            case probe::WriteFail::NoEncoder: return done(CopyOutcome::Failed, "no_encoder", msg::of(msg::CopyNoEncoder));
            case probe::WriteFail::Write:
                return done(CopyOutcome::Failed, "io", msg::of(msg::CopyTempWrite, { wu::errorText(wr.winError) }));
            default:                          return done(CopyOutcome::Failed, "encode_failed", msg::of(msg::CopyEncode));
            }
        }
    }
    // 源要完整(与原地修复同一条):文件头写了总采样数的,解出来的要正好这么多;没写的,不许有任何解码错
    const uint64_t declared = r.scan.nativeFlac ? r.scan.layout.totalSamples : 0;
    if (declared > 0 ? wr.frames != declared : wr.decodeErrors != 0) {
        wchar_t b[96];
        _snwprintf_s(b, _TRUNCATE, L"%llu / %llu / %d", (unsigned long long)wr.frames, (unsigned long long)declared, wr.decodeErrors);
        return done(CopyOutcome::Refused, wr.decodeErrors ? "decode_error" : "incomplete_audio", msg::of(msg::CopyDecode, { msg::Text(b) }));
    }
    if (!FlushFileBuffers(tmp.f.h)) return done(CopyOutcome::Failed, "io", msg::of(msg::CopyTempWrite, { wu::errorText(GetLastError()) }));
    if (opt.test.afterWrite) opt.test.afterWrite(tmp.f.h);

    // ---- 4. 核对临时文件;通过了才改成正式的名字 ----
    phase("verify");
    if (runctl::cancelRequested()) return stop();
    Want want;
    want.rate = wr.rate;
    want.bits = r.bits;
    want.padBits = spec.padBits;
    want.origRate = r.scan.info.origRate;
    want.frames = wr.frames;
    want.srcMd5 = wr.srcMd5;
    want.srcNativeMd5 = wr.srcNativeMd5;
    want.headerMd5 = wr.outMd5;
    want.fin = r.fin;
    want.rule = &opt.rule;
    const msg::Entry* vwhy = nullptr;
    bool stopped = false;
    if (!verifyCopy(tmp.f.h, wu::fileName(r.target), want, opt, vwhy, stopped)) {
        if (stopped) return stop();
        return done(CopyOutcome::Failed, "verify_failed", msg::of(msg::CopyVerifyFailed, { vwhy ? msg::of(*vwhy) : msg::Text() }));
    }
    if (runctl::cancelRequested()) return stop();             // 最后一个能停的地方
    phase("rename");
    tmp.f.close();
    if (!MoveFileExW(tmp.path.c_str(), r.target.c_str(), MOVEFILE_WRITE_THROUGH)) {      // 不带"覆盖":这中间目标名下冒出了文件也不盖
        const DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) return done(CopyOutcome::Refused, "target_exists", msg::of(msg::CopyTargetExists, { targetName }));
        return done(CopyOutcome::Failed, "io", msg::of(msg::CopyRename, { wu::errorText(e) }));
    }
    tmp.keep = true;
    at(1.0);
    r.frames = wr.frames;
    r.pcmMd5 = wr.srcMd5;
    return done(CopyOutcome::Created, "",
                kind == CopyKind::To24Bit ? msg::of(msg::CopyCreated24, { targetName, msg::num((long long)wr.frames) })
                                          : msg::of(msg::CopyCreated, { targetName, msg::num(r.bits), msg::num((long long)wr.frames) }));
}

} // namespace tagrepair
