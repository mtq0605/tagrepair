#pragma once
// 一个文件属于哪一类(纯函数;判据的输入由 tagfile.cpp 从 probe 与 flac_meta 取)
#include "tag_rule.h"

namespace tagrepair {

enum class State {
    Error,            // 打不开 / 读不出
    NotLossless,      // 不是无损 PCM(有损编码、DSD、浮点)
    NoMarker,         // 无损 PCM,音频数据里没有同步标记
    Complete,         // 有同步标记,标签完整(tag_rule.h 的 tagsComplete:采样率合格 + 两个编码器键至少一个合格)
    Repairable,       // 有同步标记,标签缺失或不完整,这里能修(FLAC)
    RateUnknown,      // 有同步标记的 FLAC,但码流里没读到原始采样率、文件里也没有合格的 ORIGINALSAMPLERATE:写不出这个标签
    Unsupported,      // 有同步标记,但这种容器这里修不了
    Damaged,          // 有同步标记的 FLAC,但元数据区走不通(截断 / 损坏):不碰
    Convertible,      // 有同步标记的 WAV(16 / 24 bit 的 PCM,标签写得出来):这里可以另存一个带标签的 FLAC;WAV 本身不改
};
constexpr int kStates = 9;

struct Facts {
    bool opened = false;
    bool losslessPcm = false;
    bool marker = false;          // syncdetect::Detector 找到了同步标记
    bool nativeFlac = false;      // 容器是原生 FLAC(.flac;不是 Ogg / Matroska / MP4 里的 FLAC)
    bool flacDamaged = false;     // 原生 FLAC,但 flacmeta::readLayout 或注释块解析没过
    unsigned bad = 0;             // tagrepair::problems():每个键各自合不合格
    bool rateAvailable = false;   // ORIGINALSAMPLERATE 写得出来:码流里读到了原始采样率,或者文件里已有一份合格的
    bool wavPcm = false;          // 容器是 WAV,里面是 16 / 24 bit 的整数 PCM
};

inline State classify(const Facts& f) {
    if (!f.opened) return State::Error;
    if (!f.losslessPcm) return State::NotLossless;
    if (!f.marker) return State::NoMarker;
    if (!f.nativeFlac) return f.wavPcm && f.rateAvailable ? State::Convertible : State::Unsupported;
    if (f.flacDamaged) return State::Damaged;
    if (tagsComplete(f.bad)) return State::Complete;      // 规则在 tag_rule.h:采样率合格 + 两个编码器键至少一个合格
    if ((f.bad & kBadOrigRate) && !f.rateAvailable) return State::RateUnknown;
    return State::Repairable;
}

inline const char* stateCode(State s) {
    switch (s) {
    case State::Error:       return "error";
    case State::NotLossless: return "not_lossless";
    case State::NoMarker:      return "no_marker";
    case State::Complete:    return "complete";
    case State::Repairable:  return "repairable";
    case State::RateUnknown: return "rate_unknown";
    case State::Unsupported: return "unsupported_container";
    case State::Damaged:     return "damaged";
    case State::Convertible: return "convertible";
    }
    return "error";
}

} // namespace tagrepair
