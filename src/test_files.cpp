#include "test_files.h"

#include "win_util.h"

#include <bcrypt.h>

#include <cstring>

namespace testfiles {

bool writeFile(const std::wstring& path, const Bytes& d) {
    const HANDLE h = CreateFileW(wu::longPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = wu::writeAt(h, 0, d.data(), d.size());
    CloseHandle(h);
    return ok;
}

Bytes readFile(const std::wstring& path) {
    Bytes d;
    const HANDLE h = CreateFileW(wu::longPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return d;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz)) wu::readRange(h, 0, (size_t)sz.QuadPart, d);
    CloseHandle(h);
    return d;
}

// 一段立体声噪声;markAt ≥ 0 时从那一帧起放一条开流的数据同步记录(原始采样率 origRate)
std::vector<int32_t> pcm(int rate, int bits, double seconds, long long markAt, int origRate, uint32_t seed) {
    std::vector<int32_t> lr = synth::noise((size_t)(rate * seconds), bits, seed);
    if (markAt >= 0) synth::embedControl(lr, bits, (size_t)markAt, synth::startRecordBits(origRate, rate));
    return lr;
}

// 样本 → 完整的 FLAC 文件;extra = STREAMINFO 之后的块
Bytes flac(const std::vector<int32_t>& lr, int rate, int bits, const std::vector<synth::Block>& extra, const Bytes& prefix,
           const Bytes& suffix) {
    uint32_t mn = 0, mx = 0;
    const Bytes frames = synth::flacFrames(lr, rate, bits, 4096, &mn, &mx);
    // STREAMINFO 的 MD5:样本按小端、每样本 bits/8 字节交错
    Bytes raw;
    raw.reserve(lr.size() * (size_t)(bits / 8));
    for (int32_t v : lr)
        for (int b = 0; b < bits / 8; ++b) raw.push_back((uint8_t)((uint32_t)v >> (8 * b)));
    uint8_t md5[16] = { 0 };
    wu::Hash h(BCRYPT_MD5_ALGORITHM);
    h.update(raw.data(), raw.size());
    h.raw(md5, 16);
    std::vector<synth::Block> blocks = { { flacmeta::kStreamInfo, synth::streamInfo(rate, 2, bits, lr.size() / 2, 4096, mn, mx, md5) } };
    blocks.insert(blocks.end(), extra.begin(), extra.end());
    return synth::flacFile(blocks, frames, prefix, suffix);
}

Bytes wav(const std::vector<int32_t>& lr, int rate, int bits, int formatTag) {
    const int bytes = bits / 8;
    const uint32_t dataLen = (uint32_t)(lr.size() * (size_t)bytes);
    Bytes o;
    auto le = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i) o.push_back((uint8_t)(v >> (8 * i)));
    };
    o.insert(o.end(), { 'R', 'I', 'F', 'F' });
    le(36 + dataLen, 4);
    o.insert(o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    le(16, 4);
    le((uint32_t)formatTag, 2);
    le(2, 2);
    le((uint32_t)rate, 4);
    le((uint32_t)(rate * 2 * bytes), 4);
    le((uint32_t)(2 * bytes), 2);
    le((uint32_t)bits, 2);
    o.insert(o.end(), { 'd', 'a', 't', 'a' });
    le(dataLen, 4);
    for (int32_t v : lr) le((uint32_t)v, bytes);
    return o;
}

Bytes vc(const std::string& vendor, const std::vector<std::string>& items) { return synth::vorbisBody(vendor, items); }
Bytes zeros(size_t n) { return Bytes(n, 0); }

// 一张 1×1 的 PNG 包成 PICTURE 块(FFmpeg 会解析这个块,得是合法的)
Bytes picture() {
    static const uint8_t png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00,
                                   0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00,
                                   0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01,
                                   0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
    Bytes o;
    auto be = [&](uint32_t v) {
        for (int i = 3; i >= 0; --i) o.push_back((uint8_t)(v >> (8 * i)));
    };
    const char* mime = "image/png";
    be(3);                                                    // 封面
    be((uint32_t)strlen(mime));
    o.insert(o.end(), mime, mime + strlen(mime));
    be(0);                                                    // 没有描述
    be(1); be(1); be(32); be(0);
    be((uint32_t)sizeof png);
    o.insert(o.end(), png, png + sizeof png);
    return o;
}

Parsed parse(const Bytes& file) {
    Parsed p;
    const flacmeta::MemSource src(file.data(), file.size());
    const msg::Entry* why = nullptr;
    if (flacmeta::readLayout(src, p.lay, why) != flacmeta::Err::Ok) return p;
    p.meta.assign(file.begin() + (std::ptrdiff_t)p.lay.metaAt, file.begin() + (std::ptrdiff_t)p.lay.audioAt);
    p.audio.assign(file.begin() + (std::ptrdiff_t)p.lay.audioAt, file.end());
    if (!flacmeta::splitBlocks(p.meta.data(), p.meta.size(), p.blocks)) return p;
    for (const flacmeta::MetaBlock& b : p.blocks)
        if (b.type == flacmeta::kVorbisComment) {
            flacmeta::Comments c;
            if (!flacmeta::parseComments(b.body.data(), b.body.size(), c)) return p;
            p.vcs.push_back(c);
        }
    p.seen = flacmeta::seenIn(p.vcs);
    p.ok = true;
    return p;
}

bool stripTags(const Bytes& file, Bytes& out) {
    Parsed p = parse(file);
    if (!p.ok) return false;
    for (size_t i = 0, j = 0; i < p.blocks.size(); ++i) {
        if (p.blocks[i].type != flacmeta::kVorbisComment) continue;
        flacmeta::Comments& c = p.vcs[j++];
        std::vector<std::string> keep;
        for (const std::string& it : c.items) {
            std::string key, val;
            bool ours = false;
            if (flacmeta::splitItem(it, key, val))
                for (int k = 0; k < tagrepair::kKeys; ++k) ours = ours || tagrepair::keyEquals(key, tagrepair::keyName(k));
            if (!ours) keep.push_back(it);
        }
        c.items = keep;
        p.blocks[i].body = flacmeta::buildComments(c);
    }
    out.assign(file.begin(), file.begin() + (std::ptrdiff_t)p.lay.metaAt);
    const Bytes meta = flacmeta::joinBlocks(p.blocks);
    out.insert(out.end(), meta.begin(), meta.end());
    out.insert(out.end(), p.audio.begin(), p.audio.end());
    return true;
}

} // namespace testfiles
