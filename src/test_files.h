#pragma once
// =============================================================================
// 测试用的文件:自检(selftest.cpp)与 tests/ 共用的那几样 —— 造一个完整的合成 FLAC / WAV、把文件拆开看、
// 把三个标签拿掉。全部是按格式拼的字节(synth_flac.*),不含任何音乐
// =============================================================================
#include "flac_meta.h"
#include "synth_flac.h"

#include <cstdint>
#include <string>
#include <vector>

namespace testfiles {

typedef std::vector<uint8_t> Bytes;

bool writeFile(const std::wstring& path, const Bytes& d);
Bytes readFile(const std::wstring& path);

// 一段立体声噪声;markAt ≥ 0 时从那一帧起放一条开流的数据同步记录(原始采样率 origRate)
std::vector<int32_t> pcm(int rate, int bits, double seconds, long long markAt, int origRate, uint32_t seed);
// 样本 → 完整的 FLAC 文件;extra = STREAMINFO 之后的块
Bytes flac(const std::vector<int32_t>& lr, int rate, int bits, const std::vector<synth::Block>& extra, const Bytes& prefix = {},
           const Bytes& suffix = {});
Bytes wav(const std::vector<int32_t>& lr, int rate, int bits, int formatTag = 1);
Bytes vc(const std::string& vendor, const std::vector<std::string>& items);
Bytes zeros(size_t n);
// 一张 1×1 的 PNG 包成 PICTURE 块(FFmpeg 会解析这个块,得是合法的)
Bytes picture();

struct Parsed {
    bool ok = false;
    flacmeta::Layout lay;
    Bytes meta, audio;                                        // 元数据区;音频帧到文件末尾
    std::vector<flacmeta::MetaBlock> blocks;
    std::vector<flacmeta::Comments> vcs;
    tagrepair::Seen seen;
};
Parsed parse(const Bytes& file);

// 把三个标签从一个 FLAC 里拿掉(造"丢了标签"的情况):别的注释、别的块、音频字节都不变。解析不了返回 false
bool stripTags(const Bytes& file, Bytes& out);

} // namespace testfiles
