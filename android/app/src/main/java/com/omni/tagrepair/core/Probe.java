package com.omni.tagrepair.core;

import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

/**
 * 看一个文件:容器 / 编码 / 音频数据里有没有同步标记 / 三个标签 / 整轨样本的 MD5。对应 Windows 版的 src/probe.cpp,
 * 那边借 FFmpeg,这里是本工具自己的 FlacDecoder / WavReader(安卓版只读 FLAC 与 WAV)。判据相同:
 *   无损 PCM、两声道、整数样本 ≥ 16 bit、44.1 k 或 48 k 才去找同步标记;从头解最多 10 秒,找到标记且读到原始采样率就停。
 */
public final class Probe {
    private Probe() {}

    public static final class Info {
        public boolean opened;
        public Msg.Text error = Msg.EMPTY;
        public String container = "";        // "flac" / "wav"
        public String codec = "";
        public boolean losslessPcm;
        public int rate, channels, bits;
        public boolean sniffed, marker;
        public int origRate;
        public final boolean[] has = new boolean[TagRule.KEYS];
        public final String[] value = { "", "", "" };
        public final List<String[]> meta = new ArrayList<String[]>();   // WAV 的元数据字典(字节串)
        // 解码要用的
        public FlacMeta.Layout layout;       // 原生 FLAC 走得通时
        public WavReader wav;
    }

    public interface Watch {
        /** 做到哪了(0..1);返回 false = 取消 */
        boolean tick(double f);
    }

    public interface Sink {
        void samples(int[] lr, int count);   // count = 各声道合计的个数(交错、左对齐)
    }

    /** 安卓版整段读进内存的元数据区上限(Windows 版是 512 MB;手机的内存小得多) */
    public static final long ANDROID_MAX_META = 64L << 20;

    public static Info inspect(Rio r) {
        Info out = new Info();
        FlacMeta.Layout lay = new FlacMeta.Layout();
        FlacMeta.LayoutResult lr = FlacMeta.readLayout(r, lay);
        if (lr.err != FlacMeta.Err.NOT_FLAC) {
            out.container = "flac";
            out.codec = "flac";
            if (lr.err != FlacMeta.Err.OK) {
                out.error = Msg.of(lr.why != null ? lr.why : Msg.LayNotFlac);
                return out;
            }
            if (lay.audioAt - lay.metaAt > ANDROID_MAX_META) {            // 整段读进内存会把手机的内存撑爆:不处理
                out.error = Msg.of(Msg.AMetaTooBig);
                return out;
            }
            out.layout = lay;
            out.opened = true;
            out.losslessPcm = true;
            out.rate = lay.sampleRate;
            out.channels = lay.channels;
            out.bits = lay.bits;
        } else if (WavReader.looksLikeWav(r)) {
            out.container = "wav";
            WavReader w = WavReader.open(r);
            if (!w.ok) {
                out.error = w.error;
                return out;
            }
            out.wav = w;
            out.opened = true;
            out.codec = w.codec;
            out.losslessPcm = true;                  // PCM 一律算无损(FFmpeg 的编码描述也是这样标的)
            out.rate = w.rate;
            out.channels = w.channels;
            out.bits = w.intBits();
            out.meta.addAll(w.meta);
            for (int k = 0; k < TagRule.KEYS; k++)
                for (String[] kv : w.meta)
                    if (TagRule.keyEquals(kv[0], TagRule.keyName(k))) {
                        out.has[k] = true;
                        out.value[k] = kv[1];
                        break;
                    }
        } else {
            out.error = Msg.of(Msg.AOnlyFlacWav);
            return out;
        }
        if (!(out.channels == 2 && out.bits >= 16 && (out.rate == 44100 || out.rate == 48000))) return out;
        out.sniffed = true;
        final SyncDetect det = new SyncDetect();
        final long limit = (long) out.rate * 10;
        if (out.layout != null) {
            new FlacDecoder(r, out.layout).decodeAll(new FlacDecoder.Sink() {
                public boolean frame(int[] lr2, int frames, int channels) {
                    if (det.framesSeen() >= limit) return false;
                    det.feed(lr2, 0, frames);
                    return !(det.markerFound() && det.originalRate() > 0);
                }
            }, null);
        } else {
            WavReader w = out.wav;
            final int per = 4096 / w.blockAlign;         // 一包 4096 字节左右(FFmpeg 读 WAV 的包)
            int[] buf = new int[per * 2];
            byte[] raw = new byte[per * w.blockAlign];
            long total = w.frames();
            for (long f = 0; f < total && det.framesSeen() < limit; f += per) {
                int n = (int) Math.min(per, total - f);
                if (!w.readFrames(r, f, n, buf, raw)) break;
                det.feed(buf, 0, n);
                if (det.markerFound() && det.originalRate() > 0) break;
            }
        }
        out.marker = det.markerFound();
        out.origRate = det.originalRate();
        return out;
    }

    public static final class Pcm {
        public String md5 = "";
        public long frames;
        public int errors;
        public boolean cancelled;
        public boolean ioError;
    }

    /** 整轨解一遍:所有样本(交错、左对齐 int32、小端)的 MD5、每声道采样数、坏帧数。info 是 inspect 的结果 */
    public static Pcm decodeAll(Rio r, Info info, final Watch watch, final Sink sink) {
        final Pcm out = new Pcm();
        final MessageDigest md5 = Crc.md("MD5");
        final byte[] scratch = new byte[1 << 16];
        if (info.layout != null) {
            FlacDecoder.Result res = new FlacDecoder(r, info.layout).decodeAll(new FlacDecoder.Sink() {
                public boolean frame(int[] lr, int frames, int channels) {
                    int n = frames * channels;
                    Crc.updateInt32(md5, lr, n, scratch);
                    if (sink != null) sink.samples(lr, n);
                    return true;
                }
            }, watch == null ? null : new FlacDecoder.Tick() {
                public boolean at(double f) { return watch.tick(f); }
            });
            out.frames = res.frames;
            out.errors = res.errors;
            out.cancelled = res.cancelled;
            out.ioError = res.ioError;
        } else if (info.wav != null && !info.wav.isFloat) {
            WavReader w = info.wav;
            int per = Math.max(1, 65536 / w.blockAlign);
            int[] buf = new int[per * w.channels];
            byte[] raw = new byte[per * w.blockAlign];
            long total = w.frames();
            for (long f = 0; f < total; f += per) {
                int n = (int) Math.min(per, total - f);
                if (!w.readFrames(r, f, n, buf, raw)) { out.ioError = true; break; }
                Crc.updateInt32(md5, buf, n * w.channels, scratch);
                if (sink != null) sink.samples(buf, n * w.channels);
                out.frames += n;
                if (watch != null && !watch.tick((double) (f + n) / total)) { out.cancelled = true; break; }
            }
        } else {
            out.ioError = true;
        }
        out.md5 = Crc.hex(md5.digest());
        return out;
    }

    // ---- 另存:源的全部采样 → 一个新的 FLAC(写进 out,一个空的、可写的文件)----
    public enum WriteFail { NONE, SOURCE, FORMAT, ENCODE, WRITE, CANCELLED }

    public static final class FlacWritten {
        public WriteFail fail = WriteFail.NONE;
        public long frames;
        public int decodeErrors;
        public int srcBits, rate, channels;
        public String srcMd5 = "", srcNativeMd5 = "";
        public final byte[] outMd5 = new byte[16];
    }

    /**
     * outBits:16 或 24;padBits:0 = 位深不变,8 = 16 bit 的采样放进 24 bit 的格子(每个采样 × 2^8,低 8 位补零,不重新计算、不加抖动)。
     * metaAfterStreamInfo:已经拼好的块(最后一块带"最后一块"标志)。
     */
    public static boolean writeFlacCopy(Rio src, Info info, Rio out, int outBits, final int padBits, byte[] metaAfterStreamInfo, final FlacWritten res,
                                        Watch watch) {
        res.srcBits = info.bits;
        res.rate = info.rate;
        res.channels = info.channels;
        if (res.channels != 2 || res.rate <= 0 || (res.srcBits != 16 && res.srcBits != 24) || (outBits != 16 && outBits != 24) || padBits < 0
                || outBits != res.srcBits + padBits || metaAfterStreamInfo.length == 0) {
            res.fail = WriteFail.FORMAT;
            return false;
        }
        byte[] head = new byte[4 + 4 + 34];
        head[0] = 'f'; head[1] = 'L'; head[2] = 'a'; head[3] = 'C';
        head[4] = FlacMeta.STREAMINFO; head[5] = 0; head[6] = 0; head[7] = 34;
        final long[] at = { 0 };
        if (!out.write(0, head, 0, head.length) || !out.write(head.length, metaAfterStreamInfo, 0, metaAfterStreamInfo.length)) {
            res.fail = WriteFail.WRITE;
            return false;
        }
        at[0] = head.length + metaAfterStreamInfo.length;
        final MessageDigest md5Src = Crc.md("MD5"), md5SrcNative = Crc.md("MD5"), md5Out = Crc.md("MD5");
        final FlacEncoder enc = new FlacEncoder(res.rate, 2, outBits);
        final int block = FlacEncoder.BLOCK;
        final int[][] pend = { new int[block], new int[block] };
        final int[] pendN = { 0 };
        final int[] minMax = { 0, 0 };
        final boolean[] failed = { false };
        final Rio outF = out;
        final int srcBytes = res.srcBits / 8, outBytes = outBits / 8, srcShift = 32 - res.srcBits;
        final byte[] scratch = new byte[1 << 16];
        final Runnable flush = new Runnable() {
            public void run() {
                byte[] fr = enc.encode(pend, pendN[0]);
                int len = enc.lastLength();
                if (!outF.write(at[0], fr, 0, len)) { failed[0] = true; return; }
                at[0] += len;
                if (minMax[0] == 0 || len < minMax[0]) minMax[0] = len;
                if (len > minMax[1]) minMax[1] = len;
                pendN[0] = 0;
            }
        };
        final byte[] rawS = new byte[1 << 16], rawO = new byte[1 << 16];
        Sink sink = new Sink() {
            public void samples(int[] lr, int count) {
                if (failed[0]) return;
                Crc.updateInt32(md5Src, lr, count, scratch);
                int ps = 0, po = 0;
                for (int i = 0; i < count; i++) {
                    int s = lr[i] >> srcShift;                 // 源里的那个数(右对齐)
                    int o = s << padBits;                      // 新文件里的数
                    for (int b = 0; b < srcBytes; b++) rawS[ps++] = (byte) (s >>> (8 * b));
                    for (int b = 0; b < outBytes; b++) rawO[po++] = (byte) (o >>> (8 * b));
                    if (ps + 8 > rawS.length) { md5SrcNative.update(rawS, 0, ps); ps = 0; }
                    if (po + 8 > rawO.length) { md5Out.update(rawO, 0, po); po = 0; }
                    pend[i & 1][pendN[0]] = o;
                    if ((i & 1) == 1) {
                        pendN[0]++;
                        res.frames++;
                        if (pendN[0] == block) {
                            flush.run();
                            if (failed[0]) return;
                        }
                    }
                }
                md5SrcNative.update(rawS, 0, ps);
                md5Out.update(rawO, 0, po);
            }
        };
        Pcm p = decodeAll(src, info, watch, sink);
        if (p.cancelled) { res.fail = WriteFail.CANCELLED; return false; }
        if (failed[0]) { res.fail = WriteFail.WRITE; return false; }
        if (p.ioError) { res.fail = WriteFail.SOURCE; return false; }
        res.decodeErrors = p.errors;
        if (pendN[0] > 0) flush.run();
        if (failed[0]) { res.fail = WriteFail.WRITE; return false; }
        res.srcMd5 = p.md5;
        res.srcNativeMd5 = Crc.hex(md5SrcNative.digest());
        System.arraycopy(md5Out.digest(), 0, res.outMd5, 0, 16);
        FlacMeta.StreamInfoFields si = new FlacMeta.StreamInfoFields();
        si.minBlock = si.maxBlock = block;
        si.minFrame = minMax[0];
        si.maxFrame = minMax[1];
        si.sampleRate = res.rate;
        si.channels = 2;
        si.bits = outBits;
        si.totalSamples = res.frames;
        System.arraycopy(res.outMd5, 0, si.md5, 0, 16);
        byte[] body = FlacMeta.buildStreamInfo(si);
        if (!out.write(8, body, 0, 34)) { res.fail = WriteFail.WRITE; return false; }
        return true;
    }
}
