#include "flac_meta.h"

#include <cstring>

namespace flacmeta {

namespace {

uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void putLe32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back((uint8_t)v);
    o.push_back((uint8_t)(v >> 8));
    o.push_back((uint8_t)(v >> 16));
    o.push_back((uint8_t)(v >> 24));
}

void putStr(std::vector<uint8_t>& o, const std::string& s) {
    putLe32(o, (uint32_t)s.size());
    o.insert(o.end(), s.begin(), s.end());
}

// 这一条是第 k 个键吗(是:value = 它的值)
bool itemIsKey(const std::string& item, int k, std::string* value) {
    std::string key, v;
    if (!splitItem(item, key, v) || !tagrepair::keyEquals(key, tagrepair::keyName(k))) return false;
    if (value) *value = std::move(v);
    return true;
}

} // namespace

bool MemSource::read(uint64_t at, void* dst, size_t n) const {
    if (at > m_n || n > m_n - (size_t)at) return false;
    if (n) std::memcpy(dst, m_d + (size_t)at, n);
    return true;
}

// ---------------------------------------------------------------------------
// 元数据块走一遍
// ---------------------------------------------------------------------------
Err readLayout(const Source& s, Layout& out, const msg::Entry*& why) {
    why = nullptr;
    out = Layout();
    out.fileSize = s.size();
    uint64_t at = 0;
    // ID3v2(个别工具会往 FLAC 前面塞一个;连着几个的也认):10 字节头,长度是 4 个 7 bit 字节
    for (int guard = 0; guard < 8 && at + 10 <= out.fileSize; ++guard) {
        uint8_t h[10];
        if (!s.read(at, h, 10)) { why = &msg::LayIo; return Err::Io; }
        if (!(h[0] == 'I' && h[1] == 'D' && h[2] == '3' && h[3] != 0xFF && h[4] != 0xFF &&
              !((h[6] | h[7] | h[8] | h[9]) & 0x80)))
            break;
        const uint64_t body = ((uint64_t)h[6] << 21) | ((uint64_t)h[7] << 14) | ((uint64_t)h[8] << 7) | h[9];
        at += 10 + body + ((h[5] & 0x10) ? 10 : 0);              // 0x10:后面还跟一个 10 字节的尾
        if (at > out.fileSize) { why = &msg::LayId3TooLong; return Err::Truncated; }
    }
    out.id3Bytes = at;
    uint8_t sig[4];
    if (at + 4 > out.fileSize || !s.read(at, sig, 4) || std::memcmp(sig, "fLaC", 4) != 0) {
        why = &msg::LayNotFlac;
        return Err::NotFlac;
    }
    out.metaAt = at + 4;
    uint64_t pos = out.metaAt;
    for (;;) {
        uint8_t h[4];
        if (pos + 4 > out.fileSize) { why = &msg::LayHeaderCut; return Err::Truncated; }
        if (!s.read(pos, h, 4)) { why = &msg::LayIo; return Err::Io; }
        BlockRef b;
        b.type = (uint8_t)(h[0] & 0x7F);
        b.last = (h[0] & 0x80) != 0;
        b.len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        b.at = pos;
        if (b.type == 127) { why = &msg::LayBadType; return Err::Corrupt; }
        if (out.blocks.empty() && !(b.type == kStreamInfo && b.len == 34)) { why = &msg::LayFirstNotStreamInfo; return Err::Corrupt; }
        if (!out.blocks.empty() && b.type == kStreamInfo) { why = &msg::LayTwoStreamInfo; return Err::Corrupt; }
        if (pos + 4 + b.len > out.fileSize) { why = &msg::LayBlockBeyondFile; return Err::Truncated; }
        if (b.type == kStreamInfo && !s.read(pos + 4, out.streamInfo, 34)) { why = &msg::LayIo; return Err::Io; }
        out.blocks.push_back(b);
        pos += 4 + (uint64_t)b.len;
        if (b.last) break;
        if (out.blocks.size() > 65536) { why = &msg::LayTooManyBlocks; return Err::Corrupt; }
        if (pos - out.metaAt > kMaxMetaBytes) { why = &msg::LayMetaTooBig; return Err::Corrupt; }
    }
    out.audioAt = pos;
    const uint8_t* si = out.streamInfo;
    out.sampleRate = (int)(((uint32_t)si[10] << 12) | ((uint32_t)si[11] << 4) | (si[12] >> 4));
    out.channels = ((si[12] >> 1) & 7) + 1;
    out.bits = (((si[12] & 1) << 4) | (si[13] >> 4)) + 1;
    out.totalSamples = ((uint64_t)(si[13] & 0x0F) << 32) | ((uint64_t)si[14] << 24) | ((uint64_t)si[15] << 16) |
                       ((uint64_t)si[16] << 8) | si[17];
    if (out.sampleRate == 0) { why = &msg::LayRateZero; return Err::Corrupt; }
    // 元数据之后必须是音频帧(同步码 0xFFF8 / 0xFFF9):块长被写坏时走到的是垃圾,这里拦住
    if (out.fileSize - out.audioAt >= 2) {
        uint8_t f[2];
        if (!s.read(out.audioAt, f, 2)) { why = &msg::LayIo; return Err::Io; }
        if (!(f[0] == 0xFF && (f[1] & 0xFE) == 0xF8)) { why = &msg::LayNoFrameAfterMeta; return Err::Corrupt; }
    } else if (out.totalSamples > 0 || out.fileSize != out.audioAt) {
        why = &msg::LayNoAudio;
        return Err::Truncated;
    }
    return Err::Ok;
}

void buildStreamInfo(const StreamInfoFields& f, uint8_t o[34]) {
    const uint32_t rate = (uint32_t)f.sampleRate, ch = (uint32_t)(f.channels - 1), bps = (uint32_t)(f.bits - 1);
    o[0] = (uint8_t)(f.minBlock >> 8);
    o[1] = (uint8_t)f.minBlock;
    o[2] = (uint8_t)(f.maxBlock >> 8);
    o[3] = (uint8_t)f.maxBlock;
    o[4] = (uint8_t)(f.minFrame >> 16);
    o[5] = (uint8_t)(f.minFrame >> 8);
    o[6] = (uint8_t)f.minFrame;
    o[7] = (uint8_t)(f.maxFrame >> 16);
    o[8] = (uint8_t)(f.maxFrame >> 8);
    o[9] = (uint8_t)f.maxFrame;
    o[10] = (uint8_t)(rate >> 12);                            // 采样率 20 位、声道数 − 1 共 3 位、位深 − 1 共 5 位、总采样数 36 位
    o[11] = (uint8_t)(rate >> 4);
    o[12] = (uint8_t)(((rate & 0x0F) << 4) | ((ch & 7) << 1) | ((bps >> 4) & 1));
    o[13] = (uint8_t)(((bps & 0x0F) << 4) | (uint32_t)((f.totalSamples >> 32) & 0x0F));
    o[14] = (uint8_t)(f.totalSamples >> 24);
    o[15] = (uint8_t)(f.totalSamples >> 16);
    o[16] = (uint8_t)(f.totalSamples >> 8);
    o[17] = (uint8_t)f.totalSamples;
    std::memcpy(o + 18, f.md5, 16);
}

// ---------------------------------------------------------------------------
// Vorbis 注释
// ---------------------------------------------------------------------------
bool parseComments(const uint8_t* d, size_t n, Comments& out) {
    out = Comments();
    size_t pos = 0;
    auto str = [&](std::string& s) -> bool {
        if (n - pos < 4) return false;
        const uint32_t len = le32(d + pos);
        pos += 4;
        if (len > n - pos) return false;
        s.assign((const char*)d + pos, len);
        pos += len;
        return true;
    };
    if (!str(out.vendor)) return false;
    if (n - pos < 4) return false;
    const uint32_t count = le32(d + pos);
    pos += 4;
    if (count > (n - pos) / 4) return false;                  // 每条至少占 4 字节的长度
    out.items.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        if (!str(out.items[i])) return false;
    out.tail.assign(d + pos, d + n);
    return true;
}

std::vector<uint8_t> buildComments(const Comments& c) {
    std::vector<uint8_t> o;
    putStr(o, c.vendor);
    putLe32(o, (uint32_t)c.items.size());
    for (const std::string& it : c.items) putStr(o, it);
    o.insert(o.end(), c.tail.begin(), c.tail.end());
    return o;
}

bool splitItem(const std::string& item, std::string& key, std::string& value) {
    const size_t eq = item.find('=');
    if (eq == std::string::npos) return false;
    key.assign(item, 0, eq);
    value.assign(item, eq + 1, std::string::npos);
    return true;
}

// ---------------------------------------------------------------------------
// 元数据区 ↔ 块
// ---------------------------------------------------------------------------
bool splitBlocks(const uint8_t* meta, size_t n, std::vector<MetaBlock>& out) {
    out.clear();
    size_t pos = 0;
    for (;;) {
        if (n - pos < 4) return false;
        const bool last = (meta[pos] & 0x80) != 0;
        const uint32_t len = ((uint32_t)meta[pos + 1] << 16) | ((uint32_t)meta[pos + 2] << 8) | meta[pos + 3];
        MetaBlock b;
        b.type = (uint8_t)(meta[pos] & 0x7F);
        pos += 4;
        if (len > n - pos) return false;
        b.body.assign(meta + pos, meta + pos + len);
        pos += len;
        out.push_back(std::move(b));
        if (last) return pos == n;                            // 最后一块之后不许还有字节
        if (pos == n) return false;                           // 字节用完了还没见到"最后一块"
    }
}

std::vector<uint8_t> joinBlocks(const std::vector<MetaBlock>& b) {
    std::vector<uint8_t> o;
    size_t total = 0;
    for (const MetaBlock& x : b) total += 4 + x.body.size();
    o.reserve(total);
    for (size_t i = 0; i < b.size(); ++i) {
        const size_t len = b[i].body.size();
        o.push_back((uint8_t)((i + 1 == b.size() ? 0x80 : 0) | (b[i].type & 0x7F)));
        o.push_back((uint8_t)(len >> 16));
        o.push_back((uint8_t)(len >> 8));
        o.push_back((uint8_t)len);
        o.insert(o.end(), b[i].body.begin(), b[i].body.end());
    }
    return o;
}

// ---------------------------------------------------------------------------
// 三个键
// ---------------------------------------------------------------------------
tagrepair::Seen seenIn(const std::vector<Comments>& vcs) {
    tagrepair::Seen s;
    for (const Comments& c : vcs)
        for (const std::string& it : c.items) {
            std::string key, v;
            if (!splitItem(it, key, v)) continue;
            for (int k = 0; k < tagrepair::kKeys; ++k)
                if (tagrepair::keyEquals(key, tagrepair::keyName(k)) && s.count[k]++ == 0) s.value[k] = v;
        }
    return s;
}

bool seenInMeta(const uint8_t* meta, size_t n, tagrepair::Seen& out) {
    std::vector<MetaBlock> blocks;
    if (!splitBlocks(meta, n, blocks)) return false;
    std::vector<Comments> vcs;
    for (const MetaBlock& b : blocks) {
        if (b.type != kVorbisComment) continue;
        Comments c;
        if (!parseComments(b.body.data(), b.body.size(), c)) return false;
        vcs.push_back(std::move(c));
    }
    out = seenIn(vcs);
    return true;
}

void chooseFinal(const std::vector<Comments>& vcs, int streamOrigRate, std::string fin[tagrepair::kKeys], const tagrepair::Rule& rule) {
    for (int k = 0; k < tagrepair::kKeys; ++k) {
        fin[k].clear();
        bool have = false;
        for (size_t b = 0; b < vcs.size() && !have; ++b)
            for (const std::string& it : vcs[b].items) {
                std::string v;
                if (itemIsKey(it, k, &v) && tagrepair::valueOk(k, v, streamOrigRate, rule)) {
                    fin[k] = v;
                    have = true;
                    break;
                }
            }
        if (have) continue;
        if (k == tagrepair::kOrigRate) {
            if (streamOrigRate > 0) fin[k] = std::to_string(streamOrigRate);
        } else {
            fin[k] = rule.encoderTag;
        }
    }
}

bool applyTags(std::vector<Comments>& vcs, const std::string fin[tagrepair::kKeys]) {
    if (vcs.empty()) return false;
    bool changed = false;
    for (int k = 0; k < tagrepair::kKeys; ++k) {
        // 只有一份、值也对:原样不动(键名大小写、所在的块都不碰)
        int count = 0;
        bool onlyOk = false;
        for (const Comments& c : vcs)
            for (const std::string& it : c.items) {
                std::string v;
                if (!itemIsKey(it, k, &v)) continue;
                ++count;
                onlyOk = v == fin[k];
            }
        if (count == 1 && onlyOk) continue;
        // 否则:第一个注释块里的第一份改成要的值(值已经对就不动它),其余的(包括别的注释块里的)全删;一份都没有就加在末尾
        const std::string want = std::string(tagrepair::keyName(k)) + "=" + fin[k];
        bool placed = false;
        for (size_t b = 0; b < vcs.size(); ++b) {
            std::vector<std::string>& items = vcs[b].items;
            for (size_t i = 0; i < items.size();) {
                std::string v;
                if (!itemIsKey(items[i], k, &v)) { ++i; continue; }
                if (b == 0 && !placed) {
                    if (v != fin[k]) { items[i] = want; changed = true; }
                    placed = true;
                    ++i;
                } else {
                    items.erase(items.begin() + (std::ptrdiff_t)i);
                    changed = true;
                }
            }
        }
        if (!placed) {
            vcs[0].items.push_back(want);
            changed = true;
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// 修好之后的整段元数据
// ---------------------------------------------------------------------------
PlanErr planFix(const std::vector<uint8_t>& oldMeta, const std::string fin[tagrepair::kKeys], uint64_t metaAtInFile, uint64_t sectorBytes, Plan& out) {
    out = Plan();
    for (int k = 0; k < tagrepair::kKeys; ++k)
        if (fin[k].empty()) return PlanErr::Internal;
    std::vector<MetaBlock> blocks;
    if (!splitBlocks(oldMeta.data(), oldMeta.size(), blocks) || blocks.empty() || blocks[0].type != kStreamInfo) return PlanErr::BadMeta;
    std::vector<size_t> vcIdx;
    std::vector<Comments> vcs;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type != kVorbisComment) continue;
        Comments c;
        if (!parseComments(blocks[i].body.data(), blocks[i].body.size(), c)) return PlanErr::BadComment;
        vcIdx.push_back(i);
        vcs.push_back(std::move(c));
    }
    bool created = false;
    if (vcIdx.empty()) {
        // 一个注释块都没有:新建一个,放在第一个 PADDING 之前(没有 PADDING 就放最后)
        size_t at = blocks.size();
        for (size_t i = 0; i < blocks.size(); ++i)
            if (blocks[i].type == kPadding) { at = i; break; }
        MetaBlock nb;
        nb.type = kVorbisComment;
        blocks.insert(blocks.begin() + (std::ptrdiff_t)at, nb);
        vcIdx.push_back(at);
        Comments c;
        c.vendor = newVendor();
        vcs.push_back(std::move(c));
        created = true;
    }
    const bool changed = applyTags(vcs, fin);
    if (!changed && !created) return PlanErr::Ok;             // 已经是要的样子
    for (size_t j = 0; j < vcIdx.size(); ++j) {
        std::vector<uint8_t> body = buildComments(vcs[j]);
        if (body.size() > kMaxBlockBody) return PlanErr::TooBig;
        blocks[vcIdx[j]].body = std::move(body);
    }
    const size_t oldTotal = oldMeta.size();
    size_t newTotal = 0;
    for (const MetaBlock& b : blocks) newTotal += 4 + b.body.size();

    // 用 PADDING 把总长找平(音频起点不变)。先用改动的注释块后面最近的 PADDING,再用它前面最近的
    std::vector<MetaBlock> cand = blocks;
    std::vector<size_t> pads;
    for (size_t i = vcIdx[0] + 1; i < cand.size(); ++i)
        if (cand[i].type == kPadding) pads.push_back(i);
    for (size_t i = vcIdx[0]; i-- > 0;)
        if (cand[i].type == kPadding) pads.push_back(i);
    bool fit = false;
    if (newTotal == oldTotal) {
        fit = true;
    } else if (newTotal > oldTotal) {
        size_t need = newTotal - oldTotal;
        std::vector<char> drop(cand.size(), 0);
        for (size_t pi : pads) {
            if (!need) break;
            const size_t len = cand[pi].body.size();
            if (len >= need) { cand[pi].body.resize(len - need); need = 0; }          // 这一块缩短就够
            else if (len + 4 <= need) { drop[pi] = 1; need -= len + 4; }              // 连块头一起用掉
            else { cand[pi].body.clear(); need -= len; }                              // 块体用光、块头留着;还差 1–3 字节找下一块
        }
        if (!need) {
            std::vector<MetaBlock> kept;
            for (size_t i = 0; i < cand.size(); ++i)
                if (!drop[i]) kept.push_back(std::move(cand[i]));
            cand.swap(kept);
            fit = true;
        }
    } else {
        const size_t spare = oldTotal - newTotal;
        if (!pads.empty()) {
            MetaBlock& p = cand[pads[0]];
            if (p.body.size() + spare <= kMaxBlockBody) { p.body.resize(p.body.size() + spare, 0); fit = true; }
        } else if (spare >= 4 && spare - 4 <= kMaxBlockBody) {
            MetaBlock p;                                       // 没有 PADDING:省下的字节够一个块头就新建一个,紧跟在注释块后面
            p.type = kPadding;
            p.body.assign(spare - 4, 0);
            cand.insert(cand.begin() + (std::ptrdiff_t)vcIdx[0] + 1, std::move(p));
            fit = true;
        }
    }
    if (fit) {
        out.newMeta = joinBlocks(cand);
        if (out.newMeta.size() != oldTotal) return PlanErr::Internal;
        size_t a = 0, b = oldTotal;
        while (a < b && out.newMeta[a] == oldMeta[a]) ++a;
        while (b > a && out.newMeta[b - 1] == oldMeta[b - 1]) --b;
        if (a == b) { out = Plan(); return PlanErr::Ok; }
        out.sameSize = true;
        out.diffAt = a;
        out.diffEnd = b;
        out.mode = withinOneSector(metaAtInFile + a, metaAtInFile + b, sectorBytes) ? Mode::InPlace : Mode::Rewrite;
        return PlanErr::Ok;
    }
    out.newMeta = joinBlocks(blocks);
    out.sameSize = false;
    out.mode = Mode::Rewrite;
    return PlanErr::Ok;
}

} // namespace flacmeta
