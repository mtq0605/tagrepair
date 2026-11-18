package com.omni.tagrepair.core;

import java.security.MessageDigest;

/** FLAC 帧头 CRC-8(多项式 0x07)、帧尾 CRC-16(多项式 0x8005);MD5 / SHA-256 的小工具。 */
public final class Crc {
    private Crc() {}

    private static final int[] T8 = new int[256], T16 = new int[256];
    static {
        for (int i = 0; i < 256; i++) {
            int c = i;
            for (int b = 0; b < 8; b++) c = (c & 0x80) != 0 ? ((c << 1) ^ 0x07) & 0xFF : (c << 1) & 0xFF;
            T8[i] = c;
            int d = i << 8;
            for (int b = 0; b < 8; b++) d = (d & 0x8000) != 0 ? ((d << 1) ^ 0x8005) & 0xFFFF : (d << 1) & 0xFFFF;
            T16[i] = d;
        }
    }

    public static int crc8(byte[] d, int off, int n) {
        int c = 0;
        for (int i = 0; i < n; i++) c = T8[(c ^ d[off + i]) & 0xFF];
        return c;
    }

    public static int crc16(byte[] d, int off, int n) {
        int c = 0;
        for (int i = 0; i < n; i++) c = ((c << 8) & 0xFFFF) ^ T16[((c >>> 8) ^ d[off + i]) & 0xFF];
        return c;
    }

    public static MessageDigest md(String alg) {
        try { return MessageDigest.getInstance(alg); } catch (Exception e) { throw new IllegalStateException(alg, e); }
    }

    public static String hex(byte[] h) {
        StringBuilder b = new StringBuilder();
        for (byte x : h) b.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
        return b.toString();
    }

    /** 交错、左对齐 int32 的样本按小端字节进 MD5(与 Windows 版直接对内存算 MD5 相同) */
    public static void updateInt32(MessageDigest md, int[] v, int n, byte[] scratch) {
        int p = 0;
        for (int i = 0; i < n; i++) {
            int x = v[i];
            scratch[p++] = (byte) x;
            scratch[p++] = (byte) (x >>> 8);
            scratch[p++] = (byte) (x >>> 16);
            scratch[p++] = (byte) (x >>> 24);
            if (p == scratch.length) { md.update(scratch, 0, p); p = 0; }
        }
        if (p > 0) md.update(scratch, 0, p);
    }

    /** 文件 [from, to) 的 SHA-256(十六进制);读不了返回 null */
    public static String sha256(Rio r, long from, long to) {
        MessageDigest md = md("SHA-256");
        byte[] b = new byte[1 << 20];
        for (long at = from; at < to; ) {
            int k = (int) Math.min(b.length, to - at);
            if (!r.read(at, b, 0, k)) return null;
            md.update(b, 0, k);
            at += k;
        }
        return hex(md.digest());
    }
}
