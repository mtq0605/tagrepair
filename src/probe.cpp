#include "probe.h"

#include "sync_detect.h"
#include "win_util.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace probe {

namespace {

// ---- 经句柄读的 AVIO ----
struct HandleIo {
    HANDLE h = INVALID_HANDLE_VALUE;
    int64_t pos = 0, size = 0;
};

int ioRead(void* opaque, uint8_t* buf, int n) {
    HandleIo* io = (HandleIo*)opaque;
    if (io->pos >= io->size) return AVERROR_EOF;
    OVERLAPPED ov{};
    ov.Offset = (DWORD)io->pos;
    ov.OffsetHigh = (DWORD)((uint64_t)io->pos >> 32);
    DWORD got = 0;
    if (!ReadFile(io->h, buf, (DWORD)n, &got, &ov)) return GetLastError() == ERROR_HANDLE_EOF ? AVERROR_EOF : AVERROR(EIO);
    if (got == 0) return AVERROR_EOF;
    io->pos += got;
    return (int)got;
}

int64_t ioSeek(void* opaque, int64_t off, int whence) {
    HandleIo* io = (HandleIo*)opaque;
    if (whence & AVSEEK_SIZE) return io->size;
    int64_t to = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: to = off; break;
    case SEEK_CUR: to = io->pos + off; break;
    case SEEK_END: to = io->size + off; break;
    default: return AVERROR(EINVAL);
    }
    if (to < 0) return AVERROR(EINVAL);
    io->pos = to;
    return to;
}

struct Demux {
    HandleIo io;
    AVIOContext* pb = nullptr;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    int stream = -1;
    Demux() = default;
    Demux(const Demux&) = delete;
    Demux& operator=(const Demux&) = delete;
    ~Demux() {
        if (dec) avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        if (pb) {
            av_freep(&pb->buffer);
            avio_context_free(&pb);
        }
    }
};

bool openDemux(HANDLE file, const std::wstring& nameHint, Demux& d, msg::Text& err) {
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file, &sz)) { err = msg::of(msg::ProbeNoSize); return false; }
    d.io.h = file;
    d.io.size = sz.QuadPart;
    const int bufSize = 64 * 1024;
    uint8_t* buf = (uint8_t*)av_malloc(bufSize);
    if (!buf) { err = msg::of(msg::ProbeNoMemory); return false; }
    d.pb = avio_alloc_context(buf, bufSize, 0, &d.io, ioRead, nullptr, ioSeek);
    if (!d.pb) { av_free(buf); err = msg::of(msg::ProbeNoMemory); return false; }
    d.fmt = avformat_alloc_context();
    if (!d.fmt) { err = msg::of(msg::ProbeNoMemory); return false; }
    d.fmt->pb = d.pb;
    d.fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&d.fmt, wu::utf8(nameHint).c_str(), nullptr, nullptr) < 0) {     // 失败时 FFmpeg 已经把 fmt 放掉并置空
        err = msg::of(msg::ProbeNotAudio);
        return false;
    }
    if (avformat_find_stream_info(d.fmt, nullptr) < 0) { err = msg::of(msg::ProbeNoStreamInfo); return false; }
    d.stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    for (unsigned i = 0; d.stream < 0 && i < d.fmt->nb_streams; ++i)
        if (d.fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) d.stream = (int)i;
    if (d.stream < 0) { err = msg::of(msg::ProbeNoAudioTrack); return false; }
    for (unsigned i = 0; i < d.fmt->nb_streams; ++i)
        d.fmt->streams[i]->discard = ((int)i == d.stream) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    return true;
}

// 解码器照流的参数原样打开:参数照搬、包时基给上、不加选项
bool openDecoder(Demux& d, bool strict) {
    AVStream* st = d.fmt->streams[d.stream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return false;
    d.dec = avcodec_alloc_context3(codec);
    if (!d.dec) return false;
    if (avcodec_parameters_to_context(d.dec, st->codecpar) < 0) { avcodec_free_context(&d.dec); return false; }
    d.dec->pkt_timebase = st->time_base;
    if (strict) d.dec->err_recognition |= AV_EF_CRCCHECK | AV_EF_EXPLODE;      // 整轨核对时:帧 CRC 不对就算错
    if (avcodec_open2(d.dec, codec, nullptr) < 0) { avcodec_free_context(&d.dec); return false; }
    return true;
}

// 整数样本有几位(浮点返回 0)
int intBitsOf(const AVCodecContext* c) {
    switch (av_get_packed_sample_fmt((AVSampleFormat)c->sample_fmt)) {
    case AV_SAMPLE_FMT_U8:  return 8;
    case AV_SAMPLE_FMT_S16: return (c->bits_per_raw_sample > 0 && c->bits_per_raw_sample <= 16) ? c->bits_per_raw_sample : 16;
    case AV_SAMPLE_FMT_S32: return (c->bits_per_raw_sample > 16 && c->bits_per_raw_sample <= 32) ? c->bits_per_raw_sample : 32;
    default:                return 0;
    }
}

// 帧 → 左对齐 int32 交错
void frameToInt32(const AVFrame* f, std::vector<int32_t>& out) {
    const int ch = f->ch_layout.nb_channels, n = f->nb_samples;
    const AVSampleFormat fmt = (AVSampleFormat)f->format;
    const bool planar = av_sample_fmt_is_planar(fmt) != 0;
    const AVSampleFormat pf = av_get_packed_sample_fmt(fmt);
    out.resize((size_t)n * (size_t)ch);
    for (int c = 0; c < ch; ++c) {
        const uint8_t* src = planar ? f->extended_data[c] : f->extended_data[0];
        const int step = planar ? 1 : ch, off = planar ? 0 : c;
        int32_t* dst = out.data() + c;
        for (int i = 0; i < n; ++i, dst += ch) {
            const int k = i * step + off;
            switch (pf) {
            case AV_SAMPLE_FMT_S16: { int16_t v; std::memcpy(&v, src + 2 * k, 2); *dst = (int32_t)((uint32_t)(int32_t)v << 16); break; }
            case AV_SAMPLE_FMT_S32: { int32_t v; std::memcpy(&v, src + 4 * k, 4); *dst = v; break; }
            case AV_SAMPLE_FMT_U8:  { *dst = (int32_t)((uint32_t)((int)src[k] - 128) << 24); break; }
            default: *dst = 0; break;
            }
        }
    }
}

// 哪些编码算"无损 PCM":
// 描述里标了无损、没标有损;TrueHD / DTS / DSD / AC-3 系各有自己的类别,不在其内
bool isLosslessPcm(AVCodecID id) {
    switch (id) {
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
    case AV_CODEC_ID_TRUEHD:
    case AV_CODEC_ID_DTS:
    case AV_CODEC_ID_AC4:
    case AV_CODEC_ID_DSD_LSBF:
    case AV_CODEC_ID_DSD_MSBF:
    case AV_CODEC_ID_DSD_LSBF_PLANAR:
    case AV_CODEC_ID_DSD_MSBF_PLANAR:
        return false;
    default: break;
    }
    const AVCodecDescriptor* desc = avcodec_descriptor_get(id);
    return desc && (desc->props & AV_CODEC_PROP_LOSSLESS) && !(desc->props & AV_CODEC_PROP_LOSSY);
}

bool fileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

} // namespace

const wchar_t* ffmpegDllName(int i) {
    // 装的顺序:后一个要用前一个(avcodec 要 avutil 与 swresample,avformat 要 avcodec)
    static const wchar_t* const names[kFfmpegDlls] = { TAGREPAIR_AVUTIL_DLL, TAGREPAIR_SWRESAMPLE_DLL, TAGREPAIR_AVCODEC_DLL, TAGREPAIR_AVFORMAT_DLL };
    return i >= 0 && i < kFfmpegDlls ? names[i] : L"";
}

bool loadFfmpeg(const std::wstring& forcedDir, std::vector<std::wstring>* tried) {
    static int state = 0;                                     // 0 没试过 / 1 在 / −1 不在
    static std::vector<std::wstring> looked;
    if (state == 0) {
        wchar_t env[32768];
        const DWORD n = GetEnvironmentVariableW(L"TAGREPAIR_FFMPEG_DIR", env, 32768);
        looked = ffmpegSearchDirs(forcedDir, wu::exeDir(), n > 0 && n < 32768 ? std::wstring(env, n) : std::wstring());
        state = -1;
        for (const std::wstring& c : looked) {
            bool all = true;
            for (int i = 0; i < kFfmpegDlls; ++i) all = all && fileExists(c + L"\\" + ffmpegDllName(i));
            if (!all) continue;                               // 四个要在同一处:缺一个就看下一处,不拿别处的来凑
            SetDllDirectoryW(c.c_str());                      // 延迟加载按名字找的时候也到这里找
            for (int i = 0; i < kFfmpegDlls; ++i)
                all = all && LoadLibraryExW((c + L"\\" + ffmpegDllName(i)).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr;
            if (all) { state = 1; break; }
        }
        if (state > 0) av_log_set_level(AV_LOG_QUIET);        // FFmpeg 自己的提示不往外打;原因由我们说
    }
    if (tried) *tried = looked;
    return state > 0;
}

bool inspect(HANDLE file, const std::wstring& nameHint, Info& out) {
    out = Info();
    Demux d;
    if (!openDemux(file, nameHint, d, out.error)) return false;
    AVStream* st = d.fmt->streams[d.stream];
    const AVCodecParameters* cp = st->codecpar;
    out.container = d.fmt->iformat && d.fmt->iformat->name ? d.fmt->iformat->name : "";
    out.codec = avcodec_get_name(cp->codec_id);
    out.rate = cp->sample_rate;
    out.channels = cp->ch_layout.nb_channels;
    for (int k = 0; k < tagrepair::kKeys; ++k)
        if (const AVDictionaryEntry* e = av_dict_get(d.fmt->metadata, tagrepair::keyName(k), nullptr, 0)) {
            out.has[k] = true;
            out.value[k] = e->value ? e->value : "";
        }
    for (const AVDictionaryEntry* e = nullptr; (e = av_dict_get(d.fmt->metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr;)
        out.meta.push_back({ e->key ? e->key : "", e->value ? e->value : "" });
    out.opened = true;
    out.losslessPcm = isLosslessPcm(cp->codec_id);
    if (!out.losslessPcm) return true;
    if (!openDecoder(d, false)) return true;                  // 没有解码器:找不了同步标记
    out.bits = intBitsOf(d.dec);
    if (d.dec->sample_rate > 0) out.rate = d.dec->sample_rate;
    if (d.dec->ch_layout.nb_channels > 0) out.channels = d.dec->ch_layout.nb_channels;
    if (!(out.channels == 2 && out.bits >= 16 && (out.rate == 44100 || out.rate == 48000))) return true;
    out.sniffed = true;
    syncdetect::Detector det;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    std::vector<int32_t> buf;
    const uint64_t limit = (uint64_t)d.dec->sample_rate * 10;
    while (pkt && f && det.framesSeen() < limit && av_read_frame(d.fmt, pkt) >= 0) {
        if (pkt->stream_index == d.stream && avcodec_send_packet(d.dec, pkt) == 0) {
            while (avcodec_receive_frame(d.dec, f) == 0) {
                frameToInt32(f, buf);
                det.feed(buf.data(), (size_t)f->nb_samples);
                av_frame_unref(f);
            }
        }
        av_packet_unref(pkt);
        if (det.markerFound() && det.originalRate() > 0) break;
    }
    out.marker = det.markerFound();
    out.origRate = det.originalRate();
    av_frame_free(&f);
    av_packet_free(&pkt);
    return true;
}

bool decodeAll(HANDLE file, const std::wstring& nameHint, Pcm& out, msg::Text& err, const Watch* watch) {
    out = Pcm();
    Demux d;
    if (!openDemux(file, nameHint, d, err)) return false;
    if (!openDecoder(d, true)) { err = msg::of(msg::ProbeNoDecoder); return false; }
    wu::Hash md5(BCRYPT_MD5_ALGORITHM);
    if (!md5.ok()) { err = msg::of(msg::ProbeNoMd5); return false; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    if (!pkt || !f) { av_frame_free(&f); av_packet_free(&pkt); err = msg::of(msg::ProbeNoMemory); return false; }
    std::vector<int32_t> buf;
    auto drain = [&]() {
        for (;;) {
            const int r = avcodec_receive_frame(d.dec, f);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) { ++out.errors; break; }
            frameToInt32(f, buf);
            md5.update(buf.data(), buf.size() * sizeof(int32_t));
            if (watch && watch->sink) watch->sink(buf.data(), buf.size());
            out.frames += (uint64_t)f->nb_samples;
            av_frame_unref(f);
        }
    };
    while (av_read_frame(d.fmt, pkt) >= 0) {
        if (pkt->stream_index == d.stream) {
            if (avcodec_send_packet(d.dec, pkt) < 0) ++out.errors;
            drain();
        }
        av_packet_unref(pkt);
        if (watch && watch->tick && !watch->tick(d.io.size > 0 ? (double)d.io.pos / (double)d.io.size : 1.0)) {      // 说了停:到此为止
            out.cancelled = true;
            break;
        }
    }
    if (!out.cancelled) {
        avcodec_send_packet(d.dec, nullptr);
        drain();
    }
    av_frame_free(&f);
    av_packet_free(&pkt);
    if (out.cancelled) { err = msg::of(msg::ProbeCancelled); return false; }
    out.md5 = md5.hex();
    if (out.md5.empty()) { err = msg::of(msg::ProbeNoMd5); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// 另存:源的全部采样 → 一个新的 FLAC
// ---------------------------------------------------------------------------
bool writeFlacCopy(HANDLE src, const std::wstring& nameHint, HANDLE out, const FlacWrite& spec, FlacWritten& res, const Watch* watch) {
    res = FlacWritten();
    auto failed = [&](WriteFail why) {
        res.fail = why;
        return false;
    };
    Demux d;
    if (!openDemux(src, nameHint, d, res.sourceError)) return failed(WriteFail::Source);
    if (!openDecoder(d, true)) { res.sourceError = msg::of(msg::ProbeNoDecoder); return failed(WriteFail::Source); }
    res.srcBits = intBitsOf(d.dec);
    res.rate = d.dec->sample_rate;
    res.channels = d.dec->ch_layout.nb_channels;
    // 只做这两种:位深不变(16 → 16、24 → 24),或者 16 bit 的采样放进 24 bit 的格子
    if (res.channels != 2 || res.rate <= 0 || (res.srcBits != 16 && res.srcBits != 24) || (spec.outBits != 16 && spec.outBits != 24) ||
        spec.padBits < 0 || spec.outBits != res.srcBits + spec.padBits || spec.metaAfterStreamInfo.empty())
        return failed(WriteFail::Format);

    struct Encoder {
        AVCodecContext* ctx = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* pkt = nullptr;
        AVPacket* inPkt = nullptr;
        AVFrame* inFrame = nullptr;
        ~Encoder() {
            av_frame_free(&inFrame);
            av_packet_free(&inPkt);
            av_packet_free(&pkt);
            av_frame_free(&frame);
            if (ctx) avcodec_free_context(&ctx);
        }
    } e;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
    if (!codec) return failed(WriteFail::NoEncoder);
    e.ctx = avcodec_alloc_context3(codec);
    e.frame = av_frame_alloc();
    e.pkt = av_packet_alloc();
    e.inPkt = av_packet_alloc();
    e.inFrame = av_frame_alloc();
    if (!e.ctx || !e.frame || !e.pkt || !e.inPkt || !e.inFrame) return failed(WriteFail::NoEncoder);
    const AVSampleFormat encFmt = spec.outBits == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
    e.ctx->sample_fmt = encFmt;
    e.ctx->bits_per_raw_sample = spec.outBits;
    e.ctx->sample_rate = res.rate;
    av_channel_layout_default(&e.ctx->ch_layout, 2);
    e.ctx->time_base = AVRational{ 1, res.rate };
    if (avcodec_open2(e.ctx, codec, nullptr) < 0) return failed(WriteFail::NoEncoder);
    const size_t block = e.ctx->frame_size > 0 ? (size_t)e.ctx->frame_size : 4096;

    // ---- 文件头:"fLaC" + STREAMINFO(先占位,编完再填)+ 调用方给的块 ----
    uint64_t at = 0;
    auto put = [&](const uint8_t* p, size_t n) {
        if (!wu::writeAt(out, at, p, n)) {
            res.winError = GetLastError();
            return false;
        }
        at += n;
        return true;
    };
    {
        uint8_t head[4 + 4 + 34] = { 'f', 'L', 'a', 'C', flacmeta::kStreamInfo, 0, 0, 34 };
        if (!put(head, sizeof head) || !put(spec.metaAfterStreamInfo.data(), spec.metaAfterStreamInfo.size())) return failed(WriteFail::Write);
    }

    wu::Hash md5Src(BCRYPT_MD5_ALGORITHM), md5SrcNative(BCRYPT_MD5_ALGORITHM), md5Out(BCRYPT_MD5_ALGORITHM);
    if (!md5Src.ok() || !md5SrcNative.ok() || !md5Out.ok()) { res.sourceError = msg::of(msg::ProbeNoMd5); return failed(WriteFail::Source); }
    uint32_t minFrame = 0, maxFrame = 0;
    bool writeFailed = false, encodeFailed = false;
    auto takePackets = [&]() {
        for (;;) {
            const int r = avcodec_receive_packet(e.ctx, e.pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return;
            if (r < 0) { encodeFailed = true; return; }
            if (e.pkt->size > 0) {
                const uint32_t n = (uint32_t)e.pkt->size;
                if (!put(e.pkt->data, n)) writeFailed = true;
                if (!minFrame || n < minFrame) minFrame = n;
                if (n > maxFrame) maxFrame = n;
            }
            av_packet_unref(e.pkt);
            if (writeFailed) return;
        }
    };
    std::vector<int32_t> pending;                             // 还没交给编码器的采样(交错,新位深、右对齐)
    uint64_t sent = 0;
    auto sendBlock = [&](size_t frames) {                     // pending 开头的 frames 帧交给编码器
        e.frame->format = encFmt;
        e.frame->nb_samples = (int)frames;
        e.frame->sample_rate = res.rate;
        av_channel_layout_default(&e.frame->ch_layout, 2);
        e.frame->pts = (int64_t)sent;
        if (av_frame_get_buffer(e.frame, 0) < 0) { encodeFailed = true; return; }
        if (encFmt == AV_SAMPLE_FMT_S16) {
            int16_t* dst = (int16_t*)e.frame->data[0];
            for (size_t i = 0; i < frames * 2; ++i) dst[i] = (int16_t)pending[i];
        } else {
            int32_t* dst = (int32_t*)e.frame->data[0];       // 编码器要左对齐的 32 位:24 bit 的值再左移 8 位
            for (size_t i = 0; i < frames * 2; ++i) dst[i] = (int32_t)((uint32_t)pending[i] << (32 - spec.outBits));
        }
        if (avcodec_send_frame(e.ctx, e.frame) < 0) encodeFailed = true;
        av_frame_unref(e.frame);
        sent += frames;
        pending.erase(pending.begin(), pending.begin() + (std::ptrdiff_t)(frames * 2));
        if (!encodeFailed) takePackets();
    };
    std::vector<int32_t> buf;
    std::vector<uint8_t> rawSrc, rawOut;
    const int srcBytes = res.srcBits / 8, outBytes = spec.outBits / 8;
    auto takeFrames = [&]() {
        for (;;) {
            const int r = avcodec_receive_frame(d.dec, e.inFrame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) { ++res.decodeErrors; break; }
            frameToInt32(e.inFrame, buf);
            md5Src.update(buf.data(), buf.size() * sizeof(int32_t));
            rawSrc.clear();
            rawOut.clear();
            for (int32_t v : buf) {
                const int32_t s = v >> (32 - res.srcBits);                        // 源里的那个数(右对齐)
                const int32_t o = (int32_t)((uint32_t)s << spec.padBits);         // 新文件里的数:位深不变时就是 s;补位时 = s × 2^padBits
                for (int b = 0; b < srcBytes; ++b) rawSrc.push_back((uint8_t)((uint32_t)s >> (8 * b)));
                for (int b = 0; b < outBytes; ++b) rawOut.push_back((uint8_t)((uint32_t)o >> (8 * b)));
                pending.push_back(o);
            }
            md5SrcNative.update(rawSrc.data(), rawSrc.size());
            md5Out.update(rawOut.data(), rawOut.size());
            res.frames += (uint64_t)e.inFrame->nb_samples;
            av_frame_unref(e.inFrame);
        }
        while (!encodeFailed && !writeFailed && pending.size() >= block * 2) sendBlock(block);
    };
    bool cancelled = false;
    while (!encodeFailed && !writeFailed && av_read_frame(d.fmt, e.inPkt) >= 0) {
        if (e.inPkt->stream_index == d.stream) {
            if (avcodec_send_packet(d.dec, e.inPkt) < 0) ++res.decodeErrors;
            takeFrames();
        }
        av_packet_unref(e.inPkt);
        if (watch && watch->tick && !watch->tick(d.io.size > 0 ? (double)d.io.pos / (double)d.io.size : 1.0)) {
            cancelled = true;
            break;
        }
    }
    if (cancelled) return failed(WriteFail::Cancelled);
    if (!encodeFailed && !writeFailed) {
        avcodec_send_packet(d.dec, nullptr);
        takeFrames();
    }
    if (!encodeFailed && !writeFailed && !pending.empty()) sendBlock(pending.size() / 2);      // 最后不满一块的
    if (!encodeFailed && !writeFailed) {
        if (avcodec_send_frame(e.ctx, nullptr) < 0) encodeFailed = true;
        else takePackets();
    }
    if (writeFailed) return failed(WriteFail::Write);
    if (encodeFailed || sent != res.frames) return failed(WriteFail::Encode);

    res.srcMd5 = md5Src.hex();
    res.srcNativeMd5 = md5SrcNative.hex();
    if (res.srcMd5.empty() || res.srcNativeMd5.empty() || !md5Out.raw(res.outMd5, 16)) { res.sourceError = msg::of(msg::ProbeNoMd5); return failed(WriteFail::Source); }
    flacmeta::StreamInfoFields si;
    si.minBlock = si.maxBlock = (int)block;
    si.minFrame = minFrame;
    si.maxFrame = maxFrame;
    si.sampleRate = res.rate;
    si.channels = 2;
    si.bits = spec.outBits;
    si.totalSamples = res.frames;
    std::memcpy(si.md5, res.outMd5, 16);
    uint8_t body[34];
    flacmeta::buildStreamInfo(si, body);
    if (!wu::writeAt(out, 8, body, 34)) {
        res.winError = GetLastError();
        return failed(WriteFail::Write);
    }
    return true;
}

} // namespace probe
