#pragma once
// =============================================================================
// FLAC 元数据区与 Vorbis 注释(纯函数:只碰字节,不碰文件系统)
// =============================================================================
// 标签修复的核心。原则只有一条:音频帧一个字节都不动。
//   · readLayout    走一遍元数据块,给出每个块的位置与音频帧的起点;"fLaC" 前面的 ID3v2 认得、保留
//   · parseComments / buildComments   注释块拆开 / 拼回(块尾多出来的字节原样带着)
//   · planFix       算出修好之后的整段元数据:别的块、别的注释、它们的顺序、厂商串都不动;
//                   三个键修完各恰好一份;能靠 PADDING 把长度找平就原地改(音频起点不变),否则整文件重写
// 期望值与边界见 tests/unit_tests.cpp。
// =============================================================================
#include "tag_rule.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flacmeta {

constexpr uint32_t kMaxBlockBody = 0xFFFFFF;          // 块长字段 24 bit
constexpr uint64_t kMaxMetaBytes = 512ull << 20;      // 元数据区整段读进内存的上限
// 原地改写只在"改动的字节全落在文件的同一个扇区里"时用:一个扇区的写入要么整个写上、要么没写,
// 写到一半断电也不会留下半新半旧的元数据区。跨扇区的一律整文件重写后换入(换入是原子的)。
// 512 是现行磁盘里最小的逻辑扇区;文件偏移按 512 对齐的一段,在 4096 字节扇区的盘上也在同一个扇区里
constexpr uint64_t kSectorBytes = 512;
inline bool withinOneSector(uint64_t from, uint64_t end, uint64_t sectorBytes) {       // [from, end) 在不在同一个扇区里
    return sectorBytes > 0 && end > from && from / sectorBytes == (end - 1) / sectorBytes;
}
enum : uint8_t { kStreamInfo = 0, kPadding = 1, kApplication = 2, kSeekTable = 3, kVorbisComment = 4, kCueSheet = 5, kPicture = 6 };

// 没有注释块的文件,新建的注释块用的厂商串
inline const char* newVendor() { return "tagrepair"; }

// 按位置读(文件或内存)。read 要么读满 n 字节,要么失败
class Source {
public:
    virtual ~Source() {}
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t at, void* dst, size_t n) const = 0;
};

class MemSource : public Source {
public:
    MemSource(const uint8_t* d, size_t n) : m_d(d), m_n(n) {}
    uint64_t size() const override { return m_n; }
    bool read(uint64_t at, void* dst, size_t n) const override;
private:
    const uint8_t* m_d;
    size_t m_n;
};

struct BlockRef {
    uint8_t  type = 0;
    bool     last = false;
    uint64_t at = 0;            // 4 字节块头的位置
    uint32_t len = 0;           // 块体长度
};

struct Layout {
    uint64_t id3Bytes = 0;      // "fLaC" 前面的 ID3v2 占了多少字节(0 = 没有)
    uint64_t metaAt = 0;        // 第一个块头(= id3Bytes + 4)
    uint64_t audioAt = 0;       // 第一个音频帧
    uint64_t fileSize = 0;
    std::vector<BlockRef> blocks;
    uint8_t  streamInfo[34] = {};
    int      sampleRate = 0, channels = 0, bits = 0;
    uint64_t totalSamples = 0;  // 0 = 文件里没写
};

enum class Err { Ok, NotFlac, Truncated, Corrupt, Io };
// why:走不通的原因(messages.h 里的那一条;走得通时是空指针)
Err readLayout(const Source& s, Layout& out, const msg::Entry*& why);

// ---- Vorbis 注释 ----
struct Comments {
    std::string vendor;
    std::vector<std::string> items;     // 每条原样的 "KEY=value" 字节(不校验编码)
    std::vector<uint8_t> tail;          // 最后一条之后多出来的字节(规范里没有,见到了就原样带着)
};
bool parseComments(const uint8_t* d, size_t n, Comments& out);
std::vector<uint8_t> buildComments(const Comments& c);
// "KEY=value" → 键、值;没有 '=' 的条目返回 false(这种条目不当任何键)
bool splitItem(const std::string& item, std::string& key, std::string& value);

// ---- 元数据区(从第一个块头到音频帧之前)拆成块 / 拼回 ----
struct MetaBlock {
    uint8_t type = 0;
    std::vector<uint8_t> body;
};
bool splitBlocks(const uint8_t* meta, size_t n, std::vector<MetaBlock>& out);
std::vector<uint8_t> joinBlocks(const std::vector<MetaBlock>& b);       // "最后一块"标志按位置重新标

// ---- 三个键 ----
tagrepair::Seen seenIn(const std::vector<Comments>& vcs);                  // 所有注释块合起来数
bool seenInMeta(const uint8_t* meta, size_t n, tagrepair::Seen& out);      // 直接从元数据区数;区或注释块坏了返回 false
// 修完之后每个键的值:文件里已有合格的(按出现顺序第一份)就留它,否则 ENCODER / MQAENCODER 用 rule.encoderTag、
// ORIGINALSAMPLERATE 用码流里的原始采样率。码流里没读到、文件里也没有合格的:fin[kOrigRate] 为空(调用方不修)
void chooseFinal(const std::vector<Comments>& vcs, int streamOrigRate, std::string fin[tagrepair::kKeys],
                 const tagrepair::Rule& rule = tagrepair::defaultRule());
// 把注释改成"三个键各恰好一份、值为 fin":已经只有一份且值对的原样不动;别的条目与顺序不动。返回改没改
bool applyTags(std::vector<Comments>& vcs, const std::string fin[tagrepair::kKeys]);

// ---- 新写一个 FLAC 文件时的 STREAMINFO(34 字节)----
// 另存(copy_flac.cpp)用:FFmpeg 的编码器只出音频帧,文件头是我们自己拼的
struct StreamInfoFields {
    int minBlock = 0, maxBlock = 0;         // 块长(采样数)
    uint32_t minFrame = 0, maxFrame = 0;    // 最短 / 最长的帧(字节;0 = 不知道)
    int sampleRate = 0, channels = 0, bits = 0;
    uint64_t totalSamples = 0;              // 每声道的采样数
    uint8_t md5[16] = {};                   // 全部采样(交错、小端、每个采样 bits / 8 字节)的 MD5
};
void buildStreamInfo(const StreamInfoFields& f, uint8_t out[34]);

enum class Mode { NoChange, InPlace, Rewrite };
enum class PlanErr { Ok, BadMeta, BadComment, TooBig, Internal };
struct Plan {
    Mode mode = Mode::NoChange;
    std::vector<uint8_t> newMeta;       // 修好之后的整段元数据(NoChange 时为空)
    bool   sameSize = false;            // 与原来一样长(音频起点不变)
    size_t diffAt = 0, diffEnd = 0;     // sameSize 时:与原来不同的字节范围 [diffAt, diffEnd)
};
// metaAtInFile = 元数据区在文件里的偏移(第一个块头);长度找平、而且改动的字节在文件里落在同一个扇区内才是 InPlace
PlanErr planFix(const std::vector<uint8_t>& oldMeta, const std::string fin[tagrepair::kKeys], uint64_t metaAtInFile, uint64_t sectorBytes, Plan& out);

} // namespace flacmeta
