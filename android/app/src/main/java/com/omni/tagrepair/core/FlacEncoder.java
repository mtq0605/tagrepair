package com.omni.tagrepair.core;

/**
 * FLAC 音频帧编码(本工具自己写的,只给"另存"用)。无损:每个子帧在 CONSTANT / VERBATIM / FIXED 0~4 阶里挑最省的,
 * 残差用 Rice 编码(分区阶数 0~8 挑最省的);两声道在 独立 / 左差 / 右差 / 中差 里挑最省的;有 wasted bits 的(比如 16 bit 放进
 * 24 bit 格子、低 8 位全是零)按规范标出来。编出的每一帧都由 FlacDecoder 解回来核对(另存的核对一节),所以只要它能编、
 * 解回来逐个样本相同就行;不追求压缩率。
 * 固定块长 BLOCK(最后一块可以短);帧头写采样率码(标准的那几个)或"见 STREAMINFO"。
 */
public final class FlacEncoder {
    public static final int BLOCK = 4096;
    /** 自检用:残差一律用 Rice 参数 0(一元码很长,专门考解码器读长一元码的那段) */
    static boolean testForceK0;
    /** 自检用:残差分区一律写成 escape(原样存的位数),考解码器的这一支;另记每种声道安排用了几次 */
    static boolean testEscape;
    static final int[] assignCount = new int[11];

    private final int rate, channels, bits;
    private long frameNo = 0;
    private final BitWriter w = new BitWriter(1 << 16);

    public FlacEncoder(int rate, int channels, int bits) {
        this.rate = rate;
        this.channels = channels;
        this.bits = bits;
    }

    /** 编一帧:x[c][0..n) 是右对齐的样本。返回这一帧的字节(只在下一次调用之前有效),长度在 lastLength() */
    public byte[] encode(int[][] x, int n) {
        w.reset();
        // 声道安排:两声道时在四种里挑
        int assign = channels - 1;
        long[][] sig;
        if (channels == 2) {
            long[] l = new long[n], r = new long[n], s = new long[n], m = new long[n];
            for (int i = 0; i < n; i++) {
                l[i] = x[0][i];
                r[i] = x[1][i];
                s[i] = l[i] - r[i];
                m[i] = (l[i] + r[i]) >> 1;
            }
            long cl = estimate(l, n, bits), cr = estimate(r, n, bits), cs = estimate(s, n, bits + 1), cm = estimate(m, n, bits);
            long ind = cl + cr, ls = cl + cs, rs = cr + cs, ms = cm + cs;
            long best = Math.min(Math.min(ind, ls), Math.min(rs, ms));
            if (best == ind) { assign = 1; sig = new long[][] { l, r }; }
            else if (best == ls) { assign = 8; sig = new long[][] { l, s }; }
            else if (best == rs) { assign = 9; sig = new long[][] { s, r }; }
            else { assign = 10; sig = new long[][] { m, s }; }
        } else {
            sig = new long[channels][];
            for (int c = 0; c < channels; c++) {
                sig[c] = new long[n];
                for (int i = 0; i < n; i++) sig[c][i] = x[c][i];
            }
        }
        assignCount[assign]++;
        header(n, assign);
        for (int c = 0; c < sig.length; c++) {
            int sb = bits;
            if ((assign == 8 && c == 1) || (assign == 9 && c == 0) || (assign == 10 && c == 1)) sb++;
            subframe(sig[c], n, sb);
        }
        w.alignZero();
        int crc = Crc.crc16(w.buf, 0, w.length());
        w.put(crc, 16);
        frameNo++;
        return w.buf;
    }

    public int lastLength() { return w.length(); }

    private static int rateCode(int hz) {
        switch (hz) {
        case 88200: return 1;
        case 176400: return 2;
        case 192000: return 3;
        case 8000: return 4;
        case 16000: return 5;
        case 22050: return 6;
        case 24000: return 7;
        case 32000: return 8;
        case 44100: return 9;
        case 48000: return 10;
        case 96000: return 11;
        default: return 0;
        }
    }

    private void header(int n, int assign) {
        w.put(0x3FFE, 14);
        w.put(0, 1);
        w.put(0, 1);                                   // 固定块长:后面是帧号
        int bsCode, extra = 0, extraBits = 0;
        if (n == 4096) bsCode = 12;
        else if (n <= 256) { bsCode = 6; extra = n - 1; extraBits = 8; }
        else { bsCode = 7; extra = n - 1; extraBits = 16; }
        w.put(bsCode, 4);
        w.put(rateCode(rate), 4);
        w.put(assign, 4);
        int bpsCode;
        switch (bits) {
        case 8: bpsCode = 1; break;
        case 12: bpsCode = 2; break;
        case 16: bpsCode = 4; break;
        case 20: bpsCode = 5; break;
        case 24: bpsCode = 6; break;
        case 32: bpsCode = 7; break;
        default: bpsCode = 0; break;
        }
        w.put(bpsCode, 3);
        w.put(0, 1);
        utf8(frameNo);
        if (extraBits > 0) w.put(extra, extraBits);
        int crc = Crc.crc8(w.buf, 0, w.length());
        w.put(crc, 8);
    }

    private void utf8(long v) {
        if (v < 0x80) { w.put((int) v, 8); return; }
        int bytes = v < 0x800 ? 2 : v < 0x10000 ? 3 : v < 0x200000 ? 4 : v < 0x4000000 ? 5 : v < 0x80000000L ? 6 : 7;
        int lead = bytes == 7 ? 0xFE : (0xFF00 >> bytes) & 0xFF;
        int shift = (bytes - 1) * 6;
        w.put(lead | (int) (v >>> shift) & (bytes == 7 ? 0 : (0xFF >> (bytes + 1))), 8);
        for (int i = bytes - 2; i >= 0; i--) w.put(0x80 | (int) ((v >>> (i * 6)) & 0x3F), 8);
    }

    // ---- 子帧 ----
    private static int wastedBits(long[] s, int n) {
        long or = 0;
        for (int i = 0; i < n; i++) or |= s[i];
        if (or == 0) return 0;
        return Long.numberOfTrailingZeros(or);
    }

    /** 粗估这一路要多少位(选声道安排用):最好的固定预测阶的残差按 Rice 估 */
    private static long estimate(long[] s, int n, int sbits) {
        long best = Long.MAX_VALUE;
        long[] res = new long[n];
        for (int order = 0; order <= 4 && order < n; order++) {
            fixedResidual(s, n, order, res);
            long sum = 0;
            for (int i = order; i < n; i++) sum += res[i] < 0 ? -res[i] : res[i];
            long cnt = Math.max(1, n - order);
            int k = riceParamFor(sum, cnt);
            long bitsUsed = cnt * (1 + k) + (sum >> k);
            if (bitsUsed < best) best = bitsUsed;
        }
        return best;
    }

    private static int riceParamFor(long sum, long cnt) {
        int k = 0;
        while (k < 30 && (cnt << (k + 1)) < sum) k++;
        return k;
    }

    private static void fixedResidual(long[] x, int n, int order, long[] r) {
        for (int i = 0; i < Math.min(order, n); i++) r[i] = x[i];
        switch (order) {
        case 0: for (int i = 0; i < n; i++) r[i] = x[i]; break;
        case 1: for (int i = 1; i < n; i++) r[i] = x[i] - x[i - 1]; break;
        case 2: for (int i = 2; i < n; i++) r[i] = x[i] - 2 * x[i - 1] + x[i - 2]; break;
        case 3: for (int i = 3; i < n; i++) r[i] = x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3]; break;
        default: for (int i = 4; i < n; i++) r[i] = x[i] - 4 * x[i - 1] + 6 * x[i - 2] - 4 * x[i - 3] + x[i - 4]; break;
        }
    }

    private void subframe(long[] s0, int n, int sbits) {
        boolean constant = true;
        for (int i = 1; i < n; i++) if (s0[i] != s0[0]) { constant = false; break; }
        if (constant) {
            w.put(0, 1);
            w.put(0, 6);
            w.put(0, 1);
            w.putSigned(s0[0], sbits);
            return;
        }
        int wasted = wastedBits(s0, n);
        if (wasted >= sbits) wasted = 0;
        long[] s = s0;
        if (wasted > 0) {
            s = new long[n];
            for (int i = 0; i < n; i++) s[i] = s0[i] >> wasted;
        }
        int eb = sbits - wasted;
        // 每个固定阶算出精确的位数,挑最省的;都不如原样存就原样存
        long verbatimBits = (long) n * eb;
        int bestOrder = -1, bestPorder = 0;
        int[] bestParams = null;
        long bestBits = verbatimBits;
        long[] res = new long[n];
        long[] bestRes = null;
        for (int order = 0; order <= 4 && order < n; order++) {
            fixedResidual(s, n, order, res);
            boolean fits = true;
            for (int i = order; i < n; i++) if (res[i] > Integer.MAX_VALUE || res[i] < Integer.MIN_VALUE) { fits = false; break; }
            if (!fits) continue;
            int[] po = new int[1];
            long rb = riceBits(res, n, order, eb, po, null);
            long total = (long) order * eb + rb;
            if (total < bestBits) {
                bestBits = total;
                bestOrder = order;
                bestPorder = po[0];
                bestRes = res.clone();
            }
        }
        w.put(0, 1);
        if (bestOrder < 0) {
            w.put(1, 6);
            putWasted(wasted);
            for (int i = 0; i < n; i++) w.putSigned(s[i], eb);
            return;
        }
        w.put(8 + bestOrder, 6);
        putWasted(wasted);
        for (int i = 0; i < bestOrder; i++) w.putSigned(s[i], eb);
        writeResidual(bestRes, n, bestOrder, eb, bestPorder);
    }

    private void putWasted(int wasted) {
        if (wasted == 0) { w.put(0, 1); return; }
        w.put(1, 1);
        for (int i = 0; i < wasted - 1; i++) w.put(0, 1);
        w.put(1, 1);
    }

    private static int maxParam(int eb) { return eb <= 16 ? 14 : 30; }

    /** 残差按最好的分区阶数要多少位(含 2 位方法 + 4 位阶数);porderOut[0] = 挑中的阶数 */
    private static long riceBits(long[] res, int n, int order, int eb, int[] porderOut, int[] paramsOut) {
        int pbits = eb <= 16 ? 4 : 5, maxK = maxParam(eb);
        long best = Long.MAX_VALUE;
        for (int po = 0; po <= 8; po++) {
            int parts = 1 << po;
            if ((n & (parts - 1)) != 0) break;
            int per = n >> po;
            if (per < order) break;
            long total = 6;
            int i = order;
            for (int p = 0; p < parts; p++) {
                int cnt = p == 0 ? per - order : per;
                long sum = 0;
                for (int j = 0; j < cnt; j++, i++) {
                    long v = res[i];
                    sum += v >= 0 ? (v << 1) : ((-v << 1) - 1);
                }
                int k = Math.min(maxK, riceParamFor(sum, Math.max(1, cnt)));
                long cost = partCost(res, i - cnt, cnt, k);
                long c2 = k > 0 ? partCost(res, i - cnt, cnt, k - 1) : Long.MAX_VALUE;
                long c3 = k < maxK ? partCost(res, i - cnt, cnt, k + 1) : Long.MAX_VALUE;
                total += pbits + Math.min(cost, Math.min(c2, c3));
            }
            if (total < best) {
                best = total;
                porderOut[0] = po;
            }
        }
        return best;
    }

    private static long partCost(long[] res, int from, int cnt, int k) {
        long bitsUsed = (long) cnt * (k + 1);
        for (int j = 0; j < cnt; j++) {
            long v = res[from + j];
            long u = v >= 0 ? (v << 1) : ((-v << 1) - 1);
            bitsUsed += u >>> k;
        }
        return bitsUsed;
    }

    private void writeResidual(long[] res, int n, int order, int eb, int po) {
        int method = eb <= 16 ? 0 : 1, pbits = method == 0 ? 4 : 5, maxK = maxParam(eb);
        w.put(method, 2);
        w.put(po, 4);
        int parts = 1 << po, per = n >> po;
        int i = order;
        for (int p = 0; p < parts; p++) {
            int cnt = p == 0 ? per - order : per;
            long sum = 0;
            for (int j = 0; j < cnt; j++) {
                long v = res[i + j];
                sum += v >= 0 ? (v << 1) : ((-v << 1) - 1);
            }
            int k = testForceK0 ? 0 : Math.min(maxK, riceParamFor(sum, Math.max(1, cnt)));
            long best = partCost(res, i, cnt, k);
            if (testForceK0) { }
            else if (k > 0 && partCost(res, i, cnt, k - 1) < best) { best = partCost(res, i, cnt, k - 1); k = k - 1; }
            else if (k < maxK && partCost(res, i, cnt, k + 1) < best) { k = k + 1; }
            if (testEscape) {
                long mx = 0;
                for (int j = 0; j < cnt; j++) mx = Math.max(mx, Math.abs(res[i + j]));
                int raw = mx == 0 ? 0 : 64 - Long.numberOfLeadingZeros(mx) + 1;
                w.put(method == 0 ? 15 : 31, pbits);
                w.put(raw, 5);
                for (int j = 0; j < cnt; j++, i++) if (raw > 0) w.putSigned(res[i], raw);
                continue;
            }
            w.put(k, pbits);
            for (int j = 0; j < cnt; j++, i++) {
                long v = res[i];
                long u = v >= 0 ? (v << 1) : ((-v << 1) - 1);
                long q = u >>> k;
                w.unary(q);
                if (k > 0) w.put((int) (u & ((1L << k) - 1)), k);
            }
        }
    }

    /** 往字节数组里按位写(高位先) */
    static final class BitWriter {
        byte[] buf;
        private int pos;          // 已写满的字节数
        private long acc;
        private int accBits;

        BitWriter(int cap) { buf = new byte[cap]; }

        void reset() { pos = 0; acc = 0; accBits = 0; }

        private void ensure(int more) {
            if (pos + more + 16 > buf.length) {
                byte[] n = new byte[Math.max(buf.length * 2, pos + more + 16)];
                System.arraycopy(buf, 0, n, 0, pos);
                buf = n;
            }
        }

        void put(int v, int n) {
            if (n == 0) return;
            if (n > 32) throw new IllegalArgumentException();
            acc = (acc << n) | (v & ((n == 32) ? 0xFFFFFFFFL : ((1L << n) - 1)));
            accBits += n;
            flush();
        }

        void putSigned(long v, int n) {
            if (n <= 32) { put((int) v, n); return; }
            put((int) (v >> 32), n - 32);
            put((int) v, 32);
        }

        void unary(long zeros) {
            while (zeros >= 32) { put(0, 32); zeros -= 32; }
            put(1, (int) zeros + 1);
        }

        private void flush() {
            ensure(8);
            while (accBits >= 8) {
                buf[pos++] = (byte) (acc >>> (accBits - 8));
                accBits -= 8;
            }
            acc &= (1L << accBits) - 1;
        }

        void alignZero() {
            if (accBits > 0) put(0, 8 - accBits);
        }

        /** 只在字节对齐时有意义 */
        int length() { return pos; }
    }
}
