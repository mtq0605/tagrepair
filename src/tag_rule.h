#pragma once
// =============================================================================
// 三个识别标签的判定规则(纯函数,不依赖任何库)
// =============================================================================
// 本程序认文件看音频数据里的同步标记(syncdetect::Detector),不看标签;很多播放器与设备认这种 FLAC 靠三个 Vorbis 注释:
// ENCODER / MQAENCODER / ORIGINALSAMPLERATE。标签在下载、转存、改标签时丢了,音频数据没变,那些设备就认不出来。
// 这里只管一件事:给定三个键各出现了几次、值是什么,加上码流里读到的原始采样率,判哪几个键要修。
//
// 合格的定义:
//   ENCODER / MQAENCODER   值以 kDefaultAcceptPrefix 开头(大小写、位置都要对),
//                          或者正好就是修复时会写进去的那个值
//   ORIGINALSAMPLERATE     一个十进制整数,等于码流里的原始采样率(码流里没读到时只要求是正整数)
//   值要正好是会写进去的那种写法:前后带空白、字母大小写不一样、"0352800" 这类"差不多"的不算合格 ——
//   只看标签的设备认不认这些写法不知道,所以修的时候一律改成标准写法,不留着
//   一个键要合格,还得恰好出现一次
//
// 标签完整的定义(tagsComplete,只在这一处):
//   ORIGINALSAMPLERATE 合格,并且 ENCODER / MQAENCODER 两个里至少有一个合格。
//   早期的官方编码器只写 ENCODER 与 ORIGINALSAMPLERATE、不写 MQAENCODER —— 那样的文件是完整的,一个字节都不许动。
//   另一个编码器键不在、或者是别的值(比如 MQAENCODER 合格、旁边有 ENCODER=Lavf…),不算不完整,原样留着。
//   不完整的文件才修;修的时候照旧把三个键都补到合格(原来合格的值留着)。
//
// 写进去的值、认可已有值的前缀,各只在这里定一次(kDefaultEncoderTag / kDefaultAcceptPrefix);
// 自检与 tests/ 里的期望值都从这两个常量来,不抄字符串。要换默认值:改这里的一行。
// =============================================================================
#include "messages.h"

#include <string>

namespace tagrepair {

enum { kEncoder = 0, kFormatEncoder = 1, kOrigRate = 2, kKeys = 3 };
enum : unsigned { kBadEncoder = 1u << kEncoder, kBadFormatEncoder = 1u << kFormatEncoder, kBadOrigRate = 1u << kOrigRate };

inline const char* keyName(int k) {
    static const char* const n[kKeys] = { "ENCODER", "MQAENCODER", "ORIGINALSAMPLERATE" };
    return k >= 0 && k < kKeys ? n[k] : "";
}

// 修复时写进 ENCODER / MQAENCODER 的默认值。纯文字,没有签名:写明是标签修复工具写的、"(unsigned)" ——
// 不冒充官方编码器
constexpr const char* kDefaultEncoderTag = "MQAEncode v1.1, tag repair (unsigned)";
// 文件里已有的 ENCODER / MQAENCODER 以它开头就算合格、留着不动。为什么要这个前缀:有的设备只认这个开头 ——
// 用两个只差这一处的文件试过,值以它开头的那个才被认出来
constexpr const char* kDefaultAcceptPrefix = "MQAEncode";
// 不管写什么值,这两段都必须在里面:是谁写的(标签修复工具,不是编码器)、没有签名
constexpr const char* kWriterMark = "tag repair";
constexpr const char* kUnsignedMark = "(unsigned)";
constexpr size_t kMaxEncoderTagBytes = 200;

// 一次扫描 / 修复用的规则。默认 = 上面两个常量;命令行的 --encoder-tag / --accept-prefix 换的就是这两项
struct Rule {
    std::string encoderTag = kDefaultEncoderTag;        // 写进 ENCODER / MQAENCODER 的值
    std::string acceptPrefix = kDefaultAcceptPrefix;    // 已有的值以它开头就合格(空 = 只要不是空值就合格)
};
inline const Rule& defaultRule() {
    static const Rule r;
    return r;
}

inline char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

// 键名比较:Vorbis 注释的键不分大小写(只有 ASCII)
inline bool keyEquals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return i == a.size() && b[i] == 0;
}

// 已有的 ENCODER / MQAENCODER 合不合格 —— 认可前缀的规则只在这一处
inline bool encoderValueOk(const std::string& v, const Rule& rule = defaultRule()) {
    if (v.empty()) return false;
    return v == rule.encoderTag || v.compare(0, rule.acceptPrefix.size(), rule.acceptPrefix) == 0;
}

// "352800" → 352800;只许数字(前后空白、正负号、前导 0 都不行);0 与超过 9 位数的不算
inline bool parseRate(const std::string& v, int& hz) {
    if (v.empty() || v.size() > 9 || v[0] == '0') return false;
    int n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    hz = n;
    return true;
}

inline bool origRateValueOk(const std::string& v, int streamOrigRate) {
    int hz = 0;
    if (!parseRate(v, hz)) return false;
    return streamOrigRate <= 0 || hz == streamOrigRate;
}

inline bool valueOk(int k, const std::string& v, int streamOrigRate, const Rule& rule = defaultRule()) {
    return k == kOrigRate ? origRateValueOk(v, streamOrigRate) : encoderValueOk(v, rule);
}

// 文件里看到的:每个键出现了几次、第一次出现的值
struct Seen {
    int count[kKeys] = { 0, 0, 0 };
    std::string value[kKeys];
};

// 每个键各自合不合格(位图:不合格的键的位是 1;0 = 三个都在、都合格、都只有一份)。
// 这是逐键的报告;文件算不算完整、要不要修,看下面的 tagsComplete
inline unsigned problems(const Seen& s, int streamOrigRate, const Rule& rule = defaultRule()) {
    unsigned bad = 0;
    for (int k = 0; k < kKeys; ++k)
        if (s.count[k] != 1 || !valueOk(k, s.value[k], streamOrigRate, rule)) bad |= 1u << k;
    return bad;
}

// 标签完整不完整 —— 规则只在这一处(定义见文件开头):bad = problems() 给的逐键报告
inline bool tagsComplete(unsigned bad) {
    const unsigned bothEncoders = kBadEncoder | kBadFormatEncoder;
    return !(bad & kBadOrigRate) && (bad & bothEncoders) != bothEncoders;
}

// ---- 从 FFmpeg 的元数据字典看(只拿得到容器元数据、不自己解析注释块的程序用这一组)----
// 字典里同名的条目会被接成 "a;b"(重复的 Vorbis 注释;FLAC 前面 ID3v2 里的同名帧,比如 TSSE → encoder),
// 所以按分号拆开,有一段合格就算这个键合格。has[k] = 字典里有没有这个键
inline bool joinedValueOk(int k, const std::string& joined, int streamOrigRate, const Rule& rule = defaultRule()) {
    size_t a = 0;
    for (;;) {
        const size_t b = joined.find(';', a);
        if (valueOk(k, joined.substr(a, b == std::string::npos ? std::string::npos : b - a), streamOrigRate, rule)) return true;
        if (b == std::string::npos) return false;
        a = b + 1;
    }
}

// 接起来的值里第一段合格的(另存时"原来合格的值留着"用);一段都不合格返回 false
inline bool firstAcceptable(int k, const std::string& joined, int streamOrigRate, const Rule& rule, std::string& out) {
    size_t a = 0;
    for (;;) {
        const size_t b = joined.find(';', a);
        const std::string part = joined.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (valueOk(k, part, streamOrigRate, rule)) { out = part; return true; }
        if (b == std::string::npos) return false;
        a = b + 1;
    }
}

inline unsigned problemsFromDict(const bool has[kKeys], const std::string value[kKeys], int streamOrigRate, const Rule& rule = defaultRule()) {
    unsigned bad = 0;
    for (int k = 0; k < kKeys; ++k)
        if (!has[k] || !joinedValueOk(k, value[k], streamOrigRate, rule)) bad |= 1u << k;
    return bad;
}

// ---- 命令行给的值能不能用(--encoder-tag / --accept-prefix)----
inline bool validUtf8(const std::string& s) {
    const unsigned char* p = (const unsigned char*)s.data();
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        size_t need = 0;
        unsigned cp = 0, floor = 0;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1Fu; floor = 0x80; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0Fu; floor = 0x800; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07u; floor = 0x10000; }
        else return false;
        if (n - i <= need) return false;
        for (size_t k = 1; k <= need; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + k] & 0x3Fu);
        }
        if (cp < floor || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;      // 过长编码 / 超范围 / 代理项
        i += need + 1;
    }
    return true;
}

inline bool hasControlChar(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return true;
    return false;
}

// 要写进 ENCODER / MQAENCODER 的值:返回空指针 = 可以用,否则是不能用的原因(messages.h 里的那一条)。
//   · 不空、不超过 kMaxEncoderTagBytes 字节、合法的 UTF-8、没有控制字符、头尾不带空格;
//   · 不带分号(按 FFmpeg 读标签的程序会把同名的几条用分号接起来,带分号的值读回来就分不清了);
//     等号可以有(注释按第一个 '=' 分键与值,值里的 '=' 原样读回);
//   · 必须带着 kWriterMark 与 kUnsignedMark:说明是标签修复工具写的、没有签名 —— 所以它不可能正好是某个官方编码器的名字
inline const msg::Entry* encoderTagProblem(const std::string& v) {
    if (v.empty()) return &msg::TagEmpty;
    if (v.size() > kMaxEncoderTagBytes) return &msg::TagTooLong;
    if (!validUtf8(v)) return &msg::TagNotUtf8;
    if (hasControlChar(v)) return &msg::TagControlChar;
    if (v.front() == ' ' || v.back() == ' ') return &msg::TagEdgeSpace;
    if (v.find(';') != std::string::npos) return &msg::TagSemicolon;
    if (v.find(kWriterMark) == std::string::npos || v.find(kUnsignedMark) == std::string::npos)
        return &msg::TagNeedsMarks;
    return nullptr;
}

// 认可已有值的前缀:可以是空的(= 只要不是空值就合格)
inline const msg::Entry* acceptPrefixProblem(const std::string& v) {
    if (v.size() > kMaxEncoderTagBytes) return &msg::PrefixTooLong;
    if (!validUtf8(v)) return &msg::PrefixNotUtf8;
    if (hasControlChar(v)) return &msg::PrefixControlChar;
    return nullptr;
}

} // namespace tagrepair
