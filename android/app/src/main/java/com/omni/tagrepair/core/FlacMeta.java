package com.omni.tagrepair.core;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * FLAC 元数据区与 Vorbis 注释(纯函数:只碰字节)。逐个函数对着 Windows 版 src/flac_meta.cpp 写,
 * 给同样的输入算出同样的字节 —— 修好的文件与 Windows 版逐字节相同靠的就是这里。原则:音频帧一个字节都不动。
 */
public final class FlacMeta {
    private FlacMeta() {}

    public static final int MAX_BLOCK_BODY = 0xFFFFFF;
    public static final long MAX_META_BYTES = 512L << 20;
    public static final long SECTOR_BYTES = 512;
    public static final int STREAMINFO = 0, PADDING = 1, APPLICATION = 2, SEEKTABLE = 3, VORBIS_COMMENT = 4, CUESHEET = 5, PICTURE = 6;
    public static final String NEW_VENDOR = "tagrepair";

    public static boolean withinOneSector(long from, long end, long sectorBytes) {
        return sectorBytes > 0 && end > from && from / sectorBytes == (end - 1) / sectorBytes;
    }

    public static final class BlockRef {
        public int type;
        public boolean last;
        public long at;
        public int len;
    }

    public static final class Layout {
        public long id3Bytes, metaAt, audioAt, fileSize;
        public final List<BlockRef> blocks = new ArrayList<BlockRef>();
        public final byte[] streamInfo = new byte[34];
        public int sampleRate, channels, bits;
        public long totalSamples;
    }

    public enum Err { OK, NOT_FLAC, TRUNCATED, CORRUPT, IO }

    /** 结果 + 走不通的原因 */
    public static final class LayoutResult {
        public Err err = Err.OK;
        public Msg.Entry why;
    }

    static int u8(byte b) { return b & 0xFF; }

    public static LayoutResult readLayout(Rio s, Layout out) {
        LayoutResult res = new LayoutResult();
        out.blocks.clear();
        out.fileSize = s.size();
        long at = 0;
        byte[] h = new byte[10];
        for (int guard = 0; guard < 8 && at + 10 <= out.fileSize; guard++) {
            if (!s.read(at, h, 0, 10)) return fail(res, Err.IO, Msg.LayIo);
            if (!(h[0] == 'I' && h[1] == 'D' && h[2] == '3' && u8(h[3]) != 0xFF && u8(h[4]) != 0xFF && ((u8(h[6]) | u8(h[7]) | u8(h[8]) | u8(h[9])) & 0x80) == 0))
                break;
            long body = ((long) u8(h[6]) << 21) | ((long) u8(h[7]) << 14) | ((long) u8(h[8]) << 7) | u8(h[9]);
            at += 10 + body + ((u8(h[5]) & 0x10) != 0 ? 10 : 0);
            if (at > out.fileSize) return fail(res, Err.TRUNCATED, Msg.LayId3TooLong);
        }
        out.id3Bytes = at;
        byte[] sig = new byte[4];
        if (at + 4 > out.fileSize || !s.read(at, sig, 0, 4) || sig[0] != 'f' || sig[1] != 'L' || sig[2] != 'a' || sig[3] != 'C')
            return fail(res, Err.NOT_FLAC, Msg.LayNotFlac);
        out.metaAt = at + 4;
        long pos = out.metaAt;
        byte[] bh = new byte[4];
        while (true) {
            if (pos + 4 > out.fileSize) return fail(res, Err.TRUNCATED, Msg.LayHeaderCut);
            if (!s.read(pos, bh, 0, 4)) return fail(res, Err.IO, Msg.LayIo);
            BlockRef b = new BlockRef();
            b.type = u8(bh[0]) & 0x7F;
            b.last = (u8(bh[0]) & 0x80) != 0;
            b.len = (u8(bh[1]) << 16) | (u8(bh[2]) << 8) | u8(bh[3]);
            b.at = pos;
            if (b.type == 127) return fail(res, Err.CORRUPT, Msg.LayBadType);
            if (out.blocks.isEmpty() && !(b.type == STREAMINFO && b.len == 34)) return fail(res, Err.CORRUPT, Msg.LayFirstNotStreamInfo);
            if (!out.blocks.isEmpty() && b.type == STREAMINFO) return fail(res, Err.CORRUPT, Msg.LayTwoStreamInfo);
            if (pos + 4 + b.len > out.fileSize) return fail(res, Err.TRUNCATED, Msg.LayBlockBeyondFile);
            if (b.type == STREAMINFO && !s.read(pos + 4, out.streamInfo, 0, 34)) return fail(res, Err.IO, Msg.LayIo);
            out.blocks.add(b);
            pos += 4 + (long) b.len;
            if (b.last) break;
            if (out.blocks.size() > 65536) return fail(res, Err.CORRUPT, Msg.LayTooManyBlocks);
            if (pos - out.metaAt > MAX_META_BYTES) return fail(res, Err.CORRUPT, Msg.LayMetaTooBig);
        }
        out.audioAt = pos;
        byte[] si = out.streamInfo;
        out.sampleRate = (u8(si[10]) << 12) | (u8(si[11]) << 4) | (u8(si[12]) >>> 4);
        out.channels = ((u8(si[12]) >>> 1) & 7) + 1;
        out.bits = (((u8(si[12]) & 1) << 4) | (u8(si[13]) >>> 4)) + 1;
        out.totalSamples = ((long) (u8(si[13]) & 0x0F) << 32) | ((long) u8(si[14]) << 24) | ((long) u8(si[15]) << 16) | ((long) u8(si[16]) << 8) | u8(si[17]);
        if (out.sampleRate == 0) return fail(res, Err.CORRUPT, Msg.LayRateZero);
        if (out.fileSize - out.audioAt >= 2) {
            byte[] f = new byte[2];
            if (!s.read(out.audioAt, f, 0, 2)) return fail(res, Err.IO, Msg.LayIo);
            if (!(u8(f[0]) == 0xFF && (u8(f[1]) & 0xFE) == 0xF8)) return fail(res, Err.CORRUPT, Msg.LayNoFrameAfterMeta);
        } else if (out.totalSamples > 0 || out.fileSize != out.audioAt) {
            return fail(res, Err.TRUNCATED, Msg.LayNoAudio);
        }
        return res;
    }

    private static LayoutResult fail(LayoutResult r, Err e, Msg.Entry why) {
        r.err = e;
        r.why = why;
        return r;
    }

    // ---- STREAMINFO(新写一个 FLAC 时用)----
    public static final class StreamInfoFields {
        public int minBlock, maxBlock;
        public int minFrame, maxFrame;
        public int sampleRate, channels, bits;
        public long totalSamples;
        public final byte[] md5 = new byte[16];
    }

    public static byte[] buildStreamInfo(StreamInfoFields f) {
        byte[] o = new byte[34];
        int rate = f.sampleRate, ch = f.channels - 1, bps = f.bits - 1;
        o[0] = (byte) (f.minBlock >> 8);
        o[1] = (byte) f.minBlock;
        o[2] = (byte) (f.maxBlock >> 8);
        o[3] = (byte) f.maxBlock;
        o[4] = (byte) (f.minFrame >> 16);
        o[5] = (byte) (f.minFrame >> 8);
        o[6] = (byte) f.minFrame;
        o[7] = (byte) (f.maxFrame >> 16);
        o[8] = (byte) (f.maxFrame >> 8);
        o[9] = (byte) f.maxFrame;
        o[10] = (byte) (rate >>> 12);
        o[11] = (byte) (rate >>> 4);
        o[12] = (byte) (((rate & 0x0F) << 4) | ((ch & 7) << 1) | ((bps >>> 4) & 1));
        o[13] = (byte) (((bps & 0x0F) << 4) | (int) ((f.totalSamples >>> 32) & 0x0F));
        o[14] = (byte) (f.totalSamples >>> 24);
        o[15] = (byte) (f.totalSamples >>> 16);
        o[16] = (byte) (f.totalSamples >>> 8);
        o[17] = (byte) f.totalSamples;
        System.arraycopy(f.md5, 0, o, 18, 16);
        return o;
    }

    // ---- Vorbis 注释(厂商串、每一条都是字节串)----
    public static final class Comments {
        public String vendor = "";
        public final List<String> items = new ArrayList<String>();
        public byte[] tail = new byte[0];

        Comments copy() {
            Comments c = new Comments();
            c.vendor = vendor;
            c.items.addAll(items);
            c.tail = tail.clone();
            return c;
        }
    }

    static long le32(byte[] d, int p) {
        return (u8(d[p]) | (u8(d[p + 1]) << 8) | (u8(d[p + 2]) << 16) | ((long) u8(d[p + 3]) << 24)) & 0xFFFFFFFFL;
    }

    /** 拆开;坏了返回 null */
    public static Comments parseComments(byte[] d, int off, int n) {
        Comments out = new Comments();
        int[] pos = { off };
        int end = off + n;
        String v = str(d, pos, end);
        if (v == null) return null;
        out.vendor = v;
        if (end - pos[0] < 4) return null;
        long count = le32(d, pos[0]);
        pos[0] += 4;
        if (count > (end - pos[0]) / 4) return null;
        for (long i = 0; i < count; i++) {
            String it = str(d, pos, end);
            if (it == null) return null;
            out.items.add(it);
        }
        out.tail = new byte[end - pos[0]];
        System.arraycopy(d, pos[0], out.tail, 0, out.tail.length);
        return out;
    }

    private static String str(byte[] d, int[] pos, int end) {
        if (end - pos[0] < 4) return null;
        long len = le32(d, pos[0]);
        pos[0] += 4;
        if (len > end - pos[0]) return null;
        String s = TagRule.bytesToB(d, pos[0], (int) len);
        pos[0] += (int) len;
        return s;
    }

    public static byte[] buildComments(Comments c) {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        putStr(o, c.vendor);
        putLe32(o, c.items.size());
        for (String it : c.items) putStr(o, it);
        o.write(c.tail, 0, c.tail.length);
        return o.toByteArray();
    }

    private static void putLe32(ByteArrayOutputStream o, long v) {
        o.write((int) v);
        o.write((int) (v >>> 8));
        o.write((int) (v >>> 16));
        o.write((int) (v >>> 24));
    }

    private static void putStr(ByteArrayOutputStream o, String s) {
        byte[] b = TagRule.bToBytes(s);
        putLe32(o, b.length);
        o.write(b, 0, b.length);
    }

    /** "KEY=value" → {键, 值};没有 '=' 返回 null */
    public static String[] splitItem(String item) {
        int eq = item.indexOf('=');
        if (eq < 0) return null;
        return new String[] { item.substring(0, eq), item.substring(eq + 1) };
    }

    static String itemValueIfKey(String item, int k) {
        String[] kv = splitItem(item);
        if (kv == null || !TagRule.keyEquals(kv[0], TagRule.keyName(k))) return null;
        return kv[1];
    }

    // ---- 元数据区 ↔ 块 ----
    public static final class MetaBlock {
        public int type;
        public byte[] body = new byte[0];
        MetaBlock() {}
        MetaBlock(int t, byte[] b) { type = t; body = b; }
    }

    /** 拆;坏了返回 null */
    public static List<MetaBlock> splitBlocks(byte[] meta, int n) {
        List<MetaBlock> out = new ArrayList<MetaBlock>();
        int pos = 0;
        while (true) {
            if (n - pos < 4) return null;
            boolean last = (u8(meta[pos]) & 0x80) != 0;
            int len = (u8(meta[pos + 1]) << 16) | (u8(meta[pos + 2]) << 8) | u8(meta[pos + 3]);
            MetaBlock b = new MetaBlock();
            b.type = u8(meta[pos]) & 0x7F;
            pos += 4;
            if (len > n - pos) return null;
            b.body = new byte[len];
            System.arraycopy(meta, pos, b.body, 0, len);
            pos += len;
            out.add(b);
            if (last) return pos == n ? out : null;
            if (pos == n) return null;
        }
    }

    public static byte[] joinBlocks(List<MetaBlock> b) {
        int total = 0;
        for (MetaBlock x : b) total += 4 + x.body.length;
        byte[] o = new byte[total];
        int p = 0;
        for (int i = 0; i < b.size(); i++) {
            int len = b.get(i).body.length;
            o[p++] = (byte) ((i + 1 == b.size() ? 0x80 : 0) | (b.get(i).type & 0x7F));
            o[p++] = (byte) (len >>> 16);
            o[p++] = (byte) (len >>> 8);
            o[p++] = (byte) len;
            System.arraycopy(b.get(i).body, 0, o, p, len);
            p += len;
        }
        return o;
    }

    // ---- 三个键 ----
    public static TagRule.Seen seenIn(List<Comments> vcs) {
        TagRule.Seen s = new TagRule.Seen();
        for (Comments c : vcs)
            for (String it : c.items) {
                String[] kv = splitItem(it);
                if (kv == null) continue;
                for (int k = 0; k < TagRule.KEYS; k++)
                    if (TagRule.keyEquals(kv[0], TagRule.keyName(k)) && s.count[k]++ == 0) s.value[k] = kv[1];
            }
        return s;
    }

    /** 元数据区里的注释块全部解出来;坏了返回 null */
    public static List<Comments> commentsOf(byte[] meta) {
        List<MetaBlock> blocks = splitBlocks(meta, meta.length);
        if (blocks == null) return null;
        List<Comments> vcs = new ArrayList<Comments>();
        for (MetaBlock b : blocks) {
            if (b.type != VORBIS_COMMENT) continue;
            Comments c = parseComments(b.body, 0, b.body.length);
            if (c == null) return null;
            vcs.add(c);
        }
        return vcs;
    }

    public static TagRule.Seen seenInMeta(byte[] meta) {
        List<Comments> vcs = commentsOf(meta);
        return vcs == null ? null : seenIn(vcs);
    }

    /** 修完之后每个键的值(字节串);码流里没读到、文件里也没有合格的:fin[ORIG_RATE] 为空 */
    public static String[] chooseFinal(List<Comments> vcs, int streamOrigRate, TagRule.Rule rule) {
        String[] fin = { "", "", "" };
        for (int k = 0; k < TagRule.KEYS; k++) {
            boolean have = false;
            for (int b = 0; b < vcs.size() && !have; b++)
                for (String it : vcs.get(b).items) {
                    String v = itemValueIfKey(it, k);
                    if (v != null && TagRule.valueOk(k, v, streamOrigRate, rule)) {
                        fin[k] = v;
                        have = true;
                        break;
                    }
                }
            if (have) continue;
            if (k == TagRule.ORIG_RATE) {
                if (streamOrigRate > 0) fin[k] = Integer.toString(streamOrigRate);
            } else {
                fin[k] = rule.encoderTag;
            }
        }
        return fin;
    }

    /** 把注释改成"三个键各恰好一份、值为 fin";返回改没改 */
    public static boolean applyTags(List<Comments> vcs, String[] fin) {
        if (vcs.isEmpty()) return false;
        boolean changed = false;
        for (int k = 0; k < TagRule.KEYS; k++) {
            int count = 0;
            boolean onlyOk = false;
            for (Comments c : vcs)
                for (String it : c.items) {
                    String v = itemValueIfKey(it, k);
                    if (v == null) continue;
                    ++count;
                    onlyOk = v.equals(fin[k]);
                }
            if (count == 1 && onlyOk) continue;
            String want = TagRule.keyName(k) + "=" + fin[k];
            boolean placed = false;
            for (int b = 0; b < vcs.size(); b++) {
                List<String> items = vcs.get(b).items;
                for (int i = 0; i < items.size(); ) {
                    String v = itemValueIfKey(items.get(i), k);
                    if (v == null) { ++i; continue; }
                    if (b == 0 && !placed) {
                        if (!v.equals(fin[k])) { items.set(i, want); changed = true; }
                        placed = true;
                        ++i;
                    } else {
                        items.remove(i);
                        changed = true;
                    }
                }
            }
            if (!placed) {
                vcs.get(0).items.add(want);
                changed = true;
            }
        }
        return changed;
    }

    // ---- 修好之后的整段元数据 ----
    public enum Mode { NO_CHANGE, IN_PLACE, REWRITE }
    public enum PlanErr { OK, BAD_META, BAD_COMMENT, TOO_BIG, INTERNAL }

    public static final class Plan {
        public Mode mode = Mode.NO_CHANGE;
        public byte[] newMeta = new byte[0];
        public boolean sameSize;
        public int diffAt, diffEnd;
    }

    private static List<MetaBlock> copyBlocks(List<MetaBlock> b) {
        List<MetaBlock> c = new ArrayList<MetaBlock>();
        for (MetaBlock x : b) c.add(new MetaBlock(x.type, x.body.clone()));
        return c;
    }

    private static byte[] resized(byte[] b, int n) {
        byte[] r = new byte[n];
        System.arraycopy(b, 0, r, 0, Math.min(n, b.length));
        return r;
    }

    public static PlanErr planFix(byte[] oldMeta, String[] fin, long metaAtInFile, long sectorBytes, Plan out) {
        out.mode = Mode.NO_CHANGE;
        out.newMeta = new byte[0];
        out.sameSize = false;
        out.diffAt = out.diffEnd = 0;
        for (int k = 0; k < TagRule.KEYS; k++) if (fin[k].isEmpty()) return PlanErr.INTERNAL;
        List<MetaBlock> blocks = splitBlocks(oldMeta, oldMeta.length);
        if (blocks == null || blocks.isEmpty() || blocks.get(0).type != STREAMINFO) return PlanErr.BAD_META;
        List<Integer> vcIdx = new ArrayList<Integer>();
        List<Comments> vcs = new ArrayList<Comments>();
        for (int i = 0; i < blocks.size(); i++) {
            if (blocks.get(i).type != VORBIS_COMMENT) continue;
            Comments c = parseComments(blocks.get(i).body, 0, blocks.get(i).body.length);
            if (c == null) return PlanErr.BAD_COMMENT;
            vcIdx.add(i);
            vcs.add(c);
        }
        boolean created = false;
        if (vcIdx.isEmpty()) {
            int at = blocks.size();
            for (int i = 0; i < blocks.size(); i++) if (blocks.get(i).type == PADDING) { at = i; break; }
            blocks.add(at, new MetaBlock(VORBIS_COMMENT, new byte[0]));
            vcIdx.add(at);
            Comments c = new Comments();
            c.vendor = NEW_VENDOR;
            vcs.add(c);
            created = true;
        }
        boolean changed = applyTags(vcs, fin);
        if (!changed && !created) return PlanErr.OK;
        for (int j = 0; j < vcIdx.size(); j++) {
            byte[] body = buildComments(vcs.get(j));
            if (body.length > MAX_BLOCK_BODY) return PlanErr.TOO_BIG;
            blocks.get(vcIdx.get(j)).body = body;
        }
        long oldTotal = oldMeta.length;
        long newTotal = 0;
        for (MetaBlock b : blocks) newTotal += 4 + b.body.length;

        List<MetaBlock> cand = copyBlocks(blocks);
        List<Integer> pads = new ArrayList<Integer>();
        int vc0 = vcIdx.get(0);
        for (int i = vc0 + 1; i < cand.size(); i++) if (cand.get(i).type == PADDING) pads.add(i);
        for (int i = vc0; i-- > 0; ) if (cand.get(i).type == PADDING) pads.add(i);
        boolean fit = false;
        if (newTotal == oldTotal) {
            fit = true;
        } else if (newTotal > oldTotal) {
            long need = newTotal - oldTotal;
            boolean[] drop = new boolean[cand.size()];
            for (int pi : pads) {
                if (need == 0) break;
                int len = cand.get(pi).body.length;
                if (len >= need) { cand.get(pi).body = resized(cand.get(pi).body, (int) (len - need)); need = 0; }
                else if (len + 4 <= need) { drop[pi] = true; need -= len + 4; }
                else { cand.get(pi).body = new byte[0]; need -= len; }
            }
            if (need == 0) {
                List<MetaBlock> kept = new ArrayList<MetaBlock>();
                for (int i = 0; i < cand.size(); i++) if (!drop[i]) kept.add(cand.get(i));
                cand = kept;
                fit = true;
            }
        } else {
            long spare = oldTotal - newTotal;
            if (!pads.isEmpty()) {
                MetaBlock p = cand.get(pads.get(0));
                if (p.body.length + spare <= MAX_BLOCK_BODY) { p.body = resized(p.body, (int) (p.body.length + spare)); fit = true; }
            } else if (spare >= 4 && spare - 4 <= MAX_BLOCK_BODY) {
                cand.add(vc0 + 1, new MetaBlock(PADDING, new byte[(int) (spare - 4)]));
                fit = true;
            }
        }
        if (fit) {
            out.newMeta = joinBlocks(cand);
            if (out.newMeta.length != oldTotal) return PlanErr.INTERNAL;
            int a = 0, b = (int) oldTotal;
            while (a < b && out.newMeta[a] == oldMeta[a]) ++a;
            while (b > a && out.newMeta[b - 1] == oldMeta[b - 1]) --b;
            if (a == b) {
                out.newMeta = new byte[0];
                out.mode = Mode.NO_CHANGE;
                return PlanErr.OK;
            }
            out.sameSize = true;
            out.diffAt = a;
            out.diffEnd = b;
            out.mode = withinOneSector(metaAtInFile + a, metaAtInFile + b, sectorBytes) ? Mode.IN_PLACE : Mode.REWRITE;
            return PlanErr.OK;
        }
        out.newMeta = joinBlocks(blocks);
        out.sameSize = false;
        out.mode = Mode.REWRITE;
        return PlanErr.OK;
    }
}
