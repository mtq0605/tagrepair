#include "synth_flac.h"

namespace synth {

uint8_t crc8(const uint8_t* d, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int b = 0; b < 8; ++b) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint16_t)(d[i] << 8);
        for (int b = 0; b < 8; ++b) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1);
    }
    return c;
}

std::vector<int32_t> noise(size_t frames, int bits, uint32_t seed) {
    std::vector<int32_t> v(frames * 2);
    uint32_t s = seed ? seed : 1u;
    const int shift = 32 - (bits - 6);                        // 留 6 bit 余量:远不到满幅
    for (int32_t& x : v) {
        s = s * 1664525u + 1013904223u;
        x = (int32_t)s >> shift;
    }
    return v;
}

int rateCode(int hz) {
    static const int base[3] = { 44100, 48000, 64000 };
    for (int b = 0; b < 3; ++b)
        for (int k = 0; k < 8; ++k)
            if ((base[b] << k) == hz) return (b << 3) | k;
    return -1;
}

std::vector<uint8_t> recordBits(const Record& r) {
    std::vector<uint8_t> bits;
    auto put = [&](uint64_t v, int n) {
        for (int i = 0; i < n; ++i) bits.push_back((uint8_t)((v >> i) & 1));      // 低位先出
    };
    put(0x11319207d5ull, 40);                                 // 同步标记:记录类型 5 + 36 位常数
    put(r.hasPosition ? 1 : 0, 1);
    put(0, 1);
    put(r.originalCode, 5);
    put(r.carrierCode, 5);
    put(r.otherFields, 21);
    put(r.items.size(), 7);
    for (const auto& it : r.items) put(it.second, 8);         // 长度表
    for (const auto& it : r.items) put(it.first, 8);          // 类型表
    if (r.hasPosition) put(r.position, 32);
    uint32_t s = r.fillSeed ? r.fillSeed : 1u;
    for (const auto& it : r.items)
        for (unsigned i = 0; i < it.second; ++i) {
            s = s * 1664525u + 1013904223u;
            bits.push_back((uint8_t)(s >> 31));
        }
    put(checkOf(bits, bits.size(), r.hasPosition ? r.position : 0u), 4);
    return bits;
}

unsigned checkOf(const std::vector<uint8_t>& bits, size_t count, uint32_t position) {
    // 校验:32 位移位寄存器,初值是记录位置的低 4 位(流的第一条记录位置是 0);每进一位右移一位、
    // 移出的是 1 就异或 3;记录补零到 32 位的整数倍后取低 4 位
    uint32_t reg = position & 15u;
    auto step = [&](unsigned bit) {
        const unsigned x = reg & 1;
        reg = (reg >> 1) | ((uint32_t)bit << 31);
        if (x) reg ^= 3;
    };
    for (size_t i = 0; i < count && i < bits.size(); ++i) step(bits[i]);
    for (size_t i = count; i % 32; ++i) step(0);
    return reg & 15u;
}

std::vector<uint8_t> startRecordBits(int origRateHz, int carrierRateHz) {
    Record r;
    r.originalCode = (unsigned)rateCode(origRateHz) & 31u;
    r.carrierCode = (unsigned)rateCode(carrierRateHz) & 31u;
    return recordBits(r);
}

void embedControl(std::vector<int32_t>& lr, int bits, size_t atFrame, const std::vector<uint8_t>& ctrl, int sampleBit) {
    const int bit = sampleBit - (24 - bits);                  // 24 bit 样本的第 8 位 = 16 bit 样本的第 0 位
    if (bit < 0 || bit >= bits) return;
    for (size_t i = 0; i < ctrl.size() && atFrame + i < lr.size() / 2; ++i) {
        const int32_t l = lr[(atFrame + i) * 2];
        int32_t& r = lr[(atFrame + i) * 2 + 1];
        const int32_t want = ((l >> bit) & 1) ^ (int32_t)ctrl[i];
        r = (r & ~((int32_t)1 << bit)) | (want << bit);
    }
}

std::vector<uint8_t> flacFrames(const std::vector<int32_t>& lr, int rate, int bits, int blockSize, uint32_t* minFrame, uint32_t* maxFrame) {
    std::vector<uint8_t> out;
    const size_t total = lr.size() / 2;
    const int bytes = bits / 8;
    uint32_t mn = 0xFFFFFFFFu, mx = 0;
    uint32_t number = 0;
    for (size_t at = 0; at < total; at += (size_t)blockSize, ++number) {
        const size_t n = total - at < (size_t)blockSize ? total - at : (size_t)blockSize;
        const size_t start = out.size();
        out.push_back(0xFF);
        out.push_back(0xF8);                                  // 同步码 + 固定块长
        const bool full = n == (size_t)blockSize && blockSize == 4096;
        const int srCode = rate == 44100 ? 9 : rate == 48000 ? 10 : 0;
        out.push_back((uint8_t)(((full ? 12 : 7) << 4) | srCode));      // 4096 → 1100;别的块长 → 0111(头尾跟 16 bit 的 块长−1)
        out.push_back((uint8_t)((1 << 4) | ((bits == 16 ? 4 : 6) << 1)));   // 两声道各自独立;样本位数
        // 帧号:UTF-8 式变长
        if (number < 0x80) {
            out.push_back((uint8_t)number);
        } else if (number < 0x800) {
            out.push_back((uint8_t)(0xC0 | (number >> 6)));
            out.push_back((uint8_t)(0x80 | (number & 0x3F)));
        } else if (number < 0x10000) {
            out.push_back((uint8_t)(0xE0 | (number >> 12)));
            out.push_back((uint8_t)(0x80 | ((number >> 6) & 0x3F)));
            out.push_back((uint8_t)(0x80 | (number & 0x3F)));
        } else {
            out.push_back((uint8_t)(0xF0 | ((number >> 18) & 0x07)));
            out.push_back((uint8_t)(0x80 | ((number >> 12) & 0x3F)));
            out.push_back((uint8_t)(0x80 | ((number >> 6) & 0x3F)));
            out.push_back((uint8_t)(0x80 | (number & 0x3F)));
        }
        if (!full) {
            out.push_back((uint8_t)((n - 1) >> 8));
            out.push_back((uint8_t)(n - 1));
        }
        out.push_back(crc8(out.data() + start, out.size() - start));
        for (int c = 0; c < 2; ++c) {
            out.push_back(0x02);                              // 子帧头:VERBATIM,没有废位
            for (size_t i = 0; i < n; ++i) {
                const uint32_t v = (uint32_t)lr[(at + i) * 2 + (size_t)c];
                for (int b = bytes - 1; b >= 0; --b) out.push_back((uint8_t)(v >> (8 * b)));
            }
        }
        const uint16_t c16 = crc16(out.data() + start, out.size() - start);
        out.push_back((uint8_t)(c16 >> 8));
        out.push_back((uint8_t)c16);
        const uint32_t len = (uint32_t)(out.size() - start);
        if (len < mn) mn = len;
        if (len > mx) mx = len;
    }
    if (minFrame) *minFrame = total ? mn : 0;
    if (maxFrame) *maxFrame = mx;
    return out;
}

std::vector<uint8_t> streamInfo(int rate, int channels, int bits, uint64_t totalSamples, int blockSize, uint32_t minFrame, uint32_t maxFrame,
                                const uint8_t md5[16]) {
    std::vector<uint8_t> s(34, 0);
    s[0] = (uint8_t)(blockSize >> 8); s[1] = (uint8_t)blockSize;
    s[2] = (uint8_t)(blockSize >> 8); s[3] = (uint8_t)blockSize;
    s[4] = (uint8_t)(minFrame >> 16); s[5] = (uint8_t)(minFrame >> 8); s[6] = (uint8_t)minFrame;
    s[7] = (uint8_t)(maxFrame >> 16); s[8] = (uint8_t)(maxFrame >> 8); s[9] = (uint8_t)maxFrame;
    s[10] = (uint8_t)(rate >> 12);
    s[11] = (uint8_t)(rate >> 4);
    s[12] = (uint8_t)(((rate & 0x0F) << 4) | ((channels - 1) << 1) | (((bits - 1) >> 4) & 1));
    s[13] = (uint8_t)((((bits - 1) & 0x0F) << 4) | ((totalSamples >> 32) & 0x0F));
    s[14] = (uint8_t)(totalSamples >> 24); s[15] = (uint8_t)(totalSamples >> 16);
    s[16] = (uint8_t)(totalSamples >> 8);  s[17] = (uint8_t)totalSamples;
    if (md5)
        for (int i = 0; i < 16; ++i) s[18 + (size_t)i] = md5[i];
    return s;
}

std::vector<uint8_t> vorbisBody(const std::string& vendor, const std::vector<std::string>& items) {
    std::vector<uint8_t> o;
    auto le = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i)));
    };
    le((uint32_t)vendor.size());
    o.insert(o.end(), vendor.begin(), vendor.end());
    le((uint32_t)items.size());
    for (const std::string& it : items) {
        le((uint32_t)it.size());
        o.insert(o.end(), it.begin(), it.end());
    }
    return o;
}

std::vector<uint8_t> flacFile(const std::vector<Block>& blocks, const std::vector<uint8_t>& frames, const std::vector<uint8_t>& prefix,
                              const std::vector<uint8_t>& suffix) {
    std::vector<uint8_t> o(prefix);
    o.push_back('f'); o.push_back('L'); o.push_back('a'); o.push_back('C');
    for (size_t i = 0; i < blocks.size(); ++i) {
        const size_t len = blocks[i].second.size();
        o.push_back((uint8_t)((i + 1 == blocks.size() ? 0x80 : 0) | (blocks[i].first & 0x7F)));
        o.push_back((uint8_t)(len >> 16));
        o.push_back((uint8_t)(len >> 8));
        o.push_back((uint8_t)len);
        o.insert(o.end(), blocks[i].second.begin(), blocks[i].second.end());
    }
    o.insert(o.end(), frames.begin(), frames.end());
    o.insert(o.end(), suffix.begin(), suffix.end());
    return o;
}

std::vector<uint8_t> id3v2(size_t bodyBytes) {
    std::vector<uint8_t> o = { 'I', 'D', '3', 3, 0, 0,
                               (uint8_t)((bodyBytes >> 21) & 0x7F), (uint8_t)((bodyBytes >> 14) & 0x7F), (uint8_t)((bodyBytes >> 7) & 0x7F),
                               (uint8_t)(bodyBytes & 0x7F) };
    o.resize(10 + bodyBytes, 0);
    return o;
}

} // namespace synth
