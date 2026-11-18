package com.omni.tagrepair.core;

/**
 * 同步标记检测:样本里有没有控制流的同步标记;有的话,开头那条数据同步记录里写的原始采样率是多少。
 * 格式事实与做法见 Windows 版 src/sync_detect.h 开头的一至五(取自公开资料);这里逐段对着 src/sync_detect.cpp 写,行为相同。
 * 样本约定:立体声交错的 int32,左对齐(24 bit 的值在高 24 位)。
 */
public final class SyncDetect {
    public static final long MARKER = 0x11319207D5L;     // 40 位
    public static final int MARKER_BITS = 40;
    public static final int LOWEST_BIT = 8, HIGHEST_BIT = 15;

    static final int HAS_POSITION_AT = 40, ORIGINAL_RATE_AT = 42, RATE_CODE_BITS = 5, ITEM_COUNT_AT = 73, ITEM_COUNT_BITS = 7,
            TABLES_AT = 80, POSITION_BITS = 32, CHECK_BITS = 4;

    /** 五:采样率的码 → Hz;无效的码返回 0 */
    public static int rateFromCode(int code) {
        final int[] base = { 44100, 48000, 64000, 0 };
        int which = (code >>> 3) & 3, power = code & 7;
        return code >= 0 && code < 32 ? base[which] << power : 0;
    }

    /** 四:bits[0..count) 的 4 位校验 */
    public static int recordCheck(byte[] bits, int count, int position) {
        int reg = position & 15;
        int i = 0;
        for (; i < count; i++) reg = in(reg, bits[i] & 1);
        for (; i % 32 != 0; i++) reg = in(reg, 0);
        return reg & 15;
    }

    private static int in(int reg, int b) {
        boolean out = (reg & 1) != 0;
        reg = (reg >>> 1) | (b << 31);
        if (out) reg ^= 3;
        return reg;
    }

    private static final int SEARCH = 0, HEAD = 1, TABLE = 2, BODY = 3, DONE = 4;
    private int stage = SEARCH;
    private final long[] recent = new long[HIGHEST_BIT - LOWEST_BIT + 1];
    private int bit = -1;
    private byte[] rec = new byte[256];
    private int recLen = 0;
    private int need = 0, checkAt = 0, rate = 0;
    private long frames = 0;

    public boolean markerFound() { return bit >= 0; }
    public int originalRate() { return rate; }
    public int controlBit() { return bit < 0 ? -1 : LOWEST_BIT + bit; }
    public long framesSeen() { return frames; }

    private void recPush(int b) {
        if (recLen == rec.length) {
            byte[] n = new byte[rec.length * 2];
            System.arraycopy(rec, 0, n, 0, recLen);
            rec = n;
        }
        rec[recLen++] = (byte) b;
    }

    private int field(int at, int width) {
        int v = 0;
        for (int i = 0; i < width; i++) v |= (rec[at + i] & 1) << i;
        return v;
    }

    private void startRecord() {
        recLen = 0;
        for (int k = 0; k < MARKER_BITS; k++) recPush((int) ((MARKER >>> k) & 1));
        stage = HEAD;
        need = TABLES_AT;
    }

    private void searchAgain() {
        stage = SEARCH;
        recLen = 0;
    }

    private void decide() {
        boolean hasPosition = field(HAS_POSITION_AT, 1) != 0;
        int items = field(ITEM_COUNT_AT, ITEM_COUNT_BITS);
        int positionAt = TABLES_AT + 16 * items;
        int itemsAt = positionAt + (hasPosition ? POSITION_BITS : 0);
        if (stage == HEAD) {
            stage = TABLE;
            need = itemsAt;
            if (recLen < need) return;
        }
        if (stage == TABLE) {
            int total = itemsAt;
            for (int i = 0; i < items; i++) total += field(TABLES_AT + 8 * i, 8);
            checkAt = total;
            stage = BODY;
            need = total + CHECK_BITS;
            if (recLen < need) return;
        }
        int position = hasPosition ? (field(positionAt, 16) | (field(positionAt + 16, 16) << 16)) : 0;
        boolean verified = recordCheck(rec, checkAt, position) == field(checkAt, CHECK_BITS);
        int r = verified ? rateFromCode(field(ORIGINAL_RATE_AT, RATE_CODE_BITS)) : 0;
        if (r > 0) {
            rate = r;
            stage = DONE;
            rec = new byte[0];
            recLen = 0;
        } else {
            searchAgain();
        }
    }

    private void push(int candidates) {
        switch (stage) {
        case DONE:
            return;
        case SEARCH: {
            int from = bit < 0 ? 0 : bit, to = bit < 0 ? HIGHEST_BIT - LOWEST_BIT : bit;
            for (int i = from; i <= to; i++) {
                long r = (recent[i] >>> 1) | ((long) ((candidates >>> i) & 1) << (MARKER_BITS - 1));
                recent[i] = r;
                if (r != MARKER) continue;
                bit = i;
                startRecord();
                return;
            }
            return;
        }
        default: {
            int b = (candidates >>> bit) & 1;
            long r = (recent[bit] >>> 1) | ((long) b << (MARKER_BITS - 1));
            recent[bit] = r;
            if (r == MARKER) {
                startRecord();
                return;
            }
            recPush(b);
            if (recLen >= need) decide();
        }
        }
    }

    /** 喂 n 帧(交错的左、右,从 lr[off] 起)。可以分很多次喂,结果与一次喂完相同 */
    public void feed(int[] lr, int off, int n) {
        frames += n;
        if (stage == DONE) return;
        for (int i = 0; i < n; i++) {
            int x = lr[off + 2 * i] ^ lr[off + 2 * i + 1];
            push((x >>> (LOWEST_BIT + 8)) & 0xFF);
        }
    }
}
