package com.omni.tagrepair.core;

/**
 * FLAC 音频帧解码(本工具自己写的,不用任何别人的代码)。按 FLAC 格式规范(RFC 9639)逐帧解:
 * 帧头(同步码、块长、采样率、声道安排、位深、帧号)+ CRC-8;每个声道一个子帧(CONSTANT / VERBATIM / FIXED / LPC,
 * 残差是 Rice 编码,支持 escape 分区;wasted bits);声道去相关(左/差、右/差、中/差);字节对齐;CRC-16。
 * 严格:帧头或帧尾 CRC 不对、字段不合法、和 STREAMINFO 对不上的帧算一个错,不出样本,从下一个能对上的帧头接着解。
 *
 * 输出:每帧交错的 int32,左对齐(样本 << (32 − 位深))—— 与 Windows 版经 FFmpeg 解出、再转成左对齐 int32 的样本一模一样
 * (这一点用同一批文件对照核对过:两边整轨样本的 MD5 相同)。
 */
public final class FlacDecoder {
    public interface Sink {
        /** 一帧解出来了:lr 交错、左对齐,共 frames 帧 × channels 声道。返回 false = 停 */
        boolean frame(int[] lr, int frames, int channels);
    }

    public interface Tick {
        /** 读到文件的哪了(0..1)。返回 false = 取消 */
        boolean at(double fraction);
    }

    public static final class Result {
        public long frames;          // 每声道的采样数
        public int errors;           // 坏帧 / 解不了的段数
        public boolean cancelled;
        public boolean ioError;
    }

    private final Rio src;
    private final long end;
    private final int siRate, siChannels, siBits, siMaxBlock;

    // ---- 读缓冲 ----
    private static final int BUF = 4 << 20, KEEP_AHEAD = 3 << 20;
    private final byte[] buf = new byte[BUF];
    private long bufAt = 0;          // buf[0] 在文件里的位置
    private int bufLen = 0;

    // 位读取(在 buf 里)
    private int bytePos;             // 下一个要读的字节在 buf 里的下标
    private long cache;              // 还没用掉的位,左对齐
    private int cacheBits;
    private boolean overrun;         // 不再使用(保留给 headerOk 的保存 / 恢复)

    /** 已经用掉的位超出了缓冲里的有效字节 */
    private boolean overran() { return (long) bytePos * 8 - cacheBits > (long) bufLen * 8; }

    /** layout:readLayout 走通的那份(从 audioAt 解到文件末尾) */
    public FlacDecoder(Rio src, FlacMeta.Layout layout) {
        this.src = src;
        this.end = layout.fileSize;
        this.bufAt = layout.audioAt;
        this.siRate = layout.sampleRate;
        this.siChannels = layout.channels;
        this.siBits = layout.bits;
        int mb = ((layout.streamInfo[2] & 0xFF) << 8) | (layout.streamInfo[3] & 0xFF);
        this.siMaxBlock = mb;
    }

    private boolean fill(long from) {
        long n = Math.min(BUF, end - from);
        if (n < 0) n = 0;
        if (n > 0 && !src.read(from, buf, 0, (int) n)) return false;
        bufAt = from;
        bufLen = (int) n;
        return true;
    }

    /** 从头解到尾。sink 可以是 null(只数采样、核对帧) */
    public Result decodeAll(Sink sink, Tick tick) {
        Result r = new Result();
        long pos = bufAt;
        if (!fill(pos)) { r.ioError = true; return r; }
        int[] out = new int[0];
        long[][] ch = new long[8][];
        int lastTickPermille = -1;
        while (pos < end) {
            // 前面留够(最大的帧也装得下),不够就从 pos 起重新装
            if (pos - bufAt + KEEP_AHEAD > bufLen && bufAt + bufLen < end) {
                if (!fill(pos)) { r.ioError = true; return r; }
            }
            int at = (int) (pos - bufAt);
            if (bufLen - at < 2) { r.errors++; break; }       // 末尾只剩 1 个字节:不是帧,算一处错
            int got = decodeFrame(at, ch);
            if (got > 0) {
                int n = blockSize, c = channels;
                if (out.length < n * c) out = new int[n * c];
                int shift = 32 - bits;
                for (int i = 0; i < n; i++)
                    for (int k = 0; k < c; k++) out[i * c + k] = (int) (ch[k][i] << shift);
                r.frames += n;
                pos += got;
                if (sink != null && !sink.frame(out, n, c)) { r.cancelled = true; return r; }
            } else {
                // 坏帧(或者帧之间、末尾的杂字节):算一个错,跳到下一个对得上的帧头
                r.errors++;
                long next = resync(pos + 1);
                if (next < 0) break;
                pos = next;
            }
            if (tick != null) {
                int pm = (int) ((pos - 0) * 1000 / Math.max(1, end));
                if (pm != lastTickPermille) {
                    lastTickPermille = pm;
                    if (!tick.at(pm / 1000.0)) { r.cancelled = true; return r; }
                }
            }
        }
        return r;
    }

    /** 从 from 起找下一个帧头对得上(同步码 + 字段合法 + CRC-8)的位置;找不到返回 −1 */
    private long resync(long from) {
        long p = from;
        while (p + 1 < end) {
            if (p - bufAt + 64 >= bufLen && bufAt + bufLen < end) {
                if (!fill(p)) return -1;
            }
            if (p < bufAt) { if (!fill(p)) return -1; }
            int at = (int) (p - bufAt);
            boolean atEof = bufAt + bufLen >= end;
            int lim = atEof ? bufLen - 1 : bufLen - 64;                // 没到文件末尾时,离缓冲末尾 64 字节之内的留给下一轮(帧头要读得全)
            for (; at < lim; at++) {
                if ((buf[at] & 0xFF) == 0xFF && ((buf[at + 1] & 0xFE) == 0xF8)) {
                    if (headerOk(at)) return bufAt + at;
                }
            }
            long next = bufAt + lim;
            if (bufAt + bufLen >= end) return -1;
            p = next;
        }
        return -1;
    }

    // ---- 一帧 ----
    private int blockSize, channels, bits, sampleRate;

    private void startBits(int at) {
        bytePos = at;
        cache = 0;
        cacheBits = 0;
        overrun = false;
    }

    private void refill() {
        while (cacheBits <= 56) {
            int b;
            if (bytePos < bufLen) b = buf[bytePos] & 0xFF;
            else b = 0;
            bytePos++;
            cache |= ((long) b) << (56 - cacheBits);
            cacheBits += 8;
        }
    }

    private int readBits(int n) {                 // n ≤ 32
        if (n == 0) return 0;
        if (cacheBits < n) refill();
        int v = (int) (cache >>> (64 - n));
        cache <<= n;
        cacheBits -= n;
        return v;
    }

    private int readSigned(int n) {
        if (n == 0) return 0;
        int v = readBits(n);
        return (v << (32 - n)) >> (32 - n);
    }

    private long readSigned33(int n) {            // n 可以到 33(32 bit 样本的差声道)
        if (n <= 32) {
            if (n == 32) return readBits(32);
            return readSigned(n);
        }
        long hi = readBits(n - 32) & 0xFFFFFFFFL;
        long lo = readBits(32) & 0xFFFFFFFFL;
        long v = (hi << 32) | lo;
        return (v << (64 - n)) >> (64 - n);
    }

    private int readUnary() {
        int n = 0;
        while (true) {
            if (cacheBits == 0) refill();
            if (cache == 0) {
                n += cacheBits;
                cacheBits = 0;
                cache = 0;
                if (overran()) return -1;
                continue;
            }
            int z = Long.numberOfLeadingZeros(cache);
            if (z < cacheBits) {
                n += z;
                cache = z + 1 >= 64 ? 0 : cache << (z + 1);      // Java 的移位按 64 取模:移 64 位要写成清零
                cacheBits -= z + 1;
                return n;
            }
            n += cacheBits;
            cacheBits = 0;
            cache = 0;
            if (overran()) return -1;
        }
    }

    /** 已经读了多少位(从 startBits 起算的字节位置) */
    private int bytesConsumed(int start) {
        int bitsRead = (bytePos - start) * 8 - cacheBits;
        return (bitsRead + 7) / 8;
    }

    private void alignToByte(int start) {
        int bitsRead = (bytePos - start) * 8 - cacheBits;
        int rem = bitsRead % 8;
        if (rem != 0) readBits(8 - rem);
    }

    private static final int[] RATE_CODES = { 0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
    private static final int[] BITS_CODES = { 0, 8, 12, 0, 16, 20, 24, 32 };

    /** 帧头的字段与 CRC-8 对得上吗(不动解码状态之外的东西;resync 用) */
    private boolean headerOk(int at) {
        int save = bytePos;
        long sc = cache;
        int sb = cacheBits;
        boolean so = overrun;
        boolean ok = parseHeader(at) > 0;
        bytePos = save;
        cache = sc;
        cacheBits = sb;
        overrun = so;
        return ok;
    }

    /** 解帧头,成功返回帧头长度(字节),否则 −1。成功时 blockSize / channels / bits / sampleRate / chanAssign 是这一帧的 */
    private int chanAssign;

    private int parseHeader(int at) {
        startBits(at);
        int sync = readBits(15);
        if (sync != 0x7FFC) return -1;                 // 1111 1111 1111 100
        readBits(1);                                    // 块长策略(固定 / 可变):这里不用
        int bsCode = readBits(4), srCode = readBits(4), chCode = readBits(4), bpsCode = readBits(3);
        if (readBits(1) != 0) return -1;
        // 帧号 / 采样号:UTF-8 式的变长数
        int first = readBits(8);
        int more;
        if ((first & 0x80) == 0) more = 0;
        else if ((first & 0xE0) == 0xC0) more = 1;
        else if ((first & 0xF0) == 0xE0) more = 2;
        else if ((first & 0xF8) == 0xF0) more = 3;
        else if ((first & 0xFC) == 0xF8) more = 4;
        else if ((first & 0xFE) == 0xFC) more = 5;
        else if (first == 0xFE) more = 6;
        else return -1;
        for (int i = 0; i < more; i++) if ((readBits(8) & 0xC0) != 0x80) return -1;
        int bs;
        if (bsCode == 0) return -1;
        else if (bsCode == 1) bs = 192;
        else if (bsCode <= 5) bs = 576 << (bsCode - 2);
        else if (bsCode == 6) bs = readBits(8) + 1;
        else if (bsCode == 7) bs = readBits(16) + 1;
        else bs = 256 << (bsCode - 8);
        int sr;
        if (srCode == 0) sr = siRate;
        else if (srCode <= 11) sr = RATE_CODES[srCode];
        else if (srCode == 12) sr = readBits(8) * 1000;
        else if (srCode == 13) sr = readBits(16);
        else if (srCode == 14) sr = readBits(16) * 10;
        else return -1;
        if (chCode > 10) return -1;
        int ch = chCode <= 7 ? chCode + 1 : 2;
        int bps;
        if (bpsCode == 0) bps = siBits;
        else if (bpsCode == 3) return -1;
        else bps = BITS_CODES[bpsCode];
        int hlen = bytesConsumed(at);
        if (overran()) return -1;
        int crc = readBits(8);
        if (overran()) return -1;
        if (Crc.crc8(buf, at, hlen) != crc) return -1;
        // 与 STREAMINFO 对不上的(声道数、位深、超过最大块长):不认
        if (ch != siChannels || bps != siBits || sr <= 0) return -1;
        if (siMaxBlock > 0 && bs > siMaxBlock) return -1;
        blockSize = bs;
        channels = ch;
        bits = bps;
        sampleRate = sr;
        chanAssign = chCode;
        return hlen + 1;
    }

    /** 解一帧(从 buf[at] 起);成功返回整帧字节数,样本进 ch[0..channels),否则 −1 */
    private int decodeFrame(int at, long[][] ch) {
        int hlen = parseHeader(at);
        if (hlen < 0) return -1;
        int n = blockSize;
        for (int c = 0; c < channels; c++) {
            if (ch[c] == null || ch[c].length < n) ch[c] = new long[Math.max(n, 4608)];
            int sbits = bits;
            if ((chanAssign == 8 && c == 1) || (chanAssign == 9 && c == 0) || (chanAssign == 10 && c == 1)) sbits++;
            if (!subframe(ch[c], n, sbits)) return -1;
            if (overran()) return -1;
        }
        alignToByte(at);
        int len = bytesConsumed(at);
        if (overran()) return -1;
        int crc = readBits(16);
        if (overran()) return -1;
        if (Crc.crc16(buf, at, len) != crc) return -1;
        // 声道去相关
        long[] a = ch[0], b = channels > 1 ? ch[1] : null;
        switch (chanAssign) {
        case 8:  for (int i = 0; i < n; i++) b[i] = a[i] - b[i]; break;                         // 左 / 差
        case 9:  for (int i = 0; i < n; i++) a[i] = a[i] + b[i]; break;                         // 差 / 右
        case 10:                                                                                 // 中 / 差
            for (int i = 0; i < n; i++) {
                long side = b[i];
                long mid = (a[i] << 1) | (side & 1);
                a[i] = (mid + side) >> 1;
                b[i] = (mid - side) >> 1;
            }
            break;
        default: break;
        }
        return len + 2;
    }

    private boolean subframe(long[] out, int n, int sbits) {
        if (readBits(1) != 0) return false;
        int type = readBits(6);
        int wasted = 0;
        if (readBits(1) != 0) {
            int u = readUnary();
            if (u < 0) return false;
            wasted = u + 1;
            if (wasted >= sbits) return false;
            sbits -= wasted;
        }
        if (type == 0) {                                       // CONSTANT
            long v = readSigned33(sbits);
            for (int i = 0; i < n; i++) out[i] = v;
        } else if (type == 1) {                                // VERBATIM
            for (int i = 0; i < n; i++) out[i] = readSigned33(sbits);
        } else if (type >= 8 && type <= 12) {                  // FIXED
            int order = type - 8;
            if (order > n) return false;
            for (int i = 0; i < order; i++) out[i] = readSigned33(sbits);
            if (!residual(out, n, order)) return false;
            fixedRestore(out, n, order);
        } else if (type >= 32) {                               // LPC
            int order = (type & 31) + 1;
            if (order > n) return false;
            for (int i = 0; i < order; i++) out[i] = readSigned33(sbits);
            int prec = readBits(4);
            if (prec == 15) return false;
            prec += 1;
            int shift = readSigned(5);
            if (shift < 0) return false;
            long[] coef = new long[order];
            for (int i = 0; i < order; i++) coef[i] = readSigned(prec);
            if (!residual(out, n, order)) return false;
            for (int i = order; i < n; i++) {
                long sum = 0;
                for (int j = 0; j < order; j++) sum += coef[j] * out[i - 1 - j];
                out[i] += sum >> shift;
            }
        } else {
            return false;
        }
        if (wasted > 0) for (int i = 0; i < n; i++) out[i] <<= wasted;
        return !overran();
    }

    private static void fixedRestore(long[] x, int n, int order) {
        switch (order) {
        case 0: break;
        case 1: for (int i = 1; i < n; i++) x[i] += x[i - 1]; break;
        case 2: for (int i = 2; i < n; i++) x[i] += 2 * x[i - 1] - x[i - 2]; break;
        case 3: for (int i = 3; i < n; i++) x[i] += 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3]; break;
        case 4: for (int i = 4; i < n; i++) x[i] += 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4]; break;
        default: break;
        }
    }

    /** 残差读进 out[order..n)(之后由预测加回去) */
    private boolean residual(long[] out, int n, int order) {
        int method = readBits(2);
        if (method > 1) return false;
        int paramBits = method == 0 ? 4 : 5, escape = method == 0 ? 15 : 31;
        int porder = readBits(4);
        int parts = 1 << porder;
        if ((n & (parts - 1)) != 0) return false;
        int per = n >> porder;
        if (per < order) return false;
        int i = order;
        for (int p = 0; p < parts; p++) {
            int cnt = p == 0 ? per - order : per;
            int k = readBits(paramBits);
            if (k == escape) {
                int raw = readBits(5);
                for (int j = 0; j < cnt; j++) out[i++] = raw == 0 ? 0 : readSigned(raw);
            } else {
                for (int j = 0; j < cnt; j++) {
                    int q = readUnary();
                    if (q < 0) return false;
                    long u = ((long) q << k) | (k == 0 ? 0 : (readBits(k) & 0xFFFFFFFFL));
                    out[i++] = (u >>> 1) ^ -(u & 1);
                }
            }
            if (overran()) return false;
        }
        return true;
    }

    public int lastSampleRate() { return sampleRate; }
}
