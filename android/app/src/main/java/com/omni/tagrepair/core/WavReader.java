package com.omni.tagrepair.core;

import java.util.ArrayList;
import java.util.List;

/**
 * WAV(RIFF / WAVE)。只认整数 PCM 与浮点(格式码 1、3,或 WAVE_FORMAT_EXTENSIBLE 里的这两种),读 fmt、data 与 LIST/INFO。
 * 编码名、元数据字典的样子照 Windows 版经 FFmpeg 看到的写:pcm_s16le / pcm_s24le / pcm_s32le / pcm_u8 / pcm_f32le / pcm_f64le;
 * INFO 里常见的几个 4 字符键换成 FFmpeg 用的名字(INAM → title、ISFT → encoder …),其余的原样用 4 字符键。
 * 不读:bext、id3 这些块里的标签(这一点与 Windows 版不同:另存的 FLAC 里不带它们)。
 */
public final class WavReader {
    public boolean ok;
    public Msg.Text error = Msg.EMPTY;
    public int formatTag, channels, rate, containerBits, blockAlign;
    public String codec = "";
    public boolean isFloat;
    public long dataAt, dataBytes;
    /** 元数据字典(键、值都是字节串),按 FFmpeg 的 av_dict_set 的排法 */
    public final List<String[]> meta = new ArrayList<String[]>();

    private static final String[][] INFO_CONV = {
        { "IART", "artist" }, { "ICMT", "comment" }, { "ICOP", "copyright" }, { "ICRD", "date" }, { "IGNR", "genre" },
        { "ILNG", "language" }, { "INAM", "title" }, { "IPRD", "album" }, { "IPRT", "track" }, { "ITRK", "track" },
        { "ISFT", "encoder" }, { "ISMP", "timecode" }, { "ITCH", "encoded_by" },
    };

    static long le32(byte[] b, int p) { return (b[p] & 0xFF) | ((b[p + 1] & 0xFF) << 8) | ((b[p + 2] & 0xFF) << 16) | ((long) (b[p + 3] & 0xFF) << 24); }
    static int le16(byte[] b, int p) { return (b[p] & 0xFF) | ((b[p + 1] & 0xFF) << 8); }

    public static boolean looksLikeWav(Rio r) {
        byte[] h = new byte[12];
        return r.size() >= 12 && r.read(0, h, 0, 12) && h[0] == 'R' && h[1] == 'I' && h[2] == 'F' && h[3] == 'F' && h[8] == 'W' && h[9] == 'A'
                && h[10] == 'V' && h[11] == 'E';
    }

    public static WavReader open(Rio r) {
        WavReader w = new WavReader();
        long size = r.size();
        byte[] h = new byte[8];
        long pos = 12;
        boolean haveFmt = false;
        while (pos + 8 <= size) {
            if (!r.read(pos, h, 0, 8)) { w.error = Msg.of(Msg.LayIo); return w; }
            String id = new String(h, 0, 4, java.nio.charset.StandardCharsets.ISO_8859_1);
            long len = le32(h, 4);
            long body = pos + 8;
            if (id.equals("fmt ")) {
                if (len < 16 || body + len > size) { w.error = Msg.of(Msg.AWavUnsupported, Msg.data("fmt")); return w; }
                byte[] f = Rio.Util.range(r, body, Math.min(len, 64));
                if (f == null) { w.error = Msg.of(Msg.LayIo); return w; }
                w.formatTag = le16(f, 0);
                w.channels = le16(f, 2);
                w.rate = (int) le32(f, 4);
                w.blockAlign = le16(f, 12);
                w.containerBits = le16(f, 14);
                int tag = w.formatTag;
                if (tag == 0xFFFE && f.length >= 40) {
                    // 子格式 GUID 的前两个字节就是格式码(后面是固定的 -0000-0010-8000-00AA00389B71)
                    tag = le16(f, 24);
                }
                if (tag == 1) {
                    switch (w.containerBits) {
                    case 8: w.codec = "pcm_u8"; break;
                    case 16: w.codec = "pcm_s16le"; break;
                    case 24: w.codec = "pcm_s24le"; break;
                    case 32: w.codec = "pcm_s32le"; break;
                    default: w.error = Msg.of(Msg.AWavUnsupported, Msg.data(w.containerBits + " bit")); return w;
                    }
                } else if (tag == 3) {
                    w.isFloat = true;
                    if (w.containerBits == 32) w.codec = "pcm_f32le";
                    else if (w.containerBits == 64) w.codec = "pcm_f64le";
                    else { w.error = Msg.of(Msg.AWavUnsupported, Msg.data("float " + w.containerBits)); return w; }
                } else {
                    w.error = Msg.of(Msg.AWavUnsupported, Msg.data(String.format("0x%04x", tag))); return w;
                }
                haveFmt = true;
            } else if (id.equals("data")) {
                if (w.dataAt == 0) {
                    w.dataAt = body;
                    w.dataBytes = (len == 0 || body + len > size) ? size - body : len;   // 长度没写 / 写多了:到文件末尾为止
                }
                if (len == 0 || body + len > size) break;                               // 数据一直到文件末尾:后面没有别的块了
                // 数据块之后还可能有 LIST 等块(FFmpeg 也接着往后读):接着看
            } else if (id.equals("LIST") && len >= 4 && body + len <= size && len <= (1 << 20)) {
                byte[] l = Rio.Util.range(r, body, len);
                if (l != null && l[0] == 'I' && l[1] == 'N' && l[2] == 'F' && l[3] == 'O') readInfo(w, l);
            }
            pos = body + len + (len & 1);
        }
        if (!haveFmt || w.dataAt == 0) { w.error = Msg.of(Msg.AWavUnsupported, Msg.data(haveFmt ? "data" : "fmt")); return w; }
        if (w.channels <= 0 || w.rate <= 0 || w.blockAlign != w.channels * (w.containerBits / 8)) {
            w.error = Msg.of(Msg.AWavUnsupported, Msg.data("block align"));
            return w;
        }
        w.ok = true;
        return w;
    }

    private static void readInfo(WavReader w, byte[] l) {
        int p = 4;
        while (p + 8 <= l.length) {
            String key = new String(l, p, 4, java.nio.charset.StandardCharsets.ISO_8859_1);
            long len = le32(l, p + 4);
            int body = p + 8;
            if (len > l.length - body) break;
            int n = 0;
            while (n < len && l[body + n] != 0) n++;               // 值到第一个 0 为止
            String value = TagRule.bytesToB(l, body, n);
            String conv = key;
            for (String[] c : INFO_CONV) if (c[0].equals(key)) { conv = c[1]; break; }
            dictSet(w.meta, conv, value);
            p = body + (int) len + (int) (len & 1);
        }
    }

    /** FFmpeg 的 av_dict_set(不带标志):同名的已有一条就把它拿掉(最后一条挪到它的位置),新的接在最后 */
    static void dictSet(List<String[]> d, String key, String value) {
        for (int i = 0; i < d.size(); i++)
            if (TagRule.keyEquals(d.get(i)[0], key)) {
                String[] last = d.remove(d.size() - 1);
                if (i < d.size()) d.set(i, last);
                break;
            }
        d.add(new String[] { key, value });
    }

    /** 整数样本有几位(浮点 0) */
    public int intBits() { return isFloat ? 0 : containerBits; }

    /** 一共多少帧(每声道的采样数;最后不满一帧的字节不算) */
    public long frames() { return blockAlign > 0 ? dataBytes / blockAlign : 0; }

    /** 从第 frame 帧起读 n 帧,转成交错、左对齐 int32 进 out。只给整数 PCM 用 */
    public boolean readFrames(Rio r, long frame, int n, int[] out, byte[] scratch) {
        int bytes = n * blockAlign;
        if (!r.read(dataAt + frame * blockAlign, scratch, 0, bytes)) return false;
        int total = n * channels, bps = containerBits / 8;
        for (int i = 0, p = 0; i < total; i++, p += bps) {
            int v;
            switch (bps) {
            case 1: v = ((scratch[p] & 0xFF) - 128) << 24; break;
            case 2: v = ((scratch[p] & 0xFF) | (scratch[p + 1] << 8)) << 16; break;
            case 3: v = ((scratch[p] & 0xFF) | ((scratch[p + 1] & 0xFF) << 8) | (scratch[p + 2] << 16)) << 8; break;
            default: v = (scratch[p] & 0xFF) | ((scratch[p + 1] & 0xFF) << 8) | ((scratch[p + 2] & 0xFF) << 16) | (scratch[p + 3] << 24); break;
            }
            out[i] = v;
        }
        return true;
    }
}
