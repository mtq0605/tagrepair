#include "sync_detect.h"

// 格式事实见 sync_detect.h 开头的一至五;这里的每一段都对着其中一条写

namespace syncdetect {

namespace {

// 三:用到的几个位号
constexpr size_t kHasPositionAt = 40;
constexpr size_t kOriginalRateAt = 42;
constexpr int    kRateCodeBits = 5;
constexpr size_t kItemCountAt = 73;
constexpr int    kItemCountBits = 7;
constexpr size_t kTablesAt = 80;                  // 附加项的长度表从这里起;到这里为止的 80 位每条记录都有
constexpr int    kPositionBits = 32;
constexpr int    kCheckBits = 4;

} // namespace

int rateFromCode(unsigned code) {
    static const int base[4] = { 44100, 48000, 64000, 0 };
    const unsigned which = (code >> 3) & 3u, power = code & 7u;
    return code < 32 ? base[which] << power : 0;
}

unsigned recordCheck(const uint8_t* bits, size_t count, uint32_t position) {
    uint32_t reg = position & 15u;
    auto in = [&reg](uint32_t b) {
        const bool out = (reg & 1u) != 0;
        reg = (reg >> 1) | (b << 31);
        if (out) reg ^= 3u;
    };
    for (size_t i = 0; i < count; ++i) in(bits[i] & 1u);
    for (size_t i = count; i % 32 != 0; ++i) in(0);
    return reg & 15u;
}

unsigned Detector::field(size_t at, int width) const {
    unsigned v = 0;
    for (int i = 0; i < width; ++i) v |= (unsigned)(rec_[at + (size_t)i] & 1u) << i;
    return v;
}

void Detector::startRecord() {
    // 刚看到同步标记:它就是记录的头 40 位,后面的位接着收
    rec_.clear();
    for (int k = 0; k < kMarkerBits; ++k) rec_.push_back((uint8_t)((kMarker >> k) & 1u));
    stage_ = Stage::Head;
    need_ = kTablesAt;
}

void Detector::searchAgain() {
    // 这条记录不作数:在已经认定的那一位上接着找下一个同步标记(那一位最近的 40 个控制位一直记着,不用清)
    stage_ = Stage::Search;
    rec_.clear();
}

void Detector::decide() {
    const bool hasPosition = field(kHasPositionAt, 1) != 0;
    const size_t items = field(kItemCountAt, kItemCountBits);
    const size_t positionAt = kTablesAt + 16 * items;                 // 两张表各 N×8 位
    const size_t itemsAt = positionAt + (hasPosition ? (size_t)kPositionBits : 0);
    if (stage_ == Stage::Head) {                  // 头 80 位到齐:知道两张表(与位置)到哪为止
        stage_ = Stage::Table;
        need_ = itemsAt;
        if (rec_.size() < need_) return;
    }
    if (stage_ == Stage::Table) {                 // 长度表到齐:知道整条记录多长
        size_t total = itemsAt;
        for (size_t i = 0; i < items; ++i) total += field(kTablesAt + 8 * i, 8);
        checkAt_ = total;
        stage_ = Stage::Body;
        need_ = total + (size_t)kCheckBits;
        if (rec_.size() < need_) return;
    }
    // 整条记录到齐:校验对上才读采样率
    const uint32_t position = hasPosition ? (uint32_t)field(positionAt, 16) | ((uint32_t)field(positionAt + 16, 16) << 16) : 0u;
    const bool verified = recordCheck(rec_.data(), checkAt_, position) == field(checkAt_, kCheckBits);
    const int rate = verified ? rateFromCode(field(kOriginalRateAt, kRateCodeBits)) : 0;
    if (rate > 0) {
        rate_ = rate;
        stage_ = Stage::Done;
        rec_.clear();
        rec_.shrink_to_fit();
    } else {
        searchAgain();
    }
}

void Detector::push(unsigned candidates) {
    switch (stage_) {
    case Stage::Done:
        return;
    case Stage::Search: {
        const int from = bit_ < 0 ? 0 : bit_, to = bit_ < 0 ? kHighestBit - kLowestBit : bit_;
        for (int i = from; i <= to; ++i) {
            uint64_t& r = recent_[i];
            r = (r >> 1) | ((uint64_t)((candidates >> i) & 1u) << (kMarkerBits - 1));
            if (r != kMarker) continue;
            bit_ = i;                             // 几位同时出现时取最低的那一位
            startRecord();
            return;
        }
        return;
    }
    default: {
        const unsigned b = (candidates >> bit_) & 1u;
        // 一条记录还没收完、同一位上又出现了同步标记:前一条不作数(多半是它的长度字段坏了,把后面的也算了进去),从新的这个标记起重新收
        uint64_t& r = recent_[bit_];
        r = (r >> 1) | ((uint64_t)b << (kMarkerBits - 1));
        if (r == kMarker) {
            startRecord();
            return;
        }
        rec_.push_back((uint8_t)b);
        if (rec_.size() >= need_) decide();
        return;
    }
    }
}

void Detector::feed(const int32_t* lr, size_t frames) {
    if (!lr) return;
    frames_ += frames;
    if (stage_ == Stage::Done) return;
    for (size_t i = 0; i < frames; ++i) {
        // 一:24 bit 样本左对齐在 32 位里,样本的第 8..15 位就是这个字的第 16..23 位
        const uint32_t x = (uint32_t)lr[2 * i] ^ (uint32_t)lr[2 * i + 1];
        push((x >> (kLowestBit + 8)) & 0xFFu);
    }
}

} // namespace syncdetect
