// =============================================================================
// tagrepair_tests —— 纯函数部分的核对(不需要 FFmpeg),外加两个造测试文件的小命令
// =============================================================================
//   tagrepair_tests                      跑全部核对;有一条不过退出码就是 1
//   tagrepair_tests samples <文件夹>      在这个(新的、空的)文件夹里造一套合成文件:各种丢了标签的带同步标记的 FLAC、
//                                             结构古怪或损坏的、没有同步标记的、别的容器的 —— 拿来比两个版本的行为、试拖放
//   tagrepair_tests strip <入.flac> <出.flac>   把三个标签从一个 FLAC 里拿掉、另存(造"丢了标签"的情况)。
//                                             入、出是同一个文件或者出已经存在都拒绝:这个命令从不改已有的文件
//
// 核对挡的错都是"会把文件改坏 / 改得不对"的那几种:
// 块长读歪一位、走到音频帧里去;ID3v2 在前面时起点错;注释块拆开再拼回少了字节(块尾多出的字节丢了);
// 键名分大小写(encoder=Lavf… 没被换掉,又多写一份);重复的键留了一份旧的;第二个注释块里的旧值留着;
// 别的注释 / 别的块(图片、定位表、应用块、CUE)被动了或换了顺序;厂商串被换;
// PADDING 少算 / 多算一个字节(音频起点挪了);剩 1–3 字节凑不出块头还硬写;"最后一块"标志丢了或有两个;
// 已经完整的文件还去动;修过一次再修还在变;超过 16 MB 的块照写;没有同步标记的被判成能修。
// 这里的文件全部是按格式手工拼的字节(synth_flac.*),不含任何音乐。真文件上的写入与读回核对在 `tagrepair selftest`。
// =============================================================================
#include "../src/classify.h"
#include "../src/cli.h"
#include "../src/flac_meta.h"
#include "../src/json_out.h"
#include "../src/probe.h"
#include "../src/sync_detect.h"
#include "../src/synth_flac.h"
#include "../src/test_files.h"
#include "../src/version.h"
#include "../src/win_util.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace flacmeta;

static int g_pass = 0, g_fail = 0;

static void say(const std::string& utf8Line) { wu::outLineUtf8(utf8Line); }

static void expect(const char* name, bool ok, const char* note = nullptr) {
    if (ok) ++g_pass; else ++g_fail;
    std::string line = std::string(ok ? "[通过] " : "[失败] ") + name;
    if (note && *note) line += std::string(" —— ") + note;
    say(line);
}

namespace {

// 期望值从 tag_rule.h 的两个常量来(默认写的值、认可的前缀),不在这里再抄一份
const std::string kOurs = tagrepair::kDefaultEncoderTag;
const std::string kPrefix = tagrepair::kDefaultAcceptPrefix;
const std::string kOfficial = kPrefix + " v1.1, 0.0.0+000 (0000000), 00000000-0000-0000-0000-000000000000, Jan 01 2000 00:00:00";
const uint64_t kOneBigSector = 1ull << 40;                   // 扇区条件不是这一项要验的东西时:当整个文件在一个扇区里

std::vector<uint8_t> fakeFrames(size_t n = 64) {              // 以帧同步码开头的一段"音频"(这里不解码,只当不许动的字节)
    std::vector<uint8_t> f(n);
    for (size_t i = 0; i < n; ++i) f[i] = (uint8_t)(i * 37 + 11);
    f[0] = 0xFF;
    f[1] = 0xF8;
    return f;
}

std::vector<uint8_t> si() {
    static const uint8_t md5[16] = { 0 };
    return synth::streamInfo(44100, 2, 24, 441000, 4096, 14, 24586, md5);
}

std::vector<uint8_t> fill(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)(seed + i * 7);
    return v;
}

// 文件 → 元数据区
std::vector<uint8_t> metaOf(const std::vector<uint8_t>& file, Layout* lay = nullptr) {
    MemSource src(file.data(), file.size());
    Layout l;
    const msg::Entry* why = nullptr;
    if (readLayout(src, l, why) != Err::Ok) return {};
    if (lay) *lay = l;
    return std::vector<uint8_t>(file.begin() + (std::ptrdiff_t)l.metaAt, file.begin() + (std::ptrdiff_t)l.audioAt);
}

// 元数据区里所有注释块
std::vector<Comments> commentsOf(const std::vector<uint8_t>& meta, std::vector<MetaBlock>* blocksOut = nullptr) {
    std::vector<MetaBlock> blocks;
    std::vector<Comments> vcs;
    if (!splitBlocks(meta.data(), meta.size(), blocks)) return vcs;
    for (const MetaBlock& b : blocks)
        if (b.type == kVorbisComment) {
            Comments c;
            parseComments(b.body.data(), b.body.size(), c);
            vcs.push_back(c);
        }
    if (blocksOut) *blocksOut = blocks;
    return vcs;
}

struct Fixed {
    PlanErr err = PlanErr::Internal;
    Plan plan;
    std::string fin[tagrepair::kKeys];
};

Fixed fix(const std::vector<uint8_t>& meta, int streamRate, uint64_t metaAt = 4, uint64_t sector = kOneBigSector) {
    Fixed f;
    chooseFinal(commentsOf(meta), streamRate, f.fin);
    f.err = planFix(meta, f.fin, metaAt, sector, f.plan);
    return f;
}

bool exactlyOnce(const std::vector<uint8_t>& meta, int streamRate) {
    tagrepair::Seen s;
    return seenInMeta(meta.data(), meta.size(), s) && tagrepair::problems(s, streamRate) == 0;
}

int lastFlags(const std::vector<uint8_t>& meta, bool* onFinal) {     // "最后一块"标志有几个;是不是在最后一块上
    int n = 0;
    size_t pos = 0;
    bool fin = false;
    while (pos + 4 <= meta.size()) {
        const bool last = (meta[pos] & 0x80) != 0;
        const size_t len = ((size_t)meta[pos + 1] << 16) | ((size_t)meta[pos + 2] << 8) | meta[pos + 3];
        if (last) ++n;
        pos += 4 + len;
        fin = last && pos == meta.size();
    }
    if (onFinal) *onFinal = fin;
    return n;
}

} // namespace

static void runChecks() {
    say("== 标签修复:FLAC 元数据块 / Vorbis 注释 / 键匹配 / PADDING 找平 / 分类(纯函数)==");
    char note[512];
    const std::vector<uint8_t> frames = fakeFrames();
    const std::vector<std::string> plainTags = { "TITLE=Curve", "ARTIST=Nobody", "encoder=Lavf58.76.100", "DATE=2026" };

    // ---- 1 块走一遍 ----
    {
        const std::vector<uint8_t> vc = synth::vorbisBody("reference libFLAC 1.3.2 20170101", plainTags);
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kSeekTable, fill(36, 1) }, { kVorbisComment, vc }, { kPicture, fill(300, 9) }, { kPadding, std::vector<uint8_t>(100, 0) } },
            frames);
        MemSource src(file.data(), file.size());
        Layout l;
        const msg::Entry* why = nullptr;
        const Err e = readLayout(src, l, why);
        const uint64_t wantAudio = 4 + (4 + 34) + (4 + 36) + (4 + vc.size()) + (4 + 300) + (4 + 100);
        snprintf(note, sizeof note, "块 %zu 个,音频起点 %llu(应为 %llu),STREAMINFO %d Hz / %d bit / %d 声道 / %llu 个样本", l.blocks.size(),
                 (unsigned long long)l.audioAt, (unsigned long long)wantAudio, l.sampleRate, l.bits, l.channels, (unsigned long long)l.totalSamples);
        expect("1 五个块:类型、位置、长度、音频起点、STREAMINFO 的四个量",
               e == Err::Ok && l.blocks.size() == 5 && l.blocks[0].type == kStreamInfo && l.blocks[1].type == kSeekTable &&
                   l.blocks[2].type == kVorbisComment && l.blocks[3].type == kPicture && l.blocks[4].type == kPadding && l.blocks[4].last &&
                   !l.blocks[3].last && l.blocks[2].at == 4 + 38 + 40 && l.blocks[2].len == vc.size() && l.metaAt == 4 && l.audioAt == wantAudio &&
                   l.fileSize == file.size() && l.sampleRate == 44100 && l.bits == 24 && l.channels == 2 && l.totalSamples == 441000 &&
                   l.id3Bytes == 0,
               note);
    }
    // ---- 2 ID3v2 在 "fLaC" 前面 ----
    {
        std::vector<uint8_t> id3 = synth::id3v2(300);         // 300 = 0x12C:跨两个 7 bit 字节
        const std::vector<uint8_t> file = synth::flacFile({ { kStreamInfo, si() } }, frames, id3);
        MemSource src(file.data(), file.size());
        Layout l;
        const msg::Entry* why = nullptr;
        const Err e = readLayout(src, l, why);
        id3[5] = 0x10;                                        // 带 10 字节的尾
        std::vector<uint8_t> id3f(id3);
        id3f.resize(id3f.size() + 10, 0);
        const std::vector<uint8_t> file2 = synth::flacFile({ { kStreamInfo, si() } }, frames, id3f);
        MemSource src2(file2.data(), file2.size());
        Layout l2;
        const Err e2 = readLayout(src2, l2, why);
        snprintf(note, sizeof note, "ID3v2 占 %llu 字节(应为 310),带尾的 %llu(应为 320)", (unsigned long long)l.id3Bytes,
                 (unsigned long long)l2.id3Bytes);
        expect("2 ID3v2 认得:起点按 7 bit × 4 的长度算,带尾的再加 10", e == Err::Ok && l.id3Bytes == 310 && l.metaAt == 314 &&
                                                                              l.audioAt == 314 + 38 && e2 == Err::Ok && l2.id3Bytes == 320,
               note);
    }
    // ---- 3 坏文件 ----
    {
        const msg::Entry* why = nullptr;
        Layout l;
        auto run = [&](const std::vector<uint8_t>& f) {
            MemSource s(f.data(), f.size());
            return readLayout(s, l, why);
        };
        const std::vector<uint8_t> good = synth::flacFile({ { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", plainTags) } }, frames);
        const std::vector<uint8_t> riff = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0, 0, 0, 0 };
        const bool notFlac = run(riff) == Err::NotFlac;
        std::vector<uint8_t> cutHdr(good.begin(), good.begin() + 4 + 38 + 2);          // 第二个块头只剩 2 字节
        const bool t1 = run(cutHdr) == Err::Truncated;
        std::vector<uint8_t> cutBody(good.begin(), good.begin() + 4 + 38 + 4 + 10);    // 注释块体不全
        const bool t2 = run(cutBody) == Err::Truncated;
        std::vector<uint8_t> noAudio(good.begin(), good.end() - (std::ptrdiff_t)frames.size());   // 元数据完整、音频一帧没有
        const bool t3 = run(noAudio) == Err::Truncated;
        std::vector<uint8_t> badLen(good);
        badLen[4 + 38 + 3] = (uint8_t)(badLen[4 + 38 + 3] - 1);                        // 注释块长少 1:后面不是帧同步码
        const bool c1 = run(badLen) == Err::Corrupt;
        std::vector<uint8_t> noSi = synth::flacFile({ { kVorbisComment, synth::vorbisBody("v", {}) }, { kStreamInfo, si() } }, frames);
        const bool c2 = run(noSi) == Err::Corrupt;
        std::vector<uint8_t> t127(good);
        t127[4 + 38] = 0x80 | 127;
        const bool c3 = run(t127) == Err::Corrupt;
        const bool ok = run(good) == Err::Ok;
        snprintf(note, sizeof note, "不是 FLAC %d;块头不全 %d;块体不全 %d;没有音频 %d;块长错一位 %d;首块不是 STREAMINFO %d;类型 127 %d;好文件 %d",
                 notFlac, t1, t2, t3, c1, c2, c3, ok);
        expect("3 截断 / 损坏的文件一律报出来(各自的原因),好文件通过", notFlac && t1 && t2 && t3 && c1 && c2 && c3 && ok, note);
    }
    // ---- 4 注释拆开 / 拼回 ----
    {
        std::vector<uint8_t> body = synth::vorbisBody("libFLAC 1.4.3", { "A=1", "novalue", "B=x=y", "" });
        Comments c;
        const bool p = parseComments(body.data(), body.size(), c);
        const bool same = p && buildComments(c) == body;
        std::vector<uint8_t> withTail(body);
        withTail.push_back(0x01);
        withTail.push_back(0xEE);
        Comments ct;
        const bool pt = parseComments(withTail.data(), withTail.size(), ct);
        const bool tailKept = pt && ct.tail.size() == 2 && buildComments(ct) == withTail;
        std::vector<uint8_t> over(body);
        over[0] = 0xFF;                                       // 厂商串长度超出块
        Comments x;
        const bool r1 = !parseComments(over.data(), over.size(), x);
        std::vector<uint8_t> cnt(body);
        cnt[4 + 13] = 200;                                    // 条数比剩下的字节装得下的多
        const bool r2 = !parseComments(cnt.data(), cnt.size(), x);
        std::vector<uint8_t> cutItem(body.begin(), body.end() - 3);
        const bool r3 = !parseComments(cutItem.data(), cutItem.size(), x);
        snprintf(note, sizeof note, "条目 %zu 条,厂商串 \"%s\";逐字节拼回 %d;块尾 2 字节带着 %d;三种坏法拒掉 %d %d %d", c.items.size(),
                 c.vendor.c_str(), same, tailKept, r1, r2, r3);
        expect("4 注释块拆开再拼回逐字节相同(含空条目、没有等号的条目、块尾多出的字节);长度越界的拒掉",
               same && c.items.size() == 4 && c.vendor == "libFLAC 1.4.3" && tailKept && r1 && r2 && r3, note);
    }
    // ---- 5 键匹配 ----
    {
        std::string k, v;
        const bool a = splitItem("B=x=y", k, v) && k == "B" && v == "x=y";
        const bool b = !splitItem("novalue", k, v);
        const bool c = tagrepair::keyEquals("encoder", "ENCODER") && tagrepair::keyEquals("EnCoDeR", "ENCODER") &&
                       tagrepair::keyEquals("originalsamplerate", "ORIGINALSAMPLERATE");
        const bool d = !tagrepair::keyEquals("ENCODERX", "ENCODER") && !tagrepair::keyEquals("ENCODE", "ENCODER") &&
                       !tagrepair::keyEquals("MQAENCODER", "ENCODER") && !tagrepair::keyEquals("", "ENCODER") &&
                       !tagrepair::keyEquals("ENCODER ", "ENCODER");
        std::vector<Comments> vcs(1);
        vcs[0].items = { "encoder=Lavf", "ENCODEDBY=me", "MqaEncoder=" + std::string(kOfficial), "ENCODER=second", "ORIGINALSAMPLERATE" };
        const tagrepair::Seen s = seenIn(vcs);
        snprintf(note, sizeof note, "ENCODER %d 份(第一份 \"%s\"),MQAENCODER %d 份,ORIGINALSAMPLERATE %d 份", s.count[0], s.value[0].c_str(),
                 s.count[1], s.count[2]);
        expect("5 键名不分大小写、要整个相等(ENCODEDBY / ENCODERX 不算),值里的等号留着,没有等号的条目不算键",
               a && b && c && d && s.count[0] == 2 && s.value[0] == "Lavf" && s.count[1] == 1 && s.value[1] == kOfficial && s.count[2] == 0, note);
    }
    // ---- 6 合格的定义 ----
    {
        using namespace tagrepair;
        int hz = 0;
        std::string lower = kPrefix;                          // 前缀的小写写法、前面带空白、没写全、不在开头:都不算
        for (char& ch : lower) ch = lowerAscii(ch);
        const bool enc = encoderValueOk(kOurs) && encoderValueOk(kOfficial) && encoderValueOk(kPrefix) && (lower == kPrefix || !encoderValueOk(lower + " x")) &&
                         !encoderValueOk("  " + kPrefix) && !encoderValueOk("Lavf58.76.100") && !encoderValueOk("") &&
                         !encoderValueOk(kPrefix.substr(0, kPrefix.size() - 1)) && !encoderValueOk("x " + kPrefix);
        const bool rate = parseRate("352800", hz) && hz == 352800 && parseRate("96000", hz) && hz == 96000 && !parseRate(" 96000 ", hz) &&
                          !parseRate("96000 ", hz) && !parseRate("096000", hz) && !parseRate("+96000", hz) && !parseRate("", hz) &&
                          !parseRate("0", hz) && !parseRate("96k", hz) && !parseRate("44100;352800", hz) && !parseRate("-44100", hz) &&
                          !parseRate("1234567890", hz) && parseRate("999999999", hz) && hz == 999999999;
        const bool rok = origRateValueOk("352800", 352800) && !origRateValueOk("96000", 352800) && origRateValueOk("96000", 0) &&
                         !origRateValueOk("abc", 0);
        Seen s;
        const unsigned none = problems(s, 352800);
        s.count[0] = s.count[1] = s.count[2] = 1;
        s.value[0] = kOfficial;
        s.value[1] = kOurs;
        s.value[2] = "352800";
        const unsigned full = problems(s, 352800);
        const unsigned wrongRate = problems(s, 96000);
        s.count[1] = 2;
        const unsigned dup = problems(s, 352800);
        s.count[1] = 1;
        s.value[0] = "Lavf";
        const unsigned badEnc = problems(s, 352800);
        snprintf(note, sizeof note, "全缺 %u(应 7),齐全 %u(应 0),采样率不符 %u(应 4),MQAENCODER 两份 %u(应 2),ENCODER=Lavf %u(应 1)", none, full,
                 wrongRate, dup, badEnc);
        expect("6 合格 = 以认可的前缀开头(大小写、位置都要对)/ 只有数字的整数且等于码流里的原始采样率 / 恰好一份;各自报在自己的位上",
               enc && rate && rok && none == 7 && full == 0 && wrongRate == kBadOrigRate && dup == kBadFormatEncoder && badEnc == kBadEncoder, note);
    }
    // ---- 7 三个都没有:加在末尾,别的条目、顺序、厂商串不动 ----
    {
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("vendor X", { "TITLE=Curve", "ARTIST=Nobody" }) },
              { kPadding, std::vector<uint8_t>(400, 0) } },
            frames);
        const std::vector<uint8_t> meta = metaOf(file);
        const Fixed f = fix(meta, 352800);
        const std::vector<Comments> vcs = commentsOf(f.plan.newMeta);
        const std::vector<std::string> want = { "TITLE=Curve", "ARTIST=Nobody", std::string("ENCODER=") + kOurs, std::string("MQAENCODER=") + kOurs,
                                                "ORIGINALSAMPLERATE=352800" };
        expect("7 三个标签都没有:加在注释末尾(ENCODER、MQAENCODER、ORIGINALSAMPLERATE),原有条目与顺序、厂商串不动",
               f.err == PlanErr::Ok && f.plan.mode == Mode::InPlace && vcs.size() == 1 && vcs[0].vendor == "vendor X" && vcs[0].items == want &&
                   exactlyOnce(f.plan.newMeta, 352800));
    }
    // ---- 8 小写的 encoder=Lavf…:原位换掉,不多写一份 ----
    {
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", plainTags) }, { kPadding, std::vector<uint8_t>(400, 0) } }, frames);
        const Fixed f = fix(metaOf(file), 192000);
        const std::vector<Comments> vcs = commentsOf(f.plan.newMeta);
        const std::vector<std::string> want = { "TITLE=Curve", "ARTIST=Nobody", std::string("ENCODER=") + kOurs, "DATE=2026",
                                                std::string("MQAENCODER=") + kOurs, "ORIGINALSAMPLERATE=192000" };
        const tagrepair::Seen s = seenIn(vcs);
        snprintf(note, sizeof note, "修完 ENCODER %d 份、共 %zu 条", s.count[0], vcs.empty() ? (size_t)0 : vcs[0].items.size());
        expect("8 已有的 encoder=Lavf…(小写键)在原来的位置换成合格的值,不留旧的、不重复", f.err == PlanErr::Ok && vcs.size() == 1 &&
                                                                                                 vcs[0].items == want && s.count[0] == 1,
               note);
    }
    // ---- 9 重复的键只留一份;已有的官方值留着 ----
    {
        const std::vector<std::string> items = { std::string("ENCODER=") + kOfficial, "TITLE=T", "encoder=Lavf", "ORIGINALSAMPLERATE=44100",
                                                 "originalsamplerate=352800", std::string("MQAENCODER=") + kOfficial,
                                                 std::string("MqaEncoder=") + kOfficial };
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", items) }, { kPadding, std::vector<uint8_t>(64, 0) } }, frames);
        const Fixed f = fix(metaOf(file), 352800);
        const std::vector<Comments> vcs = commentsOf(f.plan.newMeta);
        const std::vector<std::string> want = { std::string("ENCODER=") + kOfficial, "TITLE=T", "ORIGINALSAMPLERATE=352800",
                                                std::string("MQAENCODER=") + kOfficial };
        expect("9 重复的键只留一份:合格的官方 ENCODER / MQAENCODER 原样留着,错的 ORIGINALSAMPLERATE 换成码流里的",
               f.err == PlanErr::Ok && vcs.size() == 1 && vcs[0].items == want && exactlyOnce(f.plan.newMeta, 352800));
    }
    // ---- 10 两个注释块:第二个里的旧值不许留 ----
    {
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("first", { "TITLE=T" }) }, { kPicture, fill(50, 3) },
              { kVorbisComment, synth::vorbisBody("second", { "ENCODER=Lavf", "COMMENT=keep me", "ORIGINALSAMPLERATE=44100" }) },
              { kPadding, std::vector<uint8_t>(400, 0) } },
            frames);
        const Fixed f = fix(metaOf(file), 352800);
        std::vector<MetaBlock> blocks;
        const std::vector<Comments> vcs = commentsOf(f.plan.newMeta, &blocks);
        const bool shape = vcs.size() == 2 && vcs[0].vendor == "first" && vcs[1].vendor == "second" &&
                           vcs[1].items == std::vector<std::string>{ "COMMENT=keep me" } && vcs[0].items.size() == 4 && vcs[0].items[0] == "TITLE=T";
        expect("10 两个注释块:三个键只在第一个块里各一份,第二个块里的旧 ENCODER / ORIGINALSAMPLERATE 删掉、它别的注释留着",
               f.err == PlanErr::Ok && shape && exactlyOnce(f.plan.newMeta, 352800) && blocks.size() == 5 && blocks[2].type == kPicture &&
                   blocks[2].body == fill(50, 3));
    }
    // ---- 11 已经完整的不动;修过的再修不变 ----
    {
        const std::vector<std::string> items = { "TITLE=T", std::string("encoder=") + kOfficial, std::string("MQAENCODER=") + kOurs,
                                                 "ORIGINALSAMPLERATE=352800" };
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", items) }, { kPadding, std::vector<uint8_t>(64, 0) } }, frames);
        const Fixed f = fix(metaOf(file), 352800);
        const std::vector<uint8_t> file2 = synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", plainTags) }, { kPadding, std::vector<uint8_t>(400, 0) } }, frames);
        const Fixed g = fix(metaOf(file2), 352800);
        const Fixed again = fix(g.plan.newMeta, 352800);
        expect("11 三个标签齐全且正确(键是小写也算)的文件不动;修过一次的再修也不动",
               f.err == PlanErr::Ok && f.plan.mode == Mode::NoChange && f.plan.newMeta.empty() && g.plan.mode == Mode::InPlace &&
                   again.err == PlanErr::Ok && again.plan.mode == Mode::NoChange);
    }
    // ---- 12 PADDING 找平:够、刚好用光块体、刚好连块头用光、差 1–3 字节 ----
    {
        // 加三个标签要多出的字节数:三条各 4 字节长度 + 文本
        const size_t need = (4 + 8 + kOurs.size()) + (4 + 11 + kOurs.size()) + (4 + 19 + 6);
        auto mk = [&](const std::vector<size_t>& pads) {
            std::vector<synth::Block> b = { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", { "TITLE=T" }) } };
            for (size_t p : pads) b.push_back({ kPadding, std::vector<uint8_t>(p, 0) });
            return metaOf(synth::flacFile(b, frames));
        };
        auto padBodies = [&](const std::vector<uint8_t>& meta) {
            std::vector<size_t> v;
            std::vector<MetaBlock> blocks;
            splitBlocks(meta.data(), meta.size(), blocks);
            for (const MetaBlock& b : blocks)
                if (b.type == kPadding) v.push_back(b.body.size());
            return v;
        };
        const std::vector<uint8_t> m1 = mk({ need + 100 });
        const Fixed a = fix(m1, 352800);
        const bool roomy = a.plan.mode == Mode::InPlace && a.plan.sameSize && a.plan.newMeta.size() == m1.size() &&
                           padBodies(a.plan.newMeta) == std::vector<size_t>{ 100 };
        const Fixed b = fix(mk({ need }), 352800);
        const bool exactBody = b.plan.mode == Mode::InPlace && padBodies(b.plan.newMeta) == std::vector<size_t>{ 0 };
        const Fixed c = fix(mk({ need - 4 }), 352800);
        const bool exactBlock = c.plan.mode == Mode::InPlace && c.plan.sameSize && padBodies(c.plan.newMeta).empty();
        bool shortBy = true;
        for (size_t k = 1; k <= 3; ++k) {
            const Fixed d = fix(mk({ need - k }), 352800);      // 块体差 k 字节:块头又不能留半个
            shortBy = shortBy && d.err == PlanErr::Ok && d.plan.mode == Mode::Rewrite && !d.plan.sameSize &&
                      padBodies(d.plan.newMeta) == std::vector<size_t>{ need - k } && exactlyOnce(d.plan.newMeta, 352800);
        }
        const Fixed e = fix(mk({ need - 2, 10 }), 352800);       // 第一块用光还差 2 字节,从第二块里出
        const bool twoPads = e.plan.mode == Mode::InPlace && padBodies(e.plan.newMeta) == std::vector<size_t>{ 0, 8 };
        const Fixed g = fix(mk({}), 352800);
        const bool noPad = g.plan.mode == Mode::Rewrite && !g.plan.sameSize && g.plan.newMeta.size() == mk({}).size() + need;
        snprintf(note, sizeof note, "要多 %zu 字节;够 %d;刚好用光块体 %d;连块头用光 %d;差 1–3 字节改整文件重写 %d;两块凑 %d;没有 PADDING %d", need, roomy,
                 exactBody, exactBlock, shortBy, twoPads, noPad);
        expect("12 PADDING 找平按字节算:总长不变才原地改;块体缩到 0、整块用掉都对;差 1–3 字节凑不出块头就整文件重写",
               roomy && exactBody && exactBlock && shortBy && twoPads && noPad, note);
    }
    // ---- 13 变短:省下的字节还给 PADDING;没有 PADDING 时够 4 字节就新建,不够就重写 ----
    {
        const std::string longEnc = "ENCODER=" + std::string(100, 'x');
        const std::vector<std::string> items = { longEnc, std::string("MQAENCODER=") + kOurs, "ORIGINALSAMPLERATE=352800" };
        const size_t saved = 100 - kOurs.size();
        const std::vector<uint8_t> withPad = metaOf(synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", items) }, { kPadding, std::vector<uint8_t>(10, 0) } }, frames));
        const Fixed a = fix(withPad, 352800);
        std::vector<MetaBlock> ba;
        commentsOf(a.plan.newMeta, &ba);
        const bool grew = a.plan.mode == Mode::InPlace && ba.size() == 3 && ba[2].type == kPadding && ba[2].body.size() == 10 + saved;
        const std::vector<uint8_t> noPad = metaOf(synth::flacFile({ { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", items) },
                                                                    { kPicture, fill(20, 5) } }, frames));
        const Fixed b = fix(noPad, 352800);
        std::vector<MetaBlock> bb;
        commentsOf(b.plan.newMeta, &bb);
        const bool made = b.plan.mode == Mode::InPlace && bb.size() == 4 && bb[2].type == kPadding && bb[2].body.size() == saved - 4 &&
                          bb[3].type == kPicture && bb[3].body == fill(20, 5);
        const std::vector<std::string> items2 = { "ENCODER=" + std::string(kOurs.size() + 2, 'x'), std::string("MQAENCODER=") + kOurs,
                                                  "ORIGINALSAMPLERATE=352800" };
        const std::vector<uint8_t> tight = metaOf(synth::flacFile({ { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", items2) } }, frames));
        const Fixed c = fix(tight, 352800);
        const bool two = c.plan.mode == Mode::Rewrite && !c.plan.sameSize && c.plan.newMeta.size() == tight.size() - 2;
        snprintf(note, sizeof note, "省 %zu 字节:PADDING 变长 %d;没有 PADDING 新建一个 %d;只省 2 字节 → 整文件重写 %d", saved, grew, made, two);
        expect("13 注释变短:省下的字节加进 PADDING(总长不变);没有 PADDING 时够一个块头才新建,不够就整文件重写", grew && made && two, note);
    }
    // ---- 14 没有注释块:新建在 PADDING 之前 ----
    {
        const std::vector<uint8_t> meta = metaOf(synth::flacFile(
            { { kStreamInfo, si() }, { kSeekTable, fill(18, 2) }, { kPadding, std::vector<uint8_t>(500, 0) }, { kPicture, fill(40, 7) } }, frames));
        const Fixed f = fix(meta, 96000);
        std::vector<MetaBlock> blocks;
        const std::vector<Comments> vcs = commentsOf(f.plan.newMeta, &blocks);
        const bool shape = blocks.size() == 5 && blocks[1].type == kSeekTable && blocks[2].type == kVorbisComment && blocks[3].type == kPadding &&
                           blocks[4].type == kPicture && blocks[4].body == fill(40, 7) && blocks[1].body == fill(18, 2);
        const std::vector<uint8_t> none = metaOf(synth::flacFile({ { kStreamInfo, si() } }, frames));
        const Fixed g = fix(none, 96000);
        std::vector<MetaBlock> gb;
        commentsOf(g.plan.newMeta, &gb);
        expect("14 一个注释块都没有:新建一个(厂商串是我们的),放在第一个 PADDING 之前;连 PADDING 也没有就放最后、整文件重写",
               f.err == PlanErr::Ok && f.plan.mode == Mode::InPlace && shape && vcs.size() == 1 && vcs[0].vendor == newVendor() &&
                   vcs[0].items.size() == 3 && exactlyOnce(f.plan.newMeta, 96000) && g.plan.mode == Mode::Rewrite && gb.size() == 2 &&
                   gb[1].type == kVorbisComment && exactlyOnce(g.plan.newMeta, 96000));
    }
    // ---- 15 别的块逐字节不动、顺序不变;"最后一块"标志恰好一个 ----
    {
        const std::vector<synth::Block> in = { { kStreamInfo, si() },       { kApplication, fill(24, 1) }, { kSeekTable, fill(54, 2) },
                                               { kVorbisComment, synth::vorbisBody("v", plainTags) }, { kCueSheet, fill(400, 3) },
                                               { kPicture, fill(3000, 4) }, { 42, fill(9, 5) },   // 保留类型的块也原样留着
                                               { kPadding, std::vector<uint8_t>(4, 0) } };
        const std::vector<uint8_t> meta = metaOf(synth::flacFile(in, frames));
        const Fixed f = fix(meta, 352800);                    // PADDING 只有 4 字节:不够,整文件重写,PADDING 原样留着
        std::vector<MetaBlock> out;
        commentsOf(f.plan.newMeta, &out);
        bool same = out.size() == in.size();
        for (size_t i = 0; same && i < in.size(); ++i)
            same = out[i].type == in[i].first && (in[i].first == kVorbisComment || out[i].body == in[i].second);
        bool onFinal = false;
        const int flags = lastFlags(f.plan.newMeta, &onFinal);
        // 末尾的 PADDING 整块被用掉时,标志要挪到新的最后一块上
        const size_t need = (4 + 8 + kOurs.size()) + (4 + 11 + kOurs.size()) + (4 + 19 + 6);
        const std::vector<uint8_t> m2 = metaOf(synth::flacFile({ { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", { "T=1" }) },
                                                                 { kPadding, std::vector<uint8_t>(need - 4, 0) } }, frames));
        const Fixed g = fix(m2, 352800);
        bool onFinal2 = false;
        const int flags2 = lastFlags(g.plan.newMeta, &onFinal2);
        std::vector<MetaBlock> rt;
        const bool roundTrip = splitBlocks(meta.data(), meta.size(), rt) && joinBlocks(rt) == meta;
        snprintf(note, sizeof note, "8 个块逐个相同 %d;标志 %d 个、在最后一块上 %d;用掉末尾 PADDING 后 %d 个、在最后一块上 %d;拆了拼回 %d", same, flags,
                 onFinal, flags2, onFinal2, roundTrip);
        expect("15 别的块(应用块 / 定位表 / CUE / 图片 / 保留类型 / PADDING)逐字节不动、顺序不变;\"最后一块\"标志恰好一个且在最后",
               f.err == PlanErr::Ok && f.plan.mode == Mode::Rewrite && same && flags == 1 && onFinal && flags2 == 1 && onFinal2 && roundTrip, note);
    }
    // ---- 16 改动跨度超过上限:结果一样,只是不原地写 ----
    {
        const std::vector<uint8_t> meta = metaOf(synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", { "T=1" }) }, { kPicture, fill(5000, 6) },
              { kPadding, std::vector<uint8_t>(600, 0) } },
            frames));
        const Fixed big = fix(meta, 352800, 4, kSectorBytes);  // 图片夹在注释与 PADDING 之间:整张图要挪,跨了好几个扇区
        const Fixed roomy = fix(meta, 352800);
        snprintf(note, sizeof note, "改动跨度 %zu 字节", big.plan.diffEnd - big.plan.diffAt);
        expect("16 要挪动的字节跨了扇区:改成整文件重写,结果字节与(不管扇区时)原地改的相同、总长不变",
               big.err == PlanErr::Ok && big.plan.mode == Mode::Rewrite && big.plan.sameSize && roomy.plan.mode == Mode::InPlace &&
                   big.plan.newMeta == roomy.plan.newMeta && big.plan.diffEnd - big.plan.diffAt > 5000 && big.plan.newMeta.size() == meta.size(),
               note);
    }
    // ---- 17 超过块长上限的不写;坏的注释块不碰 ----
    {
        const std::string huge(kMaxBlockBody - 40, 'p');      // 再加三个标签就超过 2^24 − 1
        const std::vector<uint8_t> meta = metaOf(synth::flacFile({ { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", { "P=" + huge }) } },
                                                                 frames));
        const Fixed f = fix(meta, 352800);
        std::vector<uint8_t> bad = synth::vorbisBody("v", { "TITLE=T" });
        bad[0] = 0x7F;                                        // 厂商串长度写坏
        std::vector<synth::Block> bb = { { kStreamInfo, si() }, { kVorbisComment, bad } };
        const std::vector<uint8_t> badMeta = metaOf(synth::flacFile(bb, frames));
        std::string fin[tagrepair::kKeys] = { kOurs, kOurs, "352800" };
        Plan p;
        const PlanErr e = planFix(badMeta, fin, 4, kOneBigSector, p);
        std::string empty[tagrepair::kKeys] = { kOurs, kOurs, "" };
        Plan p2;
        const PlanErr e2 = planFix(metaOf(synth::flacFile({ { kStreamInfo, si() } }, frames)), empty, 4, kOneBigSector, p2);
        expect("17 注释块会超过 16 MB 上限的拒掉;注释块本身坏了的不碰;原始采样率写不出来的不修",
               !meta.empty() && f.err == PlanErr::TooBig && f.plan.newMeta.empty() && e == PlanErr::BadComment && p.newMeta.empty() &&
                   e2 == PlanErr::Internal);
    }
    // ---- 18 码流里没读到原始采样率 ----
    {
        std::vector<Comments> vcs(1);
        vcs[0].items = { "ORIGINALSAMPLERATE=96000" };
        std::string fin[tagrepair::kKeys];
        chooseFinal(vcs, 0, fin);
        const bool keep = fin[tagrepair::kOrigRate] == "96000";
        vcs[0].items = { "ORIGINALSAMPLERATE=n/a" };
        chooseFinal(vcs, 0, fin);
        const bool none = fin[tagrepair::kOrigRate].empty() && fin[tagrepair::kEncoder] == kOurs;
        chooseFinal(vcs, 88200, fin);
        const bool fromStream = fin[tagrepair::kOrigRate] == "88200";
        expect("18 码流里没读到原始采样率:文件里已有的整数留着;没有就写不出(不猜);读到了就用码流里的", keep && none && fromStream);
    }
    // ---- 19 分类 ----
    {
        using namespace tagrepair;
        Facts f;
        const bool err = classify(f) == State::Error;
        f.opened = true;
        const bool lossy = classify(f) == State::NotLossless;
        f.losslessPcm = true;
        f.nativeFlac = true;
        f.bad = 7;                                            // 标签全缺,但没有同步标记:绝不能判成能修
        f.rateAvailable = true;
        const bool noMarker = classify(f) == State::NoMarker;
        f.marker = true;
        const bool rep = classify(f) == State::Repairable;
        f.bad = 0;
        const bool comp = classify(f) == State::Complete;
        f.bad = kBadOrigRate;
        f.rateAvailable = false;
        const bool unk = classify(f) == State::RateUnknown;
        f.bad = kBadEncoder | kBadFormatEncoder;              // 两个编码器键都不合格:能修(采样率在不在都一样)
        const bool repEnc = classify(f) == State::Repairable;
        // 完整不完整只问 tag_rule.h 的 tagsComplete:八种逐键报告,分类与它一致
        bool followsRule = true;
        f.rateAvailable = true;
        for (unsigned bad = 0; bad < 8; ++bad) {
            f.bad = bad;
            followsRule = followsRule && (classify(f) == State::Complete) == tagsComplete(bad) && (classify(f) == State::Repairable) == !tagsComplete(bad);
        }
        f.rateAvailable = false;
        f.bad = kBadEncoder | kBadFormatEncoder;
        f.flacDamaged = true;
        const bool dmg = classify(f) == State::Damaged;
        f.marker = false;
        const bool dmgNoMarker = classify(f) == State::NoMarker;
        f.marker = true;
        f.flacDamaged = false;
        f.nativeFlac = false;
        const bool wav = classify(f) == State::Unsupported;
        f.bad = 0;
        const bool wav2 = classify(f) == State::Unsupported;
        snprintf(note, sizeof note, "打不开 %d;有损 %d;不是 MQA %d;能修 %d;完整 %d;采样率写不出 %d;两个编码器键都不合格能修 %d;按规则分 %d;结构坏了 %d %d;别的容器 %d %d",
                 err, lossy, noMarker, rep, comp, unk, repEnc, followsRule, dmg, dmgNoMarker, wav, wav2);
        expect("19 分类:码流不是 MQA 的文件标签再缺也不算能修;MQA 的 FLAC 按标签分完整 / 能修 / 采样率写不出 / 结构损坏;别的容器单列",
               err && lossy && noMarker && rep && comp && unk && repEnc && followsRule && dmg && dmgNoMarker && wav && wav2, note);
    }
    // ---- 29 标签完整的规则(tag_rule.h 的 tagsComplete):采样率合格,并且两个编码器键至少一个合格 ----
    {
        using namespace tagrepair;
        // 八种逐键报告一种一种写出来(E = ENCODER 不合格,M = MQAENCODER 不合格,R = ORIGINALSAMPLERATE 不合格)
        const bool allGood = tagsComplete(0);                                             // 三个都合格
        const bool onlyEncoder = tagsComplete(kBadFormatEncoder);                         // 只有 ENCODER 合格(早期官方文件:没有 MQAENCODER)
        const bool onlyFormat = tagsComplete(kBadEncoder);                                // 只有 MQAENCODER 合格(旁边的 ENCODER 是 Lavf… 或没有)
        const bool noEncoder = tagsComplete(kBadEncoder | kBadFormatEncoder);             // 两个编码器键都不合格
        const bool noRate = tagsComplete(kBadOrigRate);                                   // 编码器键都合格,采样率不合格
        const bool noRateE = tagsComplete(kBadOrigRate | kBadFormatEncoder), noRateM = tagsComplete(kBadOrigRate | kBadEncoder);
        const bool nothing = tagsComplete(kBadEncoder | kBadFormatEncoder | kBadOrigRate);
        // 从文件里看到的样子走一遍:官方文件只有 ENCODER 与 ORIGINALSAMPLERATE
        Seen s;
        s.count[kEncoder] = 1;
        s.value[kEncoder] = kOfficial;
        s.count[kOrigRate] = 1;
        s.value[kOrigRate] = "352800";
        const unsigned official = problems(s, 352800);
        const bool officialOk = official == kBadFormatEncoder && tagsComplete(official);
        s.value[kOrigRate] = "96000";                                                     // 采样率与码流不符:不完整
        const bool wrongRate = !tagsComplete(problems(s, 352800));
        s.value[kOrigRate] = "352800";
        s.count[kEncoder] = 2;                                                            // 唯一合格的那个键有两份:不算合格
        const bool dupOnly = !tagsComplete(problems(s, 352800));
        s.count[kFormatEncoder] = 1;
        s.value[kFormatEncoder] = kOurs;                                                  // 另一个键合格:完整(重复的那个原样留着)
        const bool dupOther = tagsComplete(problems(s, 352800));
        s.count[kEncoder] = 1;
        s.value[kEncoder] = "Lavf58.76.100";                                              // ENCODER=Lavf… 在合格的 MQAENCODER 旁边:完整
        const bool lavfBeside = problems(s, 352800) == kBadEncoder && tagsComplete(problems(s, 352800));
        s.value[kFormatEncoder] = "Lavf";                                                 // 两个都是别的值:不完整
        const bool bothOther = !tagsComplete(problems(s, 352800));
        snprintf(note, sizeof note, "全合格 %d;只 ENCODER 合格 %d;只 MQAENCODER 合格 %d(应都为 1);两个编码器键都不合格 %d;采样率不合格 %d %d %d;全不合格 %d(应都为 0);"
                                    "官方两键文件 %d、采样率不符 %d、唯一合格的键重复 %d、另一个键合格 %d、Lavf 在旁边 %d、两个都是别的值 %d(应都为 1)",
                 allGood, onlyEncoder, onlyFormat, noEncoder, noRate, noRateE, noRateM, nothing, officialOk, wrongRate, dupOnly, dupOther, lavfBeside, bothOther);
        expect("29 标签完整 = ORIGINALSAMPLERATE 合格,并且 ENCODER / MQAENCODER 至少一个合格(另一个不在或是别的值不碍事);采样率不合格、或两个编码器键都不合格才算不完整",
               allGood && onlyEncoder && onlyFormat && !noEncoder && !noRate && !noRateE && !noRateM && !nothing && officialOk && wrongRate && dupOnly &&
                   dupOther && lavfBeside && bothOther,
               note);
    }
    // ---- 21 只拿得到 FFmpeg 元数据字典的程序用的那一组(同名条目被接成 "a;b")----
    {
        using namespace tagrepair;
        bool has[kKeys] = { true, true, true };
        std::string val[kKeys] = { kOfficial, kOurs, "352800" };
        const unsigned full = problemsFromDict(has, val, 352800);
        val[kEncoder] = std::string("Lavf58.76.100;") + kOurs;         // ID3v2 的 TSSE 与 Vorbis 的 ENCODER 接在一起
        val[kOrigRate] = "44100;352800";
        const unsigned joined = problemsFromDict(has, val, 352800);
        val[kEncoder] = "Lavf58.76.100;LAME";
        val[kOrigRate] = "44100;96000";
        const unsigned bad = problemsFromDict(has, val, 352800);
        has[kFormatEncoder] = false;
        const unsigned missing = problemsFromDict(has, val, 352800);
        const bool has0[kKeys] = { false, false, false };
        const std::string none[kKeys];
        const unsigned all = problemsFromDict(has0, none, 352800);
        snprintf(note, sizeof note, "齐全 %u(应 0),接起来的值里有一段合格 %u(应 0),没有一段合格 %u(应 5),再缺 MQAENCODER %u(应 7),全缺 %u(应 7)", full, joined,
                 bad, missing, all);
        expect("21 从容器元数据判:同名条目接成 \"a;b\" 时有一段合格就算合格;缺的、没有一段合格的各自报在自己的位上",
               full == 0 && joined == 0 && bad == (kBadEncoder | kBadOrigRate) && missing == 7 && all == 7 && joinedValueOk(kEncoder, kOurs, 0) &&
                   !joinedValueOk(kEncoder, "", 0) && !joinedValueOk(kOrigRate, ";", 0),
               note);
    }
    // ---- 23 原地改写的扇区条件:改动的字节在文件里全落在同一个扇区内才原地改 ----
    {
        const bool unit = withinOneSector(0, 512, 512) && !withinOneSector(0, 513, 512) && withinOneSector(511, 512, 512) &&
                          !withinOneSector(511, 513, 512) && withinOneSector(512, 1024, 512) && !withinOneSector(100, 100, 512) &&
                          !withinOneSector(0, 1, 0) && withinOneSector(4096, 4608, 4096) && !withinOneSector(4095, 4097, 4096);
        const std::vector<uint8_t> meta = metaOf(synth::flacFile(
            { { kStreamInfo, si() }, { kVorbisComment, synth::vorbisBody("v", { "T=1" }) }, { kPadding, std::vector<uint8_t>(600, 0) } }, frames));
        const Fixed loose = fix(meta, 352800);                // 不管扇区时:改动的范围 [a, b)
        const size_t a = loose.plan.diffAt, b = loose.plan.diffEnd;
        // 同一段元数据放在文件里不同的位置(前面的 ID3v2 长短不同就是这样):改动 [metaAt + a, metaAt + b) 的落点跟着变
        const uint64_t atStart = 512 * 3 - a;                 // 改动的第一个字节正好是扇区的第一个字节
        const uint64_t atEnd = 512 * 4 - b;                   // 改动的最后一个字节正好是扇区的最后一个字节
        const Fixed f1 = fix(meta, 352800, atStart, kSectorBytes), f2 = fix(meta, 352800, atEnd, kSectorBytes),
                    f3 = fix(meta, 352800, atEnd + 1, kSectorBytes), f4 = fix(meta, 352800, atStart - 1, kSectorBytes), f5 = fix(meta, 352800, 4, 0);
        snprintf(note, sizeof note, "改动 %zu 字节;贴着扇区头 %d、贴着扇区尾 %d(应都原地改);往后一个字节 %d、往前一个字节 %d、扇区条件关掉 %d(应都重写)", b - a,
                 f1.plan.mode == Mode::InPlace, f2.plan.mode == Mode::InPlace, f3.plan.mode == Mode::Rewrite, f4.plan.mode == Mode::Rewrite,
                 f5.plan.mode == Mode::Rewrite);
        expect("23 原地改写的条件:改动的字节在文件里全落在同一个 512 字节的扇区内;差一个字节跨了边界就整文件重写(要写的内容不变)",
               unit && loose.plan.mode == Mode::InPlace && b > a && b - a < 512 && f1.plan.mode == Mode::InPlace && f2.plan.mode == Mode::InPlace &&
                   f3.plan.mode == Mode::Rewrite && f4.plan.mode == Mode::Rewrite && f5.plan.mode == Mode::Rewrite && f3.plan.sameSize &&
                   f1.plan.newMeta == loose.plan.newMeta && f3.plan.newMeta == loose.plan.newMeta && f5.plan.newMeta == loose.plan.newMeta,
               note);
    }
    // ---- 24 要写的值、认可的前缀:各只定在一处;命令行给的值的把关 ----
    {
        using namespace tagrepair;
        const Rule def;
        const bool consts = def.encoderTag == kDefaultEncoderTag && def.acceptPrefix == kDefaultAcceptPrefix && !encoderTagProblem(kDefaultEncoderTag) &&
                            !acceptPrefixProblem(kDefaultAcceptPrefix) && encoderValueOk(kDefaultEncoderTag) &&
                            kOurs.find(kWriterMark) != std::string::npos && kOurs.find(kUnsignedMark) != std::string::npos;
        // 换一个要写的值:写的是它;它自己算合格(修完再看是完整的);认可的前缀照旧
        Rule custom;
        custom.encoderTag = "Bench tag repair (unsigned)";
        std::vector<Comments> vcs(1);
        vcs[0].items = { "ENCODER=Lavf", "MQAENCODER=" + kOfficial };
        std::string fin[kKeys];
        chooseFinal(vcs, 352800, fin, custom);
        const bool wrote = fin[kEncoder] == custom.encoderTag && fin[kFormatEncoder] == kOfficial && fin[kOrigRate] == "352800";
        const bool own = encoderValueOk(custom.encoderTag, custom) && encoderValueOk(kOfficial, custom) && !encoderValueOk("Lavf", custom) &&
                         encoderValueOk(custom.encoderTag) == (custom.encoderTag.compare(0, kPrefix.size(), kPrefix) == 0);
        // 前缀给空:只要不是空值就合格
        Rule any;
        any.acceptPrefix.clear();
        chooseFinal(vcs, 352800, fin, any);
        Seen s;
        s.count[0] = s.count[1] = s.count[2] = 1;
        s.value[0] = "Lavf";
        s.value[1] = custom.encoderTag;
        s.value[2] = "352800";
        const bool anyOk = encoderValueOk("Lavf", any) && !encoderValueOk("", any) && fin[kEncoder] == "Lavf" && fin[kFormatEncoder] == kOfficial &&
                           problems(s, 352800, any) == 0 && problems(s, 352800, custom) == kBadEncoder && (problems(s, 352800) & kBadEncoder) != 0;
        // 把关:必须带着"谁写的"与"没有签名"两段,所以官方编码器的名字原样拿来是不行的
        const std::string marks = std::string(kWriterMark) + " " + kUnsignedMark;
        const bool good = !encoderTagProblem("Any text = fine, " + marks) && !encoderTagProblem("标签 " + marks) &&
                          !encoderTagProblem(std::string(kMaxEncoderTagBytes - marks.size(), 'a') + marks);
        const bool bad = encoderTagProblem("") && encoderTagProblem(std::string(kMaxEncoderTagBytes - marks.size() + 1, 'a') + marks) &&
                         encoderTagProblem(marks + "\n") && encoderTagProblem("a\t" + marks) && encoderTagProblem("a\x7F" + marks) &&
                         encoderTagProblem(" " + marks) && encoderTagProblem(marks + " ") && encoderTagProblem("a;b " + marks) &&
                         encoderTagProblem(kOfficial) && encoderTagProblem(kPrefix + " v1.1") && encoderTagProblem(std::string("x ") + kWriterMark) &&
                         encoderTagProblem(std::string("x ") + kUnsignedMark) && encoderTagProblem("\xC3 " + marks) &&
                         encoderTagProblem("\xED\xA0\x80" + marks) && encoderTagProblem("\xC0\xAF" + marks) && encoderTagProblem(marks + "\xE4\xB8");
        const bool prefix = !acceptPrefixProblem("") && !acceptPrefixProblem("MQA") && !acceptPrefixProblem("编码") && acceptPrefixProblem("a\nb") &&
                            acceptPrefixProblem("\xFF") && acceptPrefixProblem(std::string(kMaxEncoderTagBytes + 1, 'a'));
        snprintf(note, sizeof note, "常量自洽 %d;换值 %d %d;空前缀 %d;放行 %d;拦住 %d;前缀 %d", consts, wrote, own, anyOk, good, bad, prefix);
        expect("24 写的值与认可的前缀各定在一处(tag_rule.h);换值后写的是给的值、它自己算合格;空前缀 = 非空即合格;"
               "不带 \"tag repair\" / \"(unsigned)\"、带控制字符或分号、头尾空格、不是 UTF-8、太长的值都拦住",
               consts && wrote && own && anyOk && good && bad && prefix, note);
    }
    // ---- 20 合成文件用的校验与控制位(自检靠它造文件,先保证它自己对)----
    {
        const uint8_t msg[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        const uint8_t c8 = synth::crc8(msg, 9);
        const uint16_t c16 = synth::crc16(msg, 9);
        const std::vector<uint8_t> ds = synth::startRecordBits(352800, 44100);
        uint64_t magic = 0;
        for (int i = 0; i < 40 && i < (int)ds.size(); ++i) magic |= (uint64_t)ds[(size_t)i] << i;
        unsigned orig = 0;
        for (int i = 0; i < 5 && 42 + i < (int)ds.size(); ++i) orig |= (unsigned)ds[(size_t)(42 + i)] << i;
        std::vector<int32_t> lr = synth::noise(200, 24, 7);
        synth::embedControl(lr, 24, 50, ds);
        bool embedded = true;
        for (size_t i = 0; i < ds.size(); ++i) embedded = embedded && (((lr[(50 + i) * 2] ^ lr[(50 + i) * 2 + 1]) >> 8) & 1) == ds[i];
        snprintf(note, sizeof note, "CRC-8 0x%02X(应 0xF4),CRC-16 0x%04X(应 0xFEE8),控制位 %zu 个,同步标记 0x%llX,原始采样率码 %u(352.8 k 应为 3)", c8, c16,
                 ds.size(), (unsigned long long)magic, orig);
        expect("20 合成文件:FLAC 的 CRC-8 / CRC-16 对标准校验值;数据同步记录 84 位、同步标记与采样率码在位;控制位写进 L^R 的第 8 位",
               c8 == 0xF4 && c16 == 0xFEE8 && ds.size() == 84 && magic == 0x11319207d5ull && orig == 3 && synth::rateCode(192000) == 10 &&
                   synth::rateCode(44100) == 0 && synth::rateCode(48000) == 8 && synth::rateCode(12345) == -1 && embedded,
               note);
    }
    // ---- 25 找 FFmpeg 运行库的顺序:--ffmpeg-dir;本程序旁边;往上两级;环境变量 ----
    {
        typedef std::vector<std::wstring> Dirs;
        const Dirs normal = probe::ffmpegSearchDirs(L"", L"D:\\Player\\plugins\\tagrepair", L"E:\\ffmpeg\\bin");
        const Dirs forced = probe::ffmpegSearchDirs(L"F:\\x\\", L"D:\\Player\\plugins\\tagrepair", L"E:\\ffmpeg\\bin");
        const Dirs shallow = probe::ffmpegSearchDirs(L"", L"D:\\tool", L"");                  // 往上两级到了盘符之上:没有这一处
        const Dirs root = probe::ffmpegSearchDirs(L"", L"D:\\a\\b", L"d:\\A\\B\\");            // 环境变量与本程序旁边是同一处:不重复列
        const bool ok = normal == Dirs{ L"D:\\Player\\plugins\\tagrepair", L"D:\\Player", L"E:\\ffmpeg\\bin" } && forced == Dirs{ L"F:\\x" } &&
                        shallow == Dirs{ L"D:\\tool" } && root == Dirs{ L"D:\\a\\b", L"D:\\" };
        expect("25 找 FFmpeg 运行库的顺序:明说了文件夹就只看那一处;否则先本程序旁边、再往上两级、最后环境变量;空的、重复的不列", ok);
    }
    // ---- 26 写 JSON:转义、嵌套对象的逗号、数组 ----
    {
        jsonout::Writer w;
        w.begin().key("a").val("x\"y\\z\n\t\x01").key("n").val(-7).key("big").val(5000000000ll).key("t").val(true);
        w.beginObj("o").key("k").val(std::string("\xE4\xB8\xAD")).beginObj("p").endObj().key("f").val(false).endObj();
        w.key("arr").arrOfStr({ "1", "b\"" }).beginObj("e").endObj().key("z").val("").end();
        const std::string want = "{\"a\":\"x\\\"y\\\\z\\n\\t\\u0001\",\"n\":-7,\"big\":5000000000,\"t\":true,"
                                 "\"o\":{\"k\":\"\xE4\xB8\xAD\",\"p\":{},\"f\":false},\"arr\":[\"1\",\"b\\\"\"],\"e\":{},\"z\":\"\"}";
        expect("26 写 JSON:引号 / 反斜杠 / 控制字符转义,UTF-8 原样,嵌套对象前后的逗号、空对象、字符串数组", w.text() == want, w.text().c_str());
    }
    // ---- 27 拿掉三个标签(造测试文件用的):别的注释、别的块、音频字节都不变 ----
    {
        const std::vector<std::string> items = { "TITLE=T", std::string("encoder=") + kOfficial, "COMMENT=keep", std::string("MQAENCODER=") + kOurs,
                                                 "ORIGINALSAMPLERATE=352800", "originalsamplerate=1" };
        const std::vector<uint8_t> file = synth::flacFile(
            { { kStreamInfo, si() }, { kPicture, fill(30, 2) }, { kVorbisComment, synth::vorbisBody("v", items) }, { kPadding, std::vector<uint8_t>(40, 0) } },
            frames);
        std::vector<uint8_t> out, bad;
        const bool did = testfiles::stripTags(file, out);
        const testfiles::Parsed a = testfiles::parse(file), b = testfiles::parse(out);
        const bool shape = did && b.ok && b.vcs.size() == 1 && b.vcs[0].items == std::vector<std::string>{ "TITLE=T", "COMMENT=keep" } &&
                           b.vcs[0].vendor == "v" && b.audio == a.audio && b.blocks.size() == 4 && b.blocks[1].body == fill(30, 2) &&
                           b.seen.count[0] == 0 && b.seen.count[1] == 0 && b.seen.count[2] == 0 && memcmp(a.lay.streamInfo, b.lay.streamInfo, 34) == 0;
        const std::vector<uint8_t> junk = { 'n', 'o' };
        expect("27 拿掉三个标签:三个键(不分大小写、重复的)全拿掉,别的注释与块、音频字节、STREAMINFO 不变;不是 FLAC 的不出结果",
               shape && !testfiles::stripTags(junk, bad));
    }
    // ---- 28 对外报的名字与协议号 ----
    expect("28 对外的标识是 tagrepair、协议号 1(宿主按这两项认;改了就是不兼容的改动);默认写的标签值与新建注释块的厂商串里没有别的程序的名字",
           std::string(tagrepair::kHostId) == "tagrepair" && tagrepair::kProtocol == 1 && std::string(tagrepair::kToolName) == "tagrepair" &&
               kOurs == "MQAEncode v1.1, tag repair (unsigned)" && std::string(newVendor()) == "tagrepair");
}

// ---------------------------------------------------------------------------
// samples:一套合成文件(与 selftest 里造的是同一些情况,只是留在给定的文件夹里不删)
// ---------------------------------------------------------------------------
static int cmdSamples(const std::wstring& dirIn) {
    using namespace testfiles;
    const std::wstring dir = wu::longPath(dirIn);
    CreateDirectoryW(dir.c_str(), nullptr);
    WIN32_FIND_DATAW fd{};
    const HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    bool empty = find != INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) empty = false;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (!empty) { say("这个文件夹建不了或者不是空的:不往已有东西的文件夹里写"); return 2; }
    int made = 0, failed = 0;
    auto put = [&](const std::wstring& name, const Bytes& d) {
        if (writeFile(dir + L"\\" + name, d)) ++made; else ++failed;
    };
    const std::string ours = tagrepair::kDefaultEncoderTag;
    const std::string official = std::string(tagrepair::kDefaultAcceptPrefix) + " v1.1, 0.0.0+000 (0000000), 00000000-0000-0000-0000-000000000000, Jan 01 2000 00:00:00";
    const std::vector<int32_t> p441 = pcm(44100, 24, 3.0, 5000, 352800, 11), plain = pcm(44100, 24, 3.0, -1, 0, 11);
    const std::vector<std::string> lost = { "TITLE=合成曲线", "ARTIST=Nobody", "encoder=Lavf58.76.100", "DATE=2026" };
    const Bytes seek = [] { Bytes s(18, 0); for (int i = 0; i < 8; ++i) s[(size_t)i] = 0xFF; return s; }();
    const auto seekOf = [](int points) {
        Bytes s((size_t)points * 18, 0);
        for (int p = 0; p < points; ++p)
            for (int i = 0; i < 8; ++i) s[(size_t)p * 18 + (size_t)i] = 0xFF;
        return s;
    };
    const std::vector<synth::Block> titleAndPad = { { kVorbisComment, vc("v", { "TITLE=T" }) }, { kPadding, zeros(600) } };

    // 能修的:原地改写 / 整文件重写 / 没有注释块 / 两个注释块 / 前后有 ID3 / 48 k / 16 bit / 只有采样率不对 / 重复的键 / 值是空的
    put(L"lost_tags_padding.flac", flac(p441, 44100, 24, { { kSeekTable, seek }, { kVorbisComment, vc("reference libFLAC 1.3.2 20170101", lost) },
                                                           { kPicture, picture() }, { kPadding, zeros(4096) } }));
    put(L"丢了标签 ♪ é.flac", flac(p441, 44100, 24, titleAndPad));
    put(L"no_padding.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "TITLE=T" }) } }));
    put(L"no_comment_block.flac", flac(p441, 44100, 24, { { kApplication, Bytes{ 't', 'e', 's', 't', 1, 2, 3, 4 } }, { kPicture, picture() },
                                                          { kPadding, zeros(1000) } }));
    put(L"two_comment_blocks.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("first", { "TITLE=T" }) },
                                                            { kVorbisComment, vc("second", { "ENCODER=Lavf", "COMMENT=keep", "ORIGINALSAMPLERATE=44100" }) },
                                                            { kPadding, zeros(600) } }));
    {
        Bytes tail(128, ' ');
        tail[0] = 'T'; tail[1] = 'A'; tail[2] = 'G';
        put(L"id3_front_and_back.flac", flac(p441, 44100, 24, titleAndPad, synth::id3v2(300), tail));
    }
    put(L"carrier_48k.flac", flac(pcm(48000, 24, 2.0, 100, 192000, 21), 48000, 24, { { kVorbisComment, vc("v", {}) }, { kPadding, zeros(600) } }));
    put(L"carrier_16bit.flac", flac(pcm(44100, 16, 2.0, 2000, 88200, 31), 44100, 16, { { kVorbisComment, vc("v", {}) }, { kPadding, zeros(600) } }));
    put(L"wrong_rate.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "TITLE=T", "ENCODER=" + official, "MQAENCODER=" + official,
                                                                                "ORIGINALSAMPLERATE=96000" }) },
                                                    { kPadding, zeros(100) } }));
    put(L"duplicate_keys.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "ENCODER=" + official, "TITLE=T", "encoder=Lavf", "ORIGINALSAMPLERATE=44100",
                                                                                    "originalsamplerate=352800", "MQAENCODER=" + official,
                                                                                    "MqaEncoder=" + official }) },
                                                        { kPadding, zeros(64) } }));
    put(L"empty_value.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "ENCODER=Lavf58.76.100", "MQAENCODER=", "TITLE=T" }) }, { kPadding, zeros(600) } }));
    put(L"sector_crossing.flac", flac(p441, 44100, 24, { { kSeekTable, seekOf(25) }, { kVorbisComment, vc("v", { "TITLE=T" }) }, { kPadding, zeros(2000) } }));
    put(L"sector_inside.flac", flac(p441, 44100, 24, { { kSeekTable, seekOf(30) }, { kVorbisComment, vc("v", { "TITLE=T" }) }, { kPadding, zeros(2000) } }));
    put(L"sync_at_9.9s.flac", flac(pcm(44100, 24, 11.0, 436000, 352800, 41), 44100, 24, { { kVorbisComment, vc("v", {}) }, { kPadding, zeros(600) } }));
    // 已经完整的
    put(L"complete_official.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "TITLE=T", "ENCODER=" + official, "MQAENCODER=" + official,
                                                                                       "ORIGINALSAMPLERATE=352800" }) },
                                                           { kPadding, zeros(100) } }));
    put(L"complete_ours.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "encoder=" + ours, "MQAENCODER=" + ours, "ORIGINALSAMPLERATE=352800" }) } }));
    // 也是完整的:只有一个编码器键合格(早期官方文件没有 MQAENCODER;或者 ENCODER 被换成了 Lavf…),采样率合格
    put(L"two_tags_only.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "TITLE=T", "ENCODER=" + official, "ORIGINALSAMPLERATE=352800" }) },
                                                       { kPadding, zeros(100) } }));
    put(L"lavf_beside_good.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "encoder=Lavf58.76.100", "MQAENCODER=" + official,
                                                                                      "ORIGINALSAMPLERATE=352800" }) },
                                                          { kPadding, zeros(100) } }));
    // 拒绝 / 不碰的:只读、超过块长上限、截断与损坏
    put(L"read_only.flac", flac(p441, 44100, 24, titleAndPad));
    SetFileAttributesW((dir + L"\\read_only.flac").c_str(), FILE_ATTRIBUTE_READONLY);
    put(L"comment_16mb.flac", flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "P=" + std::string(kMaxBlockBody - 40, 'p') }) } }));
    {
        const Bytes good = flac(p441, 44100, 24, { { kVorbisComment, vc("v", { "TITLE=T", "COMMENT=" + std::string(200, 'c') }) }, { kPadding, zeros(600) } });
        const Parsed g = parse(good);
        put(L"cut_in_metadata.flac", Bytes(good.begin(), good.begin() + (std::ptrdiff_t)(g.lay.metaAt + 38 + 60)));
        put(L"cut_in_audio.flac", Bytes(good.begin(), good.end() - 9000));
        Bytes flipped(good);
        flipped[(size_t)g.lay.audioAt + 30000] ^= 0x40;
        put(L"bit_flip_in_audio.flac", flipped);
        Bytes badLen(good);
        badLen[(size_t)g.lay.metaAt + 38 + 3] ^= 0x08;
        put(L"bad_block_length.flac", badLen);
        Bytes junk(good.begin(), good.begin() + (std::ptrdiff_t)g.lay.audioAt);
        junk.insert(junk.end(), { 0x00, 0x11, 0x22 });
        junk.insert(junk.end(), good.begin() + (std::ptrdiff_t)g.lay.audioAt, good.end());
        put(L"junk_before_audio.flac", junk);
    }
    // 没有同步标记的、别的容器、有损、不是音频
    put(L"plain.flac", flac(plain, 44100, 24, titleAndPad));
    put(L"sync_at_10.2s.flac", flac(pcm(44100, 24, 11.0, 450000, 352800, 41), 44100, 24, { { kVorbisComment, vc("v", {}) }, { kPadding, zeros(600) } }));
    put(L"carrier.wav", wav(p441, 44100, 24));
    // 16 bit 的:光盘抓轨那一类。FLAC 能原地修,也能另存一个 24 bit 的拷贝;WAV 两种另存都行
    put(L"disc_16bit_no_tags.flac", flac(pcm(44100, 16, 3.0, 2000, 88200, 32), 44100, 16, { { kVorbisComment, vc("v", { "TITLE=Disc Track" }) },
                                                                                            { kPadding, zeros(600) } }));
    put(L"disc_16bit.wav", wav(pcm(44100, 16, 3.0, 2000, 88200, 33), 44100, 16));
    put(L"plain.wav", wav(plain, 44100, 24));
    {
        std::vector<int32_t> mu = synth::noise(44100, 16, 5);
        for (int32_t& v : mu) v &= 0xFF;
        put(L"mulaw.wav", wav(mu, 44100, 8, 7));
    }
    put(L"not_audio.flac", Bytes{ 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' });
    // 子文件夹里的(看 --recursive 与拖文件夹)
    CreateDirectoryW((dir + L"\\子文件夹").c_str(), nullptr);
    put(L"子文件夹\\inner_lost.flac", flac(pcm(44100, 24, 2.0, 300, 176400, 51), 44100, 24, titleAndPad));
    put(L"子文件夹\\inner_plain.flac", flac(pcm(44100, 24, 2.0, -1, 0, 52), 44100, 24, titleAndPad));

    char b[160];
    snprintf(b, sizeof b, "写了 %d 个文件%s", made, failed ? ",有写不了的" : "");
    say(b);
    return failed ? 1 : 0;
}

static int cmdStrip(const std::wstring& in, const std::wstring& out) {
    const std::wstring a = wu::longPath(in), b = wu::longPath(out);
    if (_wcsicmp(a.c_str(), b.c_str()) == 0) { say("入、出是同一个文件:不改已有的文件"); return 2; }
    if (GetFileAttributesW(b.c_str()) != INVALID_FILE_ATTRIBUTES) { say("要写的那个文件已经存在:不覆盖"); return 2; }
    const testfiles::Bytes file = testfiles::readFile(in);
    testfiles::Bytes stripped;
    if (!testfiles::stripTags(file, stripped)) { say("解析不了输入文件(不是原生 FLAC,或者结构有问题)"); return 1; }
    if (!testfiles::writeFile(out, stripped)) { say("写不了输出文件"); return 1; }
    say("已写(三个 MQA 标签已拿掉,音频字节相同):" + wu::utf8(wu::shownPath(b)));
    return 0;
}

// =============================================================================
// 同步标记检测(src/sync_detect.*)的核对。记录是 synth_flac.cpp 拼的 —— 拼的与读的是分开写的两份代码
// =============================================================================
namespace {

// 采样率码 0..31 各是多少 Hz(照着 sync_detect.h 第五条逐个写出来的,不用公式)
const int kRateOfCode[32] = { 44100, 88200, 176400, 352800, 705600, 1411200, 2822400, 5644800,
                              48000, 96000, 192000, 384000, 768000, 1536000, 3072000, 6144000,
                              64000, 128000, 256000, 512000, 1024000, 2048000, 4096000, 8192000,
                              0, 0, 0, 0, 0, 0, 0, 0 };

// 右对齐 bits 位的样本 → 检测要的左对齐 int32
std::vector<int32_t> leftJustified(const std::vector<int32_t>& lr, int bits) {
    std::vector<int32_t> o(lr.size());
    for (size_t i = 0; i < lr.size(); ++i) o[i] = (int32_t)((uint32_t)lr[i] << (32 - bits));
    return o;
}

struct Seen2 {
    bool marker = false;
    int rate = 0, bit = -1;
    bool operator==(const Seen2& o) const { return marker == o.marker && rate == o.rate && bit == o.bit; }
};

// 一段 24 bit 的噪声,从第 at 帧起在第 sampleBit 位放 ctrl;chunk 帧一批地喂
Seen2 detectIn(const std::vector<uint8_t>& ctrl, size_t at = 300, int sampleBit = 8, size_t frames = 3000, size_t chunk = 4096, int bits = 24,
               uint32_t seed = 5) {
    std::vector<int32_t> lr = synth::noise(frames, bits, seed);
    synth::embedControl(lr, bits, at, ctrl, sampleBit);
    const std::vector<int32_t> lj = leftJustified(lr, bits);
    syncdetect::Detector d;
    for (size_t i = 0; i < frames; i += chunk) d.feed(lj.data() + 2 * i, frames - i < chunk ? frames - i : chunk);
    Seen2 s;
    s.marker = d.markerFound();
    s.rate = d.originalRate();
    s.bit = d.controlBit();
    return s;
}

unsigned bitsValue(const std::vector<uint8_t>& b, size_t at, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; ++i) v |= (unsigned)b[at + (size_t)i] << i;
    return v;
}

} // namespace

static void runDetectorChecks() {
    char note[320];
    // ---- D1 采样率的码 ----
    {
        int wrong = 0;
        for (unsigned c = 0; c < 32; ++c)
            if (syncdetect::rateFromCode(c) != kRateOfCode[c]) ++wrong;
        snprintf(note, sizeof note, "32 个码里不对的 %d 个;码 3 = %d,码 10 = %d,码 16 = %d", wrong, syncdetect::rateFromCode(3), syncdetect::rateFromCode(10),
                 syncdetect::rateFromCode(16));
        expect("D1 采样率的码:32 个码逐个对(44100 / 48000 / 64000 各乘 1..128;24..31 无效 = 0)", wrong == 0, note);
    }
    // ---- D2 八个候选位 × 24 个有效的码:找到、位号对、采样率对(载体采样率的码故意与原始的不同)----
    {
        int cases = 0, bad = 0;
        for (int sb = syncdetect::kLowestBit; sb <= syncdetect::kHighestBit; ++sb)
            for (unsigned code = 0; code < 24; ++code) {
                synth::Record r;
                r.originalCode = code;
                r.carrierCode = (code + 5) % 24;
                r.otherFields = 0x155555u ^ (code * 0x9E37u & 0x1FFFFFu);
                const Seen2 s = detectIn(synth::recordBits(r), 300 + code, sb);
                ++cases;
                if (!(s.marker && s.bit == sb && s.rate == kRateOfCode[code])) ++bad;
            }
        // 16 bit 的样本:第 8 位就是它的最低位
        const Seen2 s16 = detectIn(synth::startRecordBits(88200, 44100), 300, 8, 3000, 4096, 16);
        // 第 7 位、第 16 位不在范围里:放了也不认
        const Seen2 lo = detectIn(synth::startRecordBits(88200, 44100), 300, 7), hi = detectIn(synth::startRecordBits(88200, 44100), 300, 16);
        snprintf(note, sizeof note, "%d 种里不对的 %d 种;16 bit:%d / %d / 位 %d;第 7 位 %d,第 16 位 %d", cases, bad, s16.marker, s16.rate, s16.bit, lo.marker,
                 hi.marker);
        expect("D2 标记在第 8..15 位的每一位、24 个有效的码:都找到,报的位号与采样率都对;16 bit 样本认最低位;第 7、16 位不认",
               bad == 0 && s16.marker && s16.rate == 88200 && s16.bit == 8 && !lo.marker && !hi.marker, note);
    }
    // ---- D3 同步标记 40 位里坏任何一位:不认 ----
    {
        const std::vector<uint8_t> good = synth::startRecordBits(176400, 48000);
        int found = 0;
        for (size_t flip = 0; flip < 40; ++flip) {
            std::vector<uint8_t> b = good;
            b[flip] ^= 1;
            if (detectIn(b).marker) ++found;
        }
        const Seen2 ok = detectIn(good);
        snprintf(note, sizeof note, "坏一位的 40 种里被当成标记的 %d 种;完好的:%d / %d", found, ok.marker, ok.rate);
        expect("D3 同步标记坏一位(40 种):一种都不认;完好的认", found == 0 && ok.marker && ok.rate == 176400, note);
    }
    // ---- D4 校验:标记之后的位坏一位。找到标记照报;采样率只有校验对得上才报(4 位校验本身拦不住的那几种,读到的是坏了之后的码)----
    {
        const std::vector<uint8_t> good = synth::startRecordBits(176400, 48000);
        int rejected = 0, wrong = 0, noMarker = 0;
        for (size_t flip = 40; flip < good.size(); ++flip) {
            std::vector<uint8_t> b = good;
            b[flip] ^= 1;
            const Seen2 s = detectIn(b);
            if (!s.marker) ++noMarker;
            if (flip == 40 || (flip >= 73 && flip < 80)) continue;      // 这 8 位一变记录的长度就变了(后面接的是噪声),采样率报不报不在这一条里看
            const bool passes = synth::checkOf(b, 80, 0) == bitsValue(b, 80, 4);
            const int want = passes ? kRateOfCode[bitsValue(b, 42, 5)] : 0;
            if (s.rate != want) ++wrong;
            if (!passes) ++rejected;
        }
        snprintf(note, sizeof note, "不改变记录长度的 36 种里:校验应当对不上的 %d 种,结果与应有的不同 %d 种;44 种里连标记都没报的 %d 种", rejected, wrong, noMarker);
        expect("D4 记录里标记之后坏一位(44 种):标记照报;校验对不上的不报采样率", wrong == 0 && noMarker == 0 && rejected >= 24, note);
    }
    // ---- D5 带位置、带附加项的记录(流中间的记录):校验的初值是位置的低 4 位 ----
    {
        int cases = 0, bad = 0, seedCaught = 0, seedCases = 0;
        uint32_t s = 77;
        auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return s >> 8; };
        for (int i = 0; i < 64; ++i) {
            synth::Record r;
            r.originalCode = rnd() % 24;
            r.carrierCode = rnd() % 24;
            r.otherFields = rnd() & 0x1FFFFFu;
            r.hasPosition = true;
            r.position = (rnd() << 4) | (uint32_t)(i % 16);
            const int items = i % 6;
            for (int k = 0; k < items; ++k) r.items.push_back({ rnd() % 256, rnd() % 256 });
            r.fillSeed = rnd() + 1;
            std::vector<uint8_t> b = synth::recordBits(r);
            const Seen2 a = detectIn(b, 200, 8 + i % 8, 6000);
            ++cases;
            if (!(a.marker && a.rate == kRateOfCode[r.originalCode])) ++bad;
            // 位置的最低位改掉(校验没跟着改):初值不同了,校验对不上
            const size_t posAt = 80 + 16 * (size_t)items;
            b[posAt] ^= 1;
            const bool passes = synth::checkOf(b, b.size() - 4, bitsValue(b, posAt, 4)) == bitsValue(b, b.size() - 4, 4);
            const Seen2 c = detectIn(b, 200, 8 + i % 8, 6000);
            ++seedCases;
            if (c.marker && c.rate == (passes ? kRateOfCode[r.originalCode] : 0)) ++seedCaught;
        }
        snprintf(note, sizeof note, "带位置的 %d 条里读错的 %d 条;位置改一位的 %d 条里结果对的 %d 条", cases, bad, seedCases, seedCaught);
        expect("D5 带位置 / 带 0..5 个附加项的记录:采样率读得对;位置的低位变了(校验没跟着改)就不报", bad == 0 && seedCaught == seedCases, note);
    }
    // ---- D6 分批喂与一次喂完结果相同 ----
    {
        const std::vector<uint8_t> rec = synth::startRecordBits(352800, 44100);
        const Seen2 whole = detectIn(rec, 1234, 11, 5000, 1u << 30);
        bool same = whole.marker && whole.rate == 352800 && whole.bit == 11;
        for (size_t chunk : { (size_t)1, (size_t)7, (size_t)255, (size_t)256, (size_t)257, (size_t)1152, (size_t)4096 })
            same = same && detectIn(rec, 1234, 11, 5000, chunk) == whole;
        expect("D6 分批喂(1 / 7 / 255 / 256 / 257 / 1152 / 4096 帧一批)与一次喂完结果相同", same);
    }
    // ---- D7 记录没到齐:标记报、采样率不报(不提前读);到齐的那一帧才报 ----
    {
        const std::vector<uint8_t> rec = synth::startRecordBits(96000, 48000);
        int early = 0, markerMissing = 0;
        for (size_t keep = 40; keep < 84; ++keep) {
            const Seen2 s = detectIn(rec, 300, 8, 300 + keep);
            if (s.rate != 0) ++early;
            if (!s.marker) ++markerMissing;
        }
        const Seen2 before = detectIn(rec, 300, 8, 300 + 39), full = detectIn(rec, 300, 8, 300 + 84);
        snprintf(note, sizeof note, "留 40..83 帧:提前报采样率的 %d 种,没报标记的 %d 种;留 39 帧:标记 %d;留 84 帧:%d", early, markerMissing, before.marker, full.rate);
        expect("D7 输入在记录中间断了:标记到齐(40 帧)就报找到,采样率要整条记录到齐、校验过了才报", early == 0 && markerMissing == 0 && !before.marker && full.rate == 96000,
               note);
    }
    // ---- D8 第一条记录坏了,后面还有好的:读后面那条。长度字段坏了(把后面的也算进去)也一样 ----
    {
        auto twoRecords = [](size_t flipAt, size_t gap) {
            std::vector<uint8_t> bad = synth::startRecordBits(88200, 44100);
            bad[flipAt] ^= 1;
            std::vector<int32_t> lr = synth::noise(40000, 24, 9);
            synth::embedControl(lr, 24, 500, bad);
            synth::embedControl(lr, 24, 500 + 84 + gap, synth::startRecordBits(192000, 48000));
            const std::vector<int32_t> lj = leftJustified(lr, 24);
            syncdetect::Detector d;
            d.feed(lj.data(), lj.size() / 2);
            return d.markerFound() ? d.originalRate() : -1;
        };
        const int a = twoRecords(83, 600), b = twoRecords(83, 0), c = twoRecords(79, 600), e = twoRecords(40, 600);
        snprintf(note, sizeof note, "校验坏、隔 600 帧:%d;紧挨着:%d;附加项个数坏(第 79 位):%d;\"带位置\"那一位坏:%d", a, b, c, e);
        expect("D8 第一条记录坏了、后面还有一条好的:读到的是后面那条(192000),长度字段坏了也不把后面那条吞掉", a == 192000 && b == 192000 && c == 192000 && e == 192000,
               note);
    }
    // ---- D9 不带标记的声音;无效的码;读到之后不再变 ----
    {
        std::vector<int32_t> noise24 = leftJustified(synth::noise(60000, 24, 3), 24), silence(120000, 0);
        syncdetect::Detector n, z;
        n.feed(noise24.data(), noise24.size() / 2);
        z.feed(silence.data(), silence.size() / 2);
        int invalidRead = 0, invalidNoMarker = 0;
        for (unsigned code = 24; code < 32; ++code) {
            synth::Record r;
            r.originalCode = code;
            const Seen2 s = detectIn(synth::recordBits(r));
            if (s.rate != 0) ++invalidRead;
            if (!s.marker) ++invalidNoMarker;
        }
        // 读到 88200 之后再来一条 192000:不变
        std::vector<int32_t> lr = synth::noise(4000, 24, 4);
        synth::embedControl(lr, 24, 100, synth::startRecordBits(88200, 44100));
        synth::embedControl(lr, 24, 2000, synth::startRecordBits(192000, 48000), 12);
        const std::vector<int32_t> lj = leftJustified(lr, 24);
        syncdetect::Detector d;
        d.feed(lj.data(), lj.size() / 2);
        snprintf(note, sizeof note, "噪声 %d,静音 %d;无效的码:读出采样率 %d 种、没报标记 %d 种;两条记录:%d / 位 %d;帧数 %llu", n.markerFound(), z.markerFound(),
                 invalidRead, invalidNoMarker, d.originalRate(), d.controlBit(), (unsigned long long)d.framesSeen());
        expect("D9 噪声与静音里找不到;无效的采样率码(24..31)报找到、不报采样率;读到之后不再变;喂过的帧数照数",
               !n.markerFound() && !z.markerFound() && n.originalRate() == 0 && invalidRead == 0 && invalidNoMarker == 0 && d.originalRate() == 88200 &&
                   d.controlBit() == 8 && d.framesSeen() == 4000,
               note);
    }
}

// =============================================================================
// 给人看的文字(src/messages.h)、进度条、取消之后的那一句、另存的文件名与标签键
// =============================================================================
namespace {

bool isHanzi(wchar_t c) { return c >= 0x4E00 && c <= 0x9FFF; }
bool isCjkAny(wchar_t c) { return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x3000 && c <= 0x303F); }
bool isAsciiLetter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }

// 一句里用到的位置号({0}..{9})
unsigned placeholders(const wchar_t* s) {
    unsigned m = 0;
    for (const wchar_t* p = s; *p; ++p)
        if (p[0] == L'{' && p[1] >= L'0' && p[1] <= L'9' && p[2] == L'}') m |= 1u << (p[1] - L'0');
    return m;
}

// 一个源文件里,字符串字面量中带汉字的那些行(行号);注释里的不算
std::vector<int> hanziLiteralLines(const std::vector<uint8_t>& src) {
    std::vector<int> lines;
    enum { Code, LineComment, BlockComment, Str, Chr } st = Code;
    int line = 1;
    bool hit = false;
    for (size_t i = 0; i < src.size(); ++i) {
        const uint8_t c = src[i], n = i + 1 < src.size() ? src[i + 1] : 0;
        if (c == '\n') {
            if (hit) lines.push_back(line);
            hit = false;
            ++line;
            if (st == LineComment) st = Code;
            continue;
        }
        switch (st) {
        case Code:
            if (c == '/' && n == '/') st = LineComment;
            else if (c == '/' && n == '*') st = BlockComment;
            else if (c == '"') st = Str;
            else if (c == '\'') st = Chr;
            break;
        case BlockComment:
            if (c == '*' && n == '/') { st = Code; ++i; }
            break;
        case Str:
        case Chr:
            if (c == '\\') { ++i; break; }
            if ((st == Str && c == '"') || (st == Chr && c == '\'')) { st = Code; break; }
            if (c >= 0xE4 && c <= 0xE9 && i + 2 < src.size()) {      // UTF-8 三字节:U+4000..U+9FFF 这一段的开头
                const unsigned cp = ((c & 0x0Fu) << 12) | ((src[i + 1] & 0x3Fu) << 6) | (src[i + 2] & 0x3Fu);
                if (cp >= 0x4E00 && cp <= 0x9FFF) hit = true;
            }
            break;
        default: break;
        }
    }
    if (hit) lines.push_back(line);
    return lines;
}

} // namespace

static void runTextChecks() {
    char note[700];
    // ---- 30 文字表:每一条两半都有 ----
    {
        int n = 0, emptyHalf = 0, noHanzi = 0, cjkInEnglish = 0, placeholderGap = 0, sameHalves = 0, dupName = 0, edgeSpace = 0;
        std::string first;
        std::vector<std::string> names;
        for (const msg::Entry* e : msg::all()) {
            ++n;
            const std::wstring en = e->en, zh = e->zh;
            bool bad = false;
            if (en.empty() || zh.empty()) { ++emptyHalf; bad = true; }
            bool letters = false, hanzi = false, cjkEn = false;
            for (wchar_t c : en) {
                letters = letters || isAsciiLetter(c);
                cjkEn = cjkEn || isCjkAny(c);
            }
            for (wchar_t c : zh) hanzi = hanzi || isHanzi(c);
            if (letters && !hanzi) { ++noHanzi; bad = true; }                     // 英文那一半有字,中文那一半就得有汉字
            if (cjkEn) { ++cjkInEnglish; bad = true; }
            if (placeholders(e->en) & ~placeholders(e->zh)) { ++placeholderGap; bad = true; }
            if (letters && en == zh) { ++sameHalves; bad = true; }
            if (!zh.empty() && (zh.front() == L' ' || zh.back() == L' ')) { ++edgeSpace; bad = true; }
            for (const std::string& x : names)
                if (x == e->name) { ++dupName; bad = true; }
            names.push_back(e->name);
            if (bad && first.empty()) first = e->name;
        }
        snprintf(note, sizeof note, "%d 条;缺一半的 %d,中文那一半没有汉字的 %d,英文那一半里有中文字符的 %d,英文用了中文没有的位置号 %d,两半一样的 %d,重名 %d,"
                                    "中文头尾带空格 %d;第一条不对的:%s",
                 n, emptyHalf, noHanzi, cjkInEnglish, placeholderGap, sameHalves, dupName, edgeSpace, first.empty() ? "-" : first.c_str());
        expect("30 文字表(src/messages.h)每一条:英文、中文两半都不空,中文那一半有汉字、英文那一半没有中文字符,英文用到的位置号中文都有,名字不重",
               n > 200 && !emptyHalf && !noHanzi && !cjkInEnglish && !placeholderGap && !sameHalves && !dupName && !edgeSpace, note);
    }
    // ---- 31 控制台上的写法:English (中文);数据原样进两半;放进去的东西不再当位置号 ----
    {
        const std::wstring complete = msg::of(msg::StComplete).both();
        const msg::Text data(L"C:\\Music\\a.flac");
        const msg::Text keys(L"ENCODER, MQAENCODER", L"ENCODER、MQAENCODER");
        const msg::Text t = msg::of(msg::StRepairIncomplete, { keys });
        const msg::Text tricky = msg::of(msg::StNotLossless, { msg::Text(L"{0}{1}") });
        const msg::Text joined = msg::of(msg::FixWriteFailed, { msg::Text(L"x") }) + msg::of(msg::FixTailRestored);
        const bool ok = complete == std::wstring(msg::StComplete.en) + L" (" + msg::StComplete.zh + L")" && data.both() == L"C:\\Music\\a.flac" &&
                        t.en.find(L"(ENCODER, MQAENCODER)") != std::wstring::npos && t.zh.find(L"ENCODER、MQAENCODER") != std::wstring::npos &&
                        t.both() == t.en + L" (" + t.zh + L")" && tricky.en.find(L"({0}{1})") != std::wstring::npos && msg::Text().both().empty() &&
                        joined.en == std::wstring(msg::FixWriteFailed.en) + msg::FixTailRestored.en && joined.zh.find(L"x") != std::wstring::npos &&
                        msg::num(-12).both() == L"-12";
        expect("31 一句话的写法:控制台上是「English (中文)」,英文在前、中文在半角括号里;文件名与数字原样、只写一次;两句可以接起来", ok, wu::utf8(complete).c_str());
    }
    // ---- 32 别的源文件里不写给人看的中文字面文字(都在 messages.h;自检里的文件名、标签值是数据,不算)----
    {
#ifdef TAGREPAIR_SOURCE_DIR
        const std::wstring dir = std::wstring(TAGREPAIR_SOURCE_DIR) + L"\\src";
        WIN32_FIND_DATAW fd{};
        const HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        int files = 0, offenders = 0;
        std::string where;
        bool tableHasHanzi = false;
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = fd.cFileName;
                if (name.size() < 3 || (name.rfind(L".cpp") != name.size() - 4 && name.rfind(L".h") != name.size() - 2)) continue;
                ++files;
                const std::vector<int> lines = hanziLiteralLines(testfiles::readFile(dir + L"\\" + name));
                if (name == L"messages.h") { tableHasHanzi = lines.size() > 200; continue; }
                if (name == L"selftest.cpp") continue;                             // 造文件用的名字与标签值(数据)
                if (!lines.empty()) {
                    offenders += (int)lines.size();
                    if (where.empty()) where = wu::utf8(name) + ":" + std::to_string(lines[0]);
                }
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
        snprintf(note, sizeof note, "看了 %d 个源文件;字面量里带汉字的行 %d 处%s%s", files, offenders, where.empty() ? "" : ",第一处 ", where.c_str());
        expect("32 src/ 里除了 messages.h(文字表)与 selftest.cpp(造文件用的数据),没有带汉字的字符串字面量:给人看的话只在文字表里写", files > 15 && tableHasHanzi && offenders == 0,
               note);
#else
        expect("32 src/ 里除了 messages.h 没有带汉字的字符串字面量", false, "构建时没有给 TAGREPAIR_SOURCE_DIR");
#endif
        // 这个扫描自己认不认得出字面量与注释
        const std::string sample = "int a; // 注释里的字\nconst char* b = \"字面量里的字\";\n/* 块注释\n 还是注释 */ auto c = L\"ok\"; auto d = L\"也是\";\nchar e = '\\'';\n";
        const std::vector<int> got = hanziLiteralLines(std::vector<uint8_t>(sample.begin(), sample.end()));
        expect("32.1 这个扫描:注释里的汉字不算,字符串字面量里的算(第 2 行、第 4 行)", got == std::vector<int>({ 2, 4 }));
    }
    // ---- 33 进度条上的文字 ----
    {
        const std::wstring label = msg::of(msg::PgRepair).both();
        const std::wstring start = cli::progressText(label, 0, 8, 0, L"a.flac"), mid = cli::progressText(label, 3, 8, 0.5, L"d.flac"),
                           last = cli::progressText(label, 7, 8, 1.0, L"h.flac"), none = cli::progressText(label, 0, 0, 0, L"");
        auto filled = [](const std::wstring& s) {
            int n = 0;
            for (wchar_t c : s) n += c == L'#';
            return n;
        };
        bool monotonic = true;
        int prev = -1;
        for (size_t i = 0; i < 8; ++i)
            for (int k = 0; k <= 10; ++k) {
                const int f = filled(cli::progressText(label, i, 8, k / 10.0, L"x"));
                monotonic = monotonic && f >= prev;
                prev = f;
            }
        const bool ok = start.find(L"Repairing (修复) [------------------------]   0%  1/8  a.flac") == 0 && filled(mid) == 10 &&
                        mid.find(L" 43%  4/8  d.flac") != std::wstring::npos && filled(last) == cli::kProgressCells &&
                        last.find(L"100%  8/8  h.flac") != std::wstring::npos && filled(none) == cli::kProgressCells && monotonic &&
                        filled(cli::progressText(label, 2, 8, 7.0, L"x")) == filled(cli::progressText(label, 2, 8, 1.0, L"x")) &&
                        wu::displayWidth(L"ab修复") == 6;
        expect("33 进度条:「Repairing (修复) [####----] 43%  4/8  文件名」—— 按「做完的文件 + 手上这个做到的比例」走,每个文件至少动一格的量、文件中途也动,只往前不往回",
               ok, wu::utf8(mid).c_str());
    }
    // ---- 34 取消之后的那一句 ----
    {
        const std::wstring plain = cli::cancelText(msg::CancelRepair, 2, 5, 0, 0, 0, 3).both();
        const std::wstring full = cli::cancelText(msg::CancelRepair, 2, 6, 0, 1, 1, 2).both();
        const std::wstring save = cli::cancelText(msg::CancelSave, 0, 4, 0, 0, 1, 3).both();
        const std::wstring skip = cli::cancelText(msg::CancelSave, 1, 9, 6, 0, 1, 1).both();        // 1 + 6 + 1 + 1 = 9:几项加起来正好是总数
        const bool ok = plain == L"Cancelled. Repaired 2 of 5; 3 not started. (已取消。5 个里修好了 2 个,3 个没有开始。)" &&
                        full == L"Cancelled. Repaired 2 of 6; 1 could not be done, see above; 1 stopped part-way and left as it was; 2 not started. "
                                L"(已取消。6 个里修好了 2 个,1 个没有做成,原因见上面,1 个做到一半停下、保持原样,2 个没有开始。)" &&
                        save.find(L"Cancelled. Saved 0 of 4; 1 stopped part-way and left as it was; 3 not started.") == 0 &&
                        skip == L"Cancelled. Saved 1 of 9; 6 skipped (nothing to do for them); 1 stopped part-way and left as it was; 1 not started. "
                                L"(已取消。9 个里存好了 1 个,6 个不用处理,1 个做到一半停下、保持原样,1 个没有开始。)";
        expect("34 取消之后说清楚做了多少、没做多少:「Cancelled. Repaired 2 of 5; 3 not started. (已取消。5 个里修好了 2 个,3 个没有开始。)」", ok, wu::utf8(full).c_str());
    }
    // ---- 35 另存:目标文件名、带过去的标签键、位数 ----
    {
        using namespace tagrepair;
        const bool names = copyTargetPath(L"C:\\m\\a.wav", CopyKind::WavToFlac) == L"C:\\m\\a.flac" &&
                           copyTargetPath(L"C:\\m\\a.WAV", CopyKind::To24Bit) == L"C:\\m\\a.24bit.flac" &&
                           copyTargetPath(L"C:\\m\\a.flac", CopyKind::To24Bit) == L"C:\\m\\a.24bit.flac" &&
                           copyTargetPath(L"C:\\m.dir\\track", CopyKind::WavToFlac) == L"C:\\m.dir\\track.flac" &&
                           copyTargetPath(L"C:\\m\\01. Song.name.wav", CopyKind::WavToFlac) == L"C:\\m\\01. Song.name.flac" &&
                           copyTargetPath(L"C:\\m\\.hidden", CopyKind::WavToFlac) == L"C:\\m\\.hidden.flac" &&
                           copyTargetPath(L"a.wav", CopyKind::WavToFlac) == L"a.flac";
        const bool keys = commentKeyFor("title") == "TITLE" && commentKeyFor("album_artist") == "ALBUMARTIST" && commentKeyFor("track") == "TRACKNUMBER" &&
                          commentKeyFor("disc") == "DISCNUMBER" && commentKeyFor("Date") == "DATE" && commentKeyFor("encoder").empty() &&
                          commentKeyFor("MQAENCODER").empty() && commentKeyFor("originalsamplerate").empty() && commentKeyFor("").empty() &&
                          commentKeyFor("a=b").empty() && commentKeyFor("bad\tkey").empty() && commentKeyFor("\xE4\xB8\xAD").empty() &&
                          commentKeyFor("encoded_by") == "ENCODED_BY";
        expect("35 另存的文件名:<去掉扩展名的文件名>.flac / .24bit.flac,和源在同一个文件夹;带过去的标签键转成 Vorbis 注释的写法,三个识别标签与 encoder 不带、键名不合法的不带;"
               "16 bit 放进 24 bit 的格子 = 左移 8 位",
               names && keys && kPadTo24 == 8 && std::string(copyKindCode(CopyKind::WavToFlac)) == "wav_to_flac" &&
                   std::string(copyKindCode(CopyKind::To24Bit)) == "copy_24bit");
    }
    // ---- 36 新文件的 STREAMINFO(另存时自己拼的)与造测试文件的那一份拼法一致,读回来的数也对 ----
    {
        bool same = true, parsed = true;
        const struct { int rate, bits, block; uint64_t total; uint32_t mn, mx; } cases[4] = { { 44100, 16, 4096, 88200, 14, 16390 }, { 48000, 24, 4608, 96000, 3000, 27654 },
                                                                                              { 44100, 24, 4096, 0xF12345678ull, 1, 0xFFFFFF }, { 48000, 16, 16, 1, 0, 0 } };
        for (const auto& c : cases) {
            uint8_t md5[16];
            for (int i = 0; i < 16; ++i) md5[i] = (uint8_t)(c.rate + i * 13);
            flacmeta::StreamInfoFields f;
            f.minBlock = f.maxBlock = c.block;
            f.minFrame = c.mn;
            f.maxFrame = c.mx;
            f.sampleRate = c.rate;
            f.channels = 2;
            f.bits = c.bits;
            f.totalSamples = c.total;
            memcpy(f.md5, md5, 16);
            uint8_t body[34];
            flacmeta::buildStreamInfo(f, body);
            const std::vector<uint8_t> other = synth::streamInfo(c.rate, 2, c.bits, c.total, c.block, c.mn, c.mx, md5);
            same = same && other.size() == 34 && memcmp(other.data(), body, 34) == 0;
            const std::vector<uint8_t> file = synth::flacFile({ { kStreamInfo, std::vector<uint8_t>(body, body + 34) } }, fakeFrames());
            MemSource src(file.data(), file.size());
            Layout l;
            const msg::Entry* why = nullptr;
            parsed = parsed && readLayout(src, l, why) == Err::Ok && l.sampleRate == c.rate && l.bits == c.bits && l.channels == 2 && l.totalSamples == c.total &&
                     memcmp(l.streamInfo + 18, md5, 16) == 0;
        }
        expect("36 另存时拼的 STREAMINFO:与造测试文件用的那一份(分开写的两份代码)逐字节相同;采样率、位深、声道数、总采样数、MD5 读回来都对", same && parsed);
    }
    // ---- 37 一个文件有哪几类、结果有哪几种:代码串没有重复,新加的类别不占用宿主认识的名字 ----
    {
        using namespace tagrepair;
        std::vector<std::string> states;
        bool unique = true;
        for (int i = 0; i < kStates; ++i) {
            const std::string c = stateCode((State)i);
            for (const std::string& x : states) unique = unique && x != c;
            states.push_back(c);
        }
        expect("37 类别的代码串各不相同;宿主认识的那几个名字没变(complete / repairable / rate_unknown / damaged / unsupported_container / no_marker / not_lossless / error),"
               "WAV 的新类别叫 convertible",
               unique && std::string(stateCode(State::Complete)) == "complete" && std::string(stateCode(State::Repairable)) == "repairable" &&
                   std::string(stateCode(State::RateUnknown)) == "rate_unknown" && std::string(stateCode(State::Damaged)) == "damaged" &&
                   std::string(stateCode(State::Unsupported)) == "unsupported_container" && std::string(stateCode(State::NoMarker)) == "no_marker" &&
                   std::string(stateCode(State::NotLossless)) == "not_lossless" && std::string(stateCode(State::Error)) == "error" &&
                   std::string(stateCode(State::Convertible)) == "convertible");
    }
}

// ---------------------------------------------------------------------------
// bilingual <文件>:一段抓下来的控制台输出(UTF-8),每一行要么是数据(文件名、标签值、命令写法),要么英文、中文两半都有
// ---------------------------------------------------------------------------
// 用的输入里文件名全是 ASCII(tests/process_tests.cmake 用 many 造的那一套),所以:
//   有汉字的行 —— 第一个汉字之前必须有英文字母,而且已经出现过 " ("(英文在前,中文在括号里);
//   没有汉字的行 —— 必须是认得的数据行;别的带字母的行就是只有英文的话
static int cmdBilingual(const std::wstring& path) {
    const testfiles::Bytes raw = testfiles::readFile(path);
    if (raw.empty()) { say("读不到这个文件,或者它是空的"); return 2; }
    const std::wstring text = wu::wide(std::string(raw.begin(), raw.end()));
    int lines = 0, bad = 0, both = 0, control = 0;
    for (uint8_t c : raw) control += (c < 0x20 && c != '\n') || c == 0x7F;        // 接走的输出里不许有回车符、退格、ESC 之类(进度条只在窗口里画)
    if (control) {
        say("输出里有控制字符 " + std::to_string(control) + " 个(回车符、ESC 之类)");
        return 1;
    }
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (line.empty()) continue;
        ++lines;
        size_t firstHanzi = std::wstring::npos;
        bool letters = false, lettersBefore = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (isHanzi(line[i]) && firstHanzi == std::wstring::npos) firstHanzi = i;
            if (isAsciiLetter(line[i])) {
                letters = true;
                if (firstHanzi == std::wstring::npos) lettersBefore = true;
            }
        }
        bool ok;
        if (firstHanzi != std::wstring::npos) {
            const size_t open = line.find(L" (");
            ok = lettersBefore && open != std::wstring::npos && open < firstHanzi && line.find(L')', firstHanzi) != std::wstring::npos;
            if (ok) ++both;
        } else {
            size_t i = 0;
            while (i < line.size() && line[i] == L' ') ++i;
            const std::wstring body = line.substr(i);
            bool header = body.size() > 4 && body[0] == L'[' && body[1] >= L'0' && body[1] <= L'9' && body.find(L"] ") != std::wstring::npos;
            bool tagLine = false;
            for (int k = 0; k < tagrepair::kKeys; ++k) tagLine = tagLine || body.compare(0, strlen(tagrepair::keyName(k)) + 1, wu::wide(tagrepair::keyName(k)) + L" ") == 0;
            const bool pathLine = body.size() > 3 && isAsciiLetter(body[0]) && body[1] == L':' && body[2] == L'\\';
            const bool syntax = body.compare(0, 10, L"tagrepair ") == 0 || body.compare(0, 3, L"[--") == 0;
            ok = !letters || header || tagLine || pathLine || syntax;
        }
        if (!ok) {
            ++bad;
            say("只有一种语言的一行:" + wu::utf8(line));
        }
    }
    char b[160];
    snprintf(b, sizeof b, "%d 行,其中两半都有的 %d 行,不合格 %d 行", lines, both, bad);
    say(b);
    return bad || !both ? 1 : 0;
}

// ---------------------------------------------------------------------------
// many <新的空文件夹> <个数>:一批名字全是 ASCII 的合成文件 —— <个数> 个丢了标签的带标记 FLAC(track_01.flac …,单数的有 PADDING、
// 双数的没有:两条写入的路都走到),外加一个带标记的 24 bit WAV、一个带标记的 16 bit FLAC、一个带标记的 16 bit WAV、一个普通 FLAC
// ---------------------------------------------------------------------------
static int cmdMany(const std::wstring& dirIn, int count) {
    using namespace testfiles;
    const std::wstring dir = wu::longPath(dirIn);
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES || !CreateDirectoryW(dir.c_str(), nullptr)) {
        say("这个文件夹已经存在或者建不了:只往新建的文件夹里写");
        return 2;
    }
    if (count < 1 || count > 99) { say("个数要在 1 到 99 之间"); return 2; }
    int failed = 0;
    auto put = [&](const std::wstring& name, const Bytes& d) {
        if (!writeFile(dir + L"\\" + name, d)) ++failed;
    };
    for (int i = 1; i <= count; ++i) {
        wchar_t name[32];
        _snwprintf_s(name, _TRUNCATE, L"track_%02d.flac", i);
        std::vector<synth::Block> blocks = { { kVorbisComment, vc("v", { "TITLE=Track " + std::to_string(i) }) } };
        if (i % 2) blocks.push_back({ kPadding, zeros(600) });
        put(name, flac(pcm(44100, 24, 2.0, 1000 + i, 352800, 100 + (uint32_t)i), 44100, 24, blocks));
    }
    put(L"marked_24bit.wav", wav(pcm(44100, 24, 2.0, 2000, 352800, 201), 44100, 24));
    put(L"cd_16bit.flac", flac(pcm(44100, 16, 2.0, 2000, 88200, 202), 44100, 16, { { kVorbisComment, vc("v", { "TITLE=Disc" }) } }));
    put(L"rip_16bit.wav", wav(pcm(44100, 16, 2.0, 2000, 88200, 203), 44100, 16));
    put(L"ordinary.flac", flac(pcm(44100, 24, 2.0, -1, 0, 204), 44100, 24, { { kVorbisComment, vc("v", { "TITLE=Ordinary" }) }, { kPadding, zeros(600) } }));
    say(failed ? "有写不了的文件" : "写好了");
    return failed ? 1 : 0;
}

// ---------------------------------------------------------------------------
// pty <超时毫秒> <步骤>… -- <程序> <参数>…:在一个看不见的伪控制台里跑程序(它以为自己在控制台窗口里),按步骤喂键盘输入,
// 把它往"屏幕"上写的东西(带终端控制序列的 UTF-8)原样写到标准输出。退出码 = 那个程序的退出码(超时 98,起不来 97)。
// 步骤:wait:<文字>(等到输出里出现这段文字)、type:<文字>(敲这段字再回车)、ctrlc(按 Ctrl+C)、close(关窗口)、sleep:<毫秒>
// 不开任何可见的窗口。验的是只有在控制台里才有的那几样:进度条在同一行上重画、Ctrl+C、关窗口
// ---------------------------------------------------------------------------
namespace {

struct PtyOut {
    CRITICAL_SECTION lock;
    std::string text;
    HANDLE pipe = nullptr;
};

DWORD WINAPI ptyReader(void* p) {
    PtyOut* o = (PtyOut*)p;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(o->pipe, buf, sizeof buf, &n, nullptr) && n) {
        EnterCriticalSection(&o->lock);
        o->text.append(buf, n);
        LeaveCriticalSection(&o->lock);
    }
    return 0;
}

std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') q += L'\\';
        q += c;
    }
    return q + L"\"";
}

} // namespace

static int cmdPty(int argc, wchar_t** argv) {
    // argv:pty <超时> <步骤>… -- <程序> <参数>…
    const DWORD timeoutMs = (DWORD)_wtoi(argv[2]);
    int split = -1;
    for (int i = 3; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--") { split = i; break; }
    if (split < 0 || split + 1 >= argc || timeoutMs == 0) { say("pty <超时毫秒> <步骤>… -- <程序> <参数>…"); return 2; }
    std::wstring cmd;
    for (int i = split + 1; i < argc; ++i) cmd += (cmd.empty() ? L"" : L" ") + quoteArg(argv[i]);

    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) || !CreatePipe(&outRead, &outWrite, nullptr, 0)) return 97;
    HPCON pc = nullptr;
    COORD size;
    size.X = 200;
    size.Y = 60;
    if (FAILED(CreatePseudoConsole(size, inRead, outWrite, 0, &pc))) return 97;
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;            // 三个句柄都留空:子进程用伪控制台的,不沿用我们自己被接走的标准输入 / 输出
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<char> mem(bytes);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)mem.data();
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof pc, nullptr, nullptr))
        return 97;
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi)) {
        ClosePseudoConsole(pc);
        return 97;
    }
    CloseHandle(inRead);
    CloseHandle(outWrite);
    PtyOut out;
    InitializeCriticalSection(&out.lock);
    out.pipe = outRead;
    const HANDLE reader = CreateThread(nullptr, 0, ptyReader, &out, 0, nullptr);
    const ULONGLONG t0 = GetTickCount64();
    const auto late = [&]() { return GetTickCount64() - t0 >= timeoutMs; };
    const auto exited = [&]() { return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0; };
    const auto send = [&](const std::string& keys) {
        DWORD n = 0;
        WriteFile(inWrite, keys.data(), (DWORD)keys.size(), &n, nullptr);
    };
    size_t searched = 0;                                      // 每个 wait 从上一个 wait 找到的地方之后找
    bool closed = false, timedOut = false;
    for (int i = 3; i < split && !timedOut; ++i) {
        const std::wstring step = argv[i];
        if (step.compare(0, 5, L"wait:") == 0) {
            const std::string want = wu::utf8(step.substr(5));
            for (;;) {
                EnterCriticalSection(&out.lock);
                const size_t hit = out.text.find(want, searched);
                LeaveCriticalSection(&out.lock);
                if (hit != std::string::npos) { searched = hit + want.size(); break; }
                if (late()) { timedOut = true; break; }
                if (exited()) { Sleep(200); EnterCriticalSection(&out.lock); const bool there = out.text.find(want, searched) != std::string::npos; LeaveCriticalSection(&out.lock); if (!there) break; }
                Sleep(10);
            }
        } else if (step.compare(0, 5, L"type:") == 0) {
            send(wu::utf8(step.substr(5)) + "\r");
        } else if (step == L"ctrlc") {
            send("\x03");
        } else if (step == L"close") {
            ClosePseudoConsole(pc);                           // = 关窗口:里面的程序收到 CTRL_CLOSE_EVENT
            closed = true;
        } else if (step.compare(0, 6, L"sleep:") == 0) {
            Sleep((DWORD)_wtoi(step.substr(6).c_str()));
        }
    }
    DWORD code = 98;
    const DWORD left = late() ? 0 : (DWORD)(timeoutMs - (GetTickCount64() - t0));
    if (!timedOut && WaitForSingleObject(pi.hProcess, left) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 98);
    if (!closed) ClosePseudoConsole(pc);                      // 伪控制台关掉,输出管道才到头
    CloseHandle(inWrite);
    WaitForSingleObject(reader, 5000);
    EnterCriticalSection(&out.lock);
    const std::string all = out.text;
    LeaveCriticalSection(&out.lock);
    const HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD wrote = 0;
    for (size_t done = 0; done < all.size(); done += wrote)
        if (!WriteFile(so, all.data() + done, (DWORD)(all.size() - done), &wrote, nullptr) || !wrote) break;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int wmain(int argc, wchar_t** argv) {
    const std::wstring cmd = argc >= 2 ? argv[1] : L"";
    if (cmd == L"samples" && argc == 3) return cmdSamples(argv[2]);
    if (cmd == L"strip" && argc == 4) return cmdStrip(argv[2], argv[3]);
    if (cmd == L"many" && argc == 4) return cmdMany(argv[2], _wtoi(argv[3]));
    if (cmd == L"bilingual" && argc == 3) return cmdBilingual(argv[2]);
    if (cmd == L"pty" && argc >= 5) return cmdPty(argc, argv);
    if (argc != 1) {
        say("tagrepair_tests              跑全部核对");
        say("tagrepair_tests samples <新的空文件夹>");
        say("tagrepair_tests many <新的文件夹> <个数>");
        say("tagrepair_tests strip <入.flac> <出.flac>");
        say("tagrepair_tests bilingual <抓下来的输出.txt>");
        say("tagrepair_tests pty <超时毫秒> <步骤>… -- <程序> <参数>…");
        return 2;
    }
    runChecks();
    runTextChecks();
    runDetectorChecks();
    char sum[96];
    snprintf(sum, sizeof sum, "---- 通过 %d,失败 %d ----", g_pass, g_fail);
    say("");
    say(sum);
    return g_fail ? 1 : 0;
}
