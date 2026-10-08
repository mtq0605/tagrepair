#pragma once
// =============================================================================
// 测试用的合成 FLAC(纯函数):自检与 tests/ 不靠任何外部文件
// =============================================================================
// 音频帧用 VERBATIM 子帧(样本原样存),FFmpeg 能解;内容是低电平噪声,不含任何音乐。
// "带标记"的版本只在左右声道某一位(默认是 24 bit 样本的第 8 位;16 bit 样本就是最低位)的异或里放一条数据同步记录
// (同步标记 + 采样率码 + 校验;格式事实取自公开资料,写在 sync_detect.h 开头)——
// 够 syncdetect::Detector 认出来并读到原始采样率;它不是一条完整的流,没有任何别的内容。
// 这里拼记录的代码与 sync_detect.cpp 读记录的代码是分开写的两份,互相核对。
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace synth {

uint8_t  crc8(const uint8_t* d, size_t n);       // FLAC 帧头:多项式 0x07
uint16_t crc16(const uint8_t* d, size_t n);      // FLAC 帧尾:多项式 0x8005

// 立体声交错、右对齐的噪声样本(|值| < 2^(bits−6))
std::vector<int32_t> noise(size_t frames, int bits, uint32_t seed);

// 采样率码(5 位:高 2 位基准 44100 / 48000 / 64000,低 3 位 2 的幂);不是这种率返回 −1
int rateCode(int hz);

// 一条数据同步记录要填的东西。默认 = 流的第一条记录:不带位置、没有附加项
struct Record {
    unsigned originalCode = 0;                                // 原始采样率的码
    unsigned carrierCode = 0;                                 // 载体采样率的码
    unsigned otherFields = 0;                                 // 第 52 位起的 21 位(检测不读)
    bool hasPosition = false;                                 // 带不带位置
    uint32_t position = 0;
    std::vector<std::pair<unsigned, unsigned>> items;         // 附加项:(类型,长度位数);内容填伪随机位
    uint32_t fillSeed = 1;
};
// 记录的控制位(先出的在前),末尾 4 位是按规则算出来的校验
std::vector<uint8_t> recordBits(const Record& r);
// 校验字段之前的那些位(bits 的前 count 个)应有的 4 位校验;position = 记录在控制流里的位置
unsigned checkOf(const std::vector<uint8_t>& bits, size_t count, uint32_t position);

// 开流的那条记录(84 个控制位):原始采样率、载体采样率
std::vector<uint8_t> startRecordBits(int origRateHz, int carrierRateHz);

// 把控制位写进 lr(交错、右对齐、bits 位样本)从第 atFrame 帧起的 L^R 的某一位。
// sampleBit = 按 24 bit 样本算的位号(8..15);bits 位的样本里就是第 sampleBit − (24 − bits) 位
void embedControl(std::vector<int32_t>& lr, int bits, size_t atFrame, const std::vector<uint8_t>& ctrl, int sampleBit = 8);

// 音频帧(固定块长,最后一块可以短)。minFrame / maxFrame 给 STREAMINFO 用
std::vector<uint8_t> flacFrames(const std::vector<int32_t>& lr, int rate, int bits, int blockSize, uint32_t* minFrame = nullptr,
                                uint32_t* maxFrame = nullptr);

std::vector<uint8_t> streamInfo(int rate, int channels, int bits, uint64_t totalSamples, int blockSize, uint32_t minFrame, uint32_t maxFrame,
                                const uint8_t md5[16]);

std::vector<uint8_t> vorbisBody(const std::string& vendor, const std::vector<std::string>& items);

// 一个元数据块(类型,块体);last 标志由 flacFile 按位置标
using Block = std::pair<uint8_t, std::vector<uint8_t>>;

// 整个文件:[prefix(比如 ID3v2)] "fLaC" + 各块 + frames + [suffix(比如 ID3v1)]
std::vector<uint8_t> flacFile(const std::vector<Block>& blocks, const std::vector<uint8_t>& frames, const std::vector<uint8_t>& prefix = {},
                              const std::vector<uint8_t>& suffix = {});

// 一个 ID3v2.3 标签(bodyBytes 字节的填充体)
std::vector<uint8_t> id3v2(size_t bodyBytes);

} // namespace synth
