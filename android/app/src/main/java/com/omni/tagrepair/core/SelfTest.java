package com.omni.tagrepair.core;

import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * 自检:不靠任何外部文件,用合成的测试信号(低电平噪声,不是音乐)。电脑上(test/DeskTool selftest)和手机上(应用里)跑的是同一份。
 * 项目对着 Windows 版的 tests/unit_tests.cpp 与 src/selftest.cpp 选的:元数据块、注释、键匹配、合格的定义、PADDING 找平与扇区、
 * 同步标记检测、解码 / 编码往返、整个修复(原地 / 整文件重写)、出错时的恢复、取消、写回被打断后接着写、另存两种。
 * 合成文件用的拼法(Synth)与被测的代码是分开写的两份,互相核对。
 */
public final class SelfTest {
    private SelfTest() {}

    public static final class Result {
        public int pass, fail;
        public String log = "";
    }

    private static StringBuilder log;
    private static int pass, fail;

    private static void expect(String name, boolean ok, String note) {
        if (ok) pass++;
        else fail++;
        log.append(ok ? "  ok    " : "  FAIL  ").append(name);
        if (!ok && note != null) log.append("  [").append(note).append("]");
        log.append('\n');
    }

    // ---------------------------------------------------------------------------
    // 合成文件(与 Windows 版 src/synth_flac.cpp 同样的拼法)
    // ---------------------------------------------------------------------------
    static final class Synth {
        static int[] noise(int frames, int bits, int seed) {
            int[] v = new int[frames * 2];
            int s = seed != 0 ? seed : 1;
            int shift = 32 - (bits - 6);
            for (int i = 0; i < v.length; i++) {
                s = s * 1664525 + 1013904223;
                v[i] = s >> shift;
            }
            return v;
        }

        static int rateCode(int hz) {
            int[] base = { 44100, 48000, 64000 };
            for (int b = 0; b < 3; b++) for (int k = 0; k < 8; k++) if ((base[b] << k) == hz) return (b << 3) | k;
            return -1;
        }

        static byte[] recordBits(int origCode, int carrierCode, boolean hasPosition, int position, int[][] items, int fillSeed) {
            List<Byte> bits = new ArrayList<Byte>();
            put(bits, 0x11319207d5L, 40);
            put(bits, hasPosition ? 1 : 0, 1);
            put(bits, 0, 1);
            put(bits, origCode, 5);
            put(bits, carrierCode, 5);
            put(bits, 0, 21);
            put(bits, items.length, 7);
            for (int[] it : items) put(bits, it[1], 8);
            for (int[] it : items) put(bits, it[0], 8);
            if (hasPosition) put(bits, position & 0xFFFFFFFFL, 32);
            int s = fillSeed != 0 ? fillSeed : 1;
            for (int[] it : items)
                for (int i = 0; i < it[1]; i++) {
                    s = s * 1664525 + 1013904223;
                    bits.add((byte) (s >>> 31));
                }
            byte[] b = new byte[bits.size()];
            for (int i = 0; i < b.length; i++) b[i] = bits.get(i);
            int check = checkOf(b, b.length, hasPosition ? position : 0);
            byte[] all = Arrays.copyOf(b, b.length + 4);
            for (int i = 0; i < 4; i++) all[b.length + i] = (byte) ((check >>> i) & 1);
            return all;
        }

        static void put(List<Byte> bits, long v, int n) { for (int i = 0; i < n; i++) bits.add((byte) ((v >>> i) & 1)); }

        static int checkOf(byte[] bits, int count, int position) {
            int reg = position & 15;
            int i = 0;
            for (; i < count; i++) { int x = reg & 1; reg = (reg >>> 1) | ((bits[i] & 1) << 31); if (x != 0) reg ^= 3; }
            for (; i % 32 != 0; i++) { int x = reg & 1; reg = reg >>> 1; if (x != 0) reg ^= 3; }
            return reg & 15;
        }

        static void embed(int[] lr, int bits, int atFrame, byte[] ctrl, int sampleBit) {
            int bit = sampleBit - (24 - bits);
            for (int i = 0; i < ctrl.length && atFrame + i < lr.length / 2; i++) {
                int l = lr[(atFrame + i) * 2];
                int want = ((l >> bit) & 1) ^ ctrl[i];
                int r = lr[(atFrame + i) * 2 + 1];
                lr[(atFrame + i) * 2 + 1] = (r & ~(1 << bit)) | (want << bit);
            }
        }

        static int minFrame, maxFrame;

        static byte[] frames(int[] lr, int rate, int bits, int block) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            int total = lr.length / 2, bytes = bits / 8, number = 0;
            minFrame = Integer.MAX_VALUE;
            maxFrame = 0;
            for (int at = 0; at < total; at += block, number++) {
                int n = Math.min(block, total - at);
                java.io.ByteArrayOutputStream f = new java.io.ByteArrayOutputStream();
                f.write(0xFF);
                f.write(0xF8);
                boolean full = n == block && block == 4096;
                int sr = rate == 44100 ? 9 : rate == 48000 ? 10 : 0;
                f.write(((full ? 12 : 7) << 4) | sr);
                f.write((1 << 4) | ((bits == 16 ? 4 : 6) << 1));
                if (number < 0x80) f.write(number);
                else if (number < 0x800) { f.write(0xC0 | (number >> 6)); f.write(0x80 | (number & 0x3F)); }
                else { f.write(0xE0 | (number >> 12)); f.write(0x80 | ((number >> 6) & 0x3F)); f.write(0x80 | (number & 0x3F)); }
                if (!full) { f.write((n - 1) >> 8); f.write(n - 1); }
                byte[] h = f.toByteArray();
                f.write(crc8(h));
                for (int c = 0; c < 2; c++) {
                    f.write(0x02);
                    for (int i = 0; i < n; i++) {
                        int v = lr[(at + i) * 2 + c];
                        for (int b = bytes - 1; b >= 0; b--) f.write(v >>> (8 * b));
                    }
                }
                byte[] body = f.toByteArray();
                int c16 = crc16(body);
                f.write(c16 >> 8);
                f.write(c16);
                byte[] fr = f.toByteArray();
                minFrame = Math.min(minFrame, fr.length);
                maxFrame = Math.max(maxFrame, fr.length);
                out.write(fr, 0, fr.length);
            }
            return out.toByteArray();
        }

        static int crc8(byte[] d) {
            int c = 0;
            for (byte x : d) { c ^= x & 0xFF; for (int b = 0; b < 8; b++) c = (c & 0x80) != 0 ? ((c << 1) ^ 7) & 0xFF : (c << 1) & 0xFF; }
            return c;
        }

        static int crc16(byte[] d) {
            int c = 0;
            for (byte x : d) { c ^= (x & 0xFF) << 8; for (int b = 0; b < 8; b++) c = (c & 0x8000) != 0 ? ((c << 1) ^ 0x8005) & 0xFFFF : (c << 1) & 0xFFFF; }
            return c;
        }

        static byte[] streamInfo(int rate, int bits, long total, int block, byte[] md5) {
            byte[] s = new byte[34];
            s[0] = (byte) (block >> 8); s[1] = (byte) block; s[2] = (byte) (block >> 8); s[3] = (byte) block;
            s[4] = (byte) (minFrame >> 16); s[5] = (byte) (minFrame >> 8); s[6] = (byte) minFrame;
            s[7] = (byte) (maxFrame >> 16); s[8] = (byte) (maxFrame >> 8); s[9] = (byte) maxFrame;
            s[10] = (byte) (rate >> 12); s[11] = (byte) (rate >> 4);
            s[12] = (byte) (((rate & 0x0F) << 4) | (1 << 1) | (((bits - 1) >> 4) & 1));
            s[13] = (byte) ((((bits - 1) & 0x0F) << 4) | (int) ((total >>> 32) & 0x0F));
            s[14] = (byte) (total >>> 24); s[15] = (byte) (total >>> 16); s[16] = (byte) (total >>> 8); s[17] = (byte) total;
            if (md5 != null) System.arraycopy(md5, 0, s, 18, 16);
            return s;
        }

        static byte[] vorbis(String vendor, String... items) {
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            le(o, vendor.length());
            for (char c : vendor.toCharArray()) o.write(c);
            le(o, items.length);
            for (String it : items) { le(o, it.length()); for (char c : it.toCharArray()) o.write(c); }
            return o.toByteArray();
        }

        static void le(java.io.ByteArrayOutputStream o, int v) { for (int i = 0; i < 4; i++) o.write(v >>> (8 * i)); }

        static byte[] file(byte[] prefix, Object[][] blocks, byte[] frames, byte[] suffix) {
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            if (prefix != null) o.write(prefix, 0, prefix.length);
            o.write('f'); o.write('L'); o.write('a'); o.write('C');
            for (int i = 0; i < blocks.length; i++) {
                byte[] b = (byte[]) blocks[i][1];
                o.write((i + 1 == blocks.length ? 0x80 : 0) | ((Integer) blocks[i][0] & 0x7F));
                o.write(b.length >> 16); o.write(b.length >> 8); o.write(b.length);
                o.write(b, 0, b.length);
            }
            o.write(frames, 0, frames.length);
            if (suffix != null) o.write(suffix, 0, suffix.length);
            return o.toByteArray();
        }

        static byte[] id3v2(int body) {
            byte[] o = new byte[10 + body];
            o[0] = 'I'; o[1] = 'D'; o[2] = '3'; o[3] = 3;
            o[6] = (byte) ((body >> 21) & 0x7F); o[7] = (byte) ((body >> 14) & 0x7F); o[8] = (byte) ((body >> 7) & 0x7F); o[9] = (byte) (body & 0x7F);
            return o;
        }

        static byte[] wav(int[] lr, int rate, int bits) {
            java.io.ByteArrayOutputStream d = new java.io.ByteArrayOutputStream();
            int bytes = bits / 8;
            for (int v : lr) for (int b = 0; b < bytes; b++) d.write(v >>> (8 * b));
            byte[] data = d.toByteArray();
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            o.write('R'); o.write('I'); o.write('F'); o.write('F'); le(o, 36 + data.length);
            o.write('W'); o.write('A'); o.write('V'); o.write('E');
            o.write('f'); o.write('m'); o.write('t'); o.write(' '); le(o, 16);
            o.write(1); o.write(0); o.write(2); o.write(0); le(o, rate); le(o, rate * 2 * bytes);
            o.write(2 * bytes); o.write(0); o.write(bits); o.write(0);
            o.write('d'); o.write('a'); o.write('t'); o.write('a'); le(o, data.length);
            o.write(data, 0, data.length);
            return o.toByteArray();
        }
    }

    static byte[] md5Native(int[] lr, int bits) {
        MessageDigest md = Crc.md("MD5");
        int bytes = bits / 8;
        for (int v : lr) for (int b = 0; b < bytes; b++) md.update((byte) (v >>> (8 * b)));
        return md.digest();
    }

    /** 一个带同步标记的合成 FLAC(右对齐样本 lr、24 或 16 bit)。blocksAfterSi:STREAMINFO 之后的块 */
    static byte[] mqaFlac(int[] lr, int bits, int origRate, boolean marker, Object[][] blocksAfterSi, byte[] prefix, byte[] suffix) {
        if (marker) Synth.embed(lr, bits, 1000, Synth.recordBits(Synth.rateCode(origRate), Synth.rateCode(44100), false, 0, new int[0][], 1), 8);
        byte[] fr = Synth.frames(lr, 44100, bits, 4096);
        byte[] si = Synth.streamInfo(44100, bits, lr.length / 2, 4096, md5Native(lr, bits));
        Object[][] all = new Object[blocksAfterSi.length + 1][];
        all[0] = new Object[] { FlacMeta.STREAMINFO, si };
        System.arraycopy(blocksAfterSi, 0, all, 1, blocksAfterSi.length);
        return Synth.file(prefix, all, fr, suffix);
    }

    static Object[] blk(int type, byte[] body) { return new Object[] { type, body }; }

    // ---------------------------------------------------------------------------
    // 内存里的可写文件(测修复用)
    // ---------------------------------------------------------------------------
    static class MemFile implements Rio, TagFile.Temp {
        byte[] d;
        int n;
        boolean deleted;
        long maxSize = -1;
        final String name;
        MemFile(String name, byte[] init) { this.name = name; d = init == null ? new byte[0] : init.clone(); n = d.length; }
        public long size() { return n; }
        public boolean read(long at, byte[] dst, int off, int k) {
            if (at < 0 || at > n || k > n - at) return false;
            System.arraycopy(d, (int) at, dst, off, k);
            return true;
        }
        public boolean write(long at, byte[] src, int off, int k) {
            if (maxSize >= 0 && at + k > maxSize) return false;            // 模拟盘满:超过这个长度的写一律失败
            long end = at + k;
            if (end > d.length) d = Arrays.copyOf(d, (int) Math.max(end, d.length * 2L));
            System.arraycopy(src, off, d, (int) at, k);
            if (end > n) n = (int) end;
            return true;
        }
        public boolean truncate(long size) { if (size < n) n = (int) size; return n == size; }
        public boolean sync() { return true; }
        public String path() { return name; }
        public void delete() { deleted = true; }
        public void close() {}
        byte[] bytes() { return Arrays.copyOf(d, n); }
    }

    static final class MemWorkspace implements TagFile.Workspace {
        final List<MemFile> temps = new ArrayList<MemFile>();
        MemFile journaled;
        TagFile.Journal j;
        boolean failJournal;
        public TagFile.Temp newTemp() { MemFile f = new MemFile("temp" + temps.size(), null); temps.add(f); return f; }
        public boolean journalBegin(TagFile.Temp t, TagFile.Journal jj) { if (failJournal) return false; journaled = (MemFile) t; j = jj; return true; }
        public void journalEnd(TagFile.Temp t) { journaled = null; }
        boolean allTempsDeleted() { for (MemFile f : temps) if (!f.deleted) return false; return true; }
    }

    // ---------------------------------------------------------------------------
    public static Result run(TagFile.Workspace unusedOnDisk) {
        log = new StringBuilder();
        pass = fail = 0;
        try {
            pure();
            detector();
            codec();
            repair();
            copies();
            texts();
        } catch (Throwable t) {
            fail++;
            log.append("  FAIL  exception: ").append(t).append('\n');
            for (StackTraceElement e : t.getStackTrace()) log.append("        at ").append(e).append('\n');
        }
        Result r = new Result();
        r.pass = pass;
        r.fail = fail;
        r.log = log.toString();
        return r;
    }

    static final String OURS = TagRule.DEFAULT_ENCODER_TAG;
    static final String OFFICIAL = "MQAEncode v1.1, 2.4.0+0 (a1b2c3d), F00D, Jan 01 2000";

    static byte[] metaOf(byte[] file) {
        FlacMeta.Layout l = new FlacMeta.Layout();
        FlacMeta.readLayout(new Rio.Mem(file), l);
        return Arrays.copyOfRange(file, (int) l.metaAt, (int) l.audioAt);
    }

    static FlacMeta.Plan plan(byte[] meta, int rate) {
        List<FlacMeta.Comments> vcs = FlacMeta.commentsOf(meta);
        String[] fin = FlacMeta.chooseFinal(vcs, rate, TagRule.DEFAULT);
        FlacMeta.Plan p = new FlacMeta.Plan();
        FlacMeta.PlanErr e = FlacMeta.planFix(meta, fin, 4, FlacMeta.SECTOR_BYTES, p);
        return e == FlacMeta.PlanErr.OK ? p : null;
    }

    static List<String> items(byte[] meta) {
        List<FlacMeta.Comments> v = FlacMeta.commentsOf(meta);
        return v == null || v.isEmpty() ? null : v.get(0).items;
    }

    private static void pure() {
        log.append("== 元数据块 / 注释 / 键 / 合格的定义 / PADDING 找平 ==\n");
        int[] lr = Synth.noise(5000, 24, 7);
        byte[] fr = Synth.frames(lr, 44100, 24, 4096);
        byte[] si = Synth.streamInfo(44100, 24, 5000, 4096, null);
        byte[] vc = Synth.vorbis("reference libFLAC 1.3.2 20170101", "TITLE=Curve", "ARTIST=Nobody", "encoder=Lavf58.76.100", "DATE=2026");
        byte[] f1 = Synth.file(null, new Object[][] { blk(0, si), blk(3, new byte[36]), blk(4, vc), blk(6, new byte[300]), blk(1, new byte[100]) }, fr, null);
        FlacMeta.Layout l = new FlacMeta.Layout();
        FlacMeta.LayoutResult r = FlacMeta.readLayout(new Rio.Mem(f1), l);
        long wantAudio = 4 + 38 + 40 + 4 + vc.length + 304 + 104;
        expect("1 五个块:类型、位置、长度、音频起点、STREAMINFO", r.err == FlacMeta.Err.OK && l.blocks.size() == 5 && l.blocks.get(2).at == 4 + 38 + 40
                && l.audioAt == wantAudio && l.sampleRate == 44100 && l.bits == 24 && l.channels == 2 && l.totalSamples == 5000 && l.blocks.get(4).last, null);
        byte[] f2 = Synth.file(Synth.id3v2(300), new Object[][] { blk(0, si) }, fr, null);
        FlacMeta.Layout l2 = new FlacMeta.Layout();
        expect("2 ID3v2 在 fLaC 前面:起点按 7 bit × 4 算", FlacMeta.readLayout(new Rio.Mem(f2), l2).err == FlacMeta.Err.OK && l2.id3Bytes == 310 && l2.metaAt == 314, null);
        byte[] good = Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("v", "A=1")) }, fr, null);
        byte[] badLen = good.clone();
        badLen[4 + 38 + 3]--;
        byte[] cut = Arrays.copyOf(good, 4 + 38 + 2);
        expect("3 截断 / 损坏的报出来,好文件通过", FlacMeta.readLayout(new Rio.Mem(badLen), new FlacMeta.Layout()).err == FlacMeta.Err.CORRUPT
                && FlacMeta.readLayout(new Rio.Mem(cut), new FlacMeta.Layout()).err == FlacMeta.Err.TRUNCATED
                && FlacMeta.readLayout(new Rio.Mem(good), new FlacMeta.Layout()).err == FlacMeta.Err.OK, null);
        byte[] body = Synth.vorbis("libFLAC 1.4.3", "A=1", "novalue", "B=x=y", "");
        FlacMeta.Comments c = FlacMeta.parseComments(body, 0, body.length);
        byte[] withTail = Arrays.copyOf(body, body.length + 2);
        withTail[body.length] = 1;
        withTail[body.length + 1] = (byte) 0xEE;
        FlacMeta.Comments ct = FlacMeta.parseComments(withTail, 0, withTail.length);
        expect("4 注释拆开再拼回逐字节相同(含块尾多出的字节)", c != null && Arrays.equals(FlacMeta.buildComments(c), body) && ct != null && ct.tail.length == 2
                && Arrays.equals(FlacMeta.buildComments(ct), withTail) && FlacMeta.parseComments(body, 0, body.length - 3) == null, null);
        expect("5 键名不分大小写、要整个相等", TagRule.keyEquals("EnCoDeR", "ENCODER") && !TagRule.keyEquals("ENCODERX", "ENCODER") && !TagRule.keyEquals("ENCODER ", "ENCODER")
                && FlacMeta.splitItem("novalue") == null && FlacMeta.splitItem("B=x=y")[1].equals("x=y"), null);
        TagRule.Seen s = new TagRule.Seen();
        int none = TagRule.problems(s, 352800);
        s.count[0] = s.count[1] = s.count[2] = 1;
        s.value[0] = OFFICIAL;
        s.value[1] = OURS;
        s.value[2] = "352800";
        int full = TagRule.problems(s, 352800), wrongRate = TagRule.problems(s, 96000);
        expect("6 合格的定义(前缀、只有数字的采样率、恰好一份)", none == 7 && full == 0 && wrongRate == TagRule.BAD_ORIG_RATE && TagRule.parseRate("096000") < 0
                && TagRule.parseRate(" 96000") < 0 && TagRule.parseRate("999999999") == 999999999 && TagRule.parseRate("1234567890") < 0
                && !TagRule.encoderValueOk("mqaencode x", TagRule.DEFAULT) && TagRule.tagsComplete(TagRule.BAD_FORMAT_ENCODER)
                && !TagRule.tagsComplete(TagRule.BAD_ENCODER | TagRule.BAD_FORMAT_ENCODER), "none=" + none + " full=" + full);
        byte[] m7 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("vendor X", "TITLE=Curve", "ARTIST=Nobody")), blk(1, new byte[400]) }, fr, null));
        FlacMeta.Plan p7 = plan(m7, 352800);
        expect("7 三个都没有:加在末尾,原有条目、厂商串不动,原地", p7 != null && p7.mode == FlacMeta.Mode.IN_PLACE && items(p7.newMeta).equals(Arrays.asList("TITLE=Curve",
                "ARTIST=Nobody", "ENCODER=" + OURS, "MQAENCODER=" + OURS, "ORIGINALSAMPLERATE=352800")) && FlacMeta.commentsOf(p7.newMeta).get(0).vendor.equals("vendor X"), null);
        byte[] m8 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, vc), blk(1, new byte[400]) }, fr, null));
        FlacMeta.Plan p8 = plan(m8, 192000);
        expect("8 小写的 encoder=Lavf:原位换掉,不多写一份", p8 != null && items(p8.newMeta).equals(Arrays.asList("TITLE=Curve", "ARTIST=Nobody", "ENCODER=" + OURS,
                "DATE=2026", "MQAENCODER=" + OURS, "ORIGINALSAMPLERATE=192000")), null);
        byte[] m9 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("v", "ENCODER=" + OFFICIAL, "TITLE=T", "encoder=Lavf",
                "ORIGINALSAMPLERATE=44100", "originalsamplerate=352800", "MQAENCODER=" + OFFICIAL, "MqaEncoder=" + OFFICIAL)), blk(1, new byte[64]) }, fr, null));
        FlacMeta.Plan p9 = plan(m9, 352800);
        expect("9 重复的键只留一份,合格的官方值留着", p9 != null && items(p9.newMeta).equals(Arrays.asList("ENCODER=" + OFFICIAL, "TITLE=T", "ORIGINALSAMPLERATE=352800",
                "MQAENCODER=" + OFFICIAL)), null);
        byte[] m10 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("v", "TITLE=T")) }, fr, null));
        FlacMeta.Plan p10 = plan(m10, 352800);
        expect("10 没有 PADDING:整文件重写", p10 != null && p10.mode == FlacMeta.Mode.REWRITE && !p10.sameSize, null);
        byte[] m11 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(1, new byte[1000]) }, fr, null));
        FlacMeta.Plan p11 = plan(m11, 352800);
        List<FlacMeta.MetaBlock> b11 = p11 == null ? null : FlacMeta.splitBlocks(p11.newMeta, p11.newMeta.length);
        expect("11 一个注释块都没有:新建一个放在 PADDING 之前,厂商串 tagrepair,长度找平", b11 != null && b11.size() == 3 && b11.get(1).type == FlacMeta.VORBIS_COMMENT
                && b11.get(2).type == FlacMeta.PADDING && p11.sameSize && FlacMeta.commentsOf(p11.newMeta).get(0).vendor.equals("tagrepair"), null);
        // 改动跨扇区:整文件重写;改动落在一个扇区:原地
        String longTitle = new String(new char[600]).replace('\0', 'x');
        byte[] m12 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("v", "TITLE=" + longTitle, "ENCODER=Lavf")), blk(1, new byte[400]) }, fr, null));
        FlacMeta.Plan p12 = plan(m12, 352800);
        expect("12 长度找平但改动跨 512 字节的扇区:整文件重写", p12 != null && p12.sameSize && p12.mode == FlacMeta.Mode.REWRITE, null);
        byte[] m13 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(4, Synth.vorbis("v", "ENCODER=" + OFFICIAL, "MQAENCODER=x", "ORIGINALSAMPLERATE=352800")),
                blk(1, new byte[10]) }, fr, null));
        expect("13 标签完整(一个编码器键合格就够):tagsComplete", TagRule.tagsComplete(TagRule.problems(FlacMeta.seenInMeta(m13), 352800)), null);
        // PADDING 缺 1–3 字节:用光块体、块头留着,再找下一块
        byte[] m14 = metaOf(Synth.file(null, new Object[][] { blk(0, si), blk(1, new byte[2]), blk(4, Synth.vorbis("v")), blk(1, new byte[2]) }, fr, null));
        FlacMeta.Plan p14 = plan(m14, 352800);
        expect("14 PADDING 不够:能找平就原样长度,找不平就整文件重写", p14 != null && p14.mode == FlacMeta.Mode.REWRITE, null);
    }

    private static void detector() {
        log.append("== 同步标记检测 ==\n");
        int[] lr = Synth.noise(20000, 24, 3);
        Synth.embed(lr, 24, 500, Synth.recordBits(3, 9, false, 0, new int[0][], 1), 8);
        SyncDetect d = new SyncDetect();
        int[] left = leftAlign(lr, 24);
        d.feed(left, 0, 20000);
        expect("D1 找到标记,读到原始采样率 352800,控制位是第 8 位", d.markerFound() && d.originalRate() == 352800 && d.controlBit() == 8, "rate=" + d.originalRate());
        int[] lr2 = Synth.noise(20000, 24, 5);
        Synth.embed(lr2, 24, 300, Synth.recordBits(10, 1, true, 12345, new int[][] { { 1, 13 }, { 7, 40 } }, 9), 13);
        SyncDetect d2 = new SyncDetect();
        int[] la2 = leftAlign(lr2, 24);
        for (int at = 0; at < 20000; at += 37) d2.feed(la2, at * 2, Math.min(37, 20000 - at));
        expect("D2 第 13 位、带位置和两个附加项、分很多次喂:192000", d2.markerFound() && d2.originalRate() == 192000 && d2.controlBit() == 13, "rate=" + d2.originalRate());
        byte[] bad = Synth.recordBits(3, 9, false, 0, new int[0][], 1);
        bad[bad.length - 1] ^= 1;
        int[] lr3 = Synth.noise(20000, 24, 11);
        Synth.embed(lr3, 24, 100, bad, 8);
        SyncDetect d3 = new SyncDetect();
        d3.feed(leftAlign(lr3, 24), 0, 20000);
        expect("D3 校验不对:找到标记、读不到采样率", d3.markerFound() && d3.originalRate() == 0, null);
        int[] lr4 = Synth.noise(20000, 16, 13);
        Synth.embed(lr4, 16, 100, Synth.recordBits(1, 8, false, 0, new int[0][], 1), 8);
        SyncDetect d4 = new SyncDetect();
        d4.feed(leftAlign(lr4, 16), 0, 20000);
        expect("D4 16 bit 样本(最低位):88200", d4.markerFound() && d4.originalRate() == 88200, null);
        SyncDetect d5 = new SyncDetect();
        d5.feed(leftAlign(Synth.noise(20000, 24, 17), 24), 0, 20000);
        expect("D5 普通噪声:没有标记", !d5.markerFound(), null);
        expect("D6 采样率的码", SyncDetect.rateFromCode(3) == 352800 && SyncDetect.rateFromCode(10) == 192000 && SyncDetect.rateFromCode(24) == 0, null);
    }

    static int[] leftAlign(int[] lr, int bits) {
        int[] o = new int[lr.length];
        for (int i = 0; i < lr.length; i++) o[i] = lr[i] << (32 - bits);
        return o;
    }

    private static void codec() {
        log.append("== 解码 / 编码 ==\n");
        int[] lr = Synth.noise(9000, 24, 21);
        byte[] f = mqaFlac(lr.clone(), 24, 352800, false, new Object[][] { blk(1, new byte[16]) }, null, null);
        Probe.Info info = Probe.inspect(new Rio.Mem(f));
        final MessageDigest md = Crc.md("MD5");
        Probe.Pcm p = Probe.decodeAll(new Rio.Mem(f), info, null, null);
        Crc.updateInt32(md, leftAlign(lr, 24), lr.length, new byte[1 << 16]);
        expect("C1 解码(VERBATIM 子帧,最后一块短):样本、个数、无错", p.md5.equals(Crc.hex(md.digest())) && p.frames == 9000 && p.errors == 0, "frames=" + p.frames);
        byte[] broken = f.clone();
        broken[broken.length - 100] ^= 0x40;                  // 最后一帧里改一位:帧尾 CRC 对不上
        Probe.Pcm pb = Probe.decodeAll(new Rio.Mem(broken), Probe.inspect(new Rio.Mem(broken)), null, null);
        expect("C2 帧 CRC 不对的帧算错、不出样本", pb.errors >= 1 && pb.frames == 4096 * 2, "frames=" + pb.frames + " errors=" + pb.errors);
        // 编码 → 解码往返:噪声、正弦样的慢变化、常数、只有 8 个零位的 24 bit
        int[][] cases = new int[7][];
        cases[0] = Synth.noise(10000, 16, 3);
        cases[1] = new int[20000];
        for (int i = 0; i < 10000; i++) { cases[1][2 * i] = (int) (Math.sin(i / 30.0) * 3000000); cases[1][2 * i + 1] = (int) (Math.sin(i / 31.0) * 2900000); }
        cases[2] = new int[8200];
        Arrays.fill(cases[2], -77);
        cases[3] = Synth.noise(5000, 16, 9);
        for (int i = 0; i < cases[3].length; i++) cases[3][i] <<= 8;
        cases[4] = Synth.noise(9000, 24, 19);
        int[] tiny = Synth.noise(9000, 12, 23);
        for (int i = 0; i < 9000; i++) cases[4][2 * i + 1] = cases[4][2 * i] + tiny[2 * i];          // 左右几乎一样:中差最省
        cases[5] = Synth.noise(9000, 16, 29);                                                             // 配 Rice 参数 0:一元码很长
        cases[6] = Synth.noise(9000, 24, 37);                                                             // 配 escape 分区
        int[] bitsOf = { 16, 24, 24, 24, 24, 16, 24 };
        int midSideBefore = 0;
        boolean all = true;
        StringBuilder note = new StringBuilder();
        for (int k = 0; k < cases.length; k++) {
            int[] x = cases[k];
            if (k == 4) midSideBefore = FlacEncoder.assignCount[10];
            int n = x.length / 2, bits = bitsOf[k];
            FlacEncoder.testForceK0 = k == 5;
            FlacEncoder.testEscape = k == 6;
            FlacEncoder enc = new FlacEncoder(44100, 2, bits);
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            Synth.minFrame = 1; Synth.maxFrame = 1 << 20;
            for (int at = 0; at < n; at += FlacEncoder.BLOCK) {
                int m = Math.min(FlacEncoder.BLOCK, n - at);
                int[][] ch = new int[2][m];
                for (int i = 0; i < m; i++) { ch[0][i] = x[2 * (at + i)]; ch[1][i] = x[2 * (at + i) + 1]; }
                byte[] fr = enc.encode(ch, m);
                o.write(fr, 0, enc.lastLength());
            }
            byte[] file = Synth.file(null, new Object[][] { blk(0, Synth.streamInfo(44100, bits, n, FlacEncoder.BLOCK, md5Native(x, bits))) }, o.toByteArray(), null);
            Probe.Info ii = Probe.inspect(new Rio.Mem(file));
            Probe.Pcm pp = Probe.decodeAll(new Rio.Mem(file), ii, null, null);
            MessageDigest m2 = Crc.md("MD5");
            Crc.updateInt32(m2, leftAlign(x, bits), x.length, new byte[1 << 16]);
            boolean ok = ii.opened && pp.errors == 0 && pp.frames == n && pp.md5.equals(Crc.hex(m2.digest()));
            if (!ok) { all = false; note.append("case ").append(k).append(" frames=").append(pp.frames).append(" errors=").append(pp.errors).append("; "); }
        }
        FlacEncoder.testForceK0 = false;
        FlacEncoder.testEscape = false;
        if (FlacEncoder.assignCount[10] == midSideBefore) { all = false; note.append("mid/side never chosen; "); }
        expect("C3 自己的编码器编出来、自己的解码器解回去逐个样本相同(七种信号,含中差、Rice 参数 0 的长一元码、escape 分区)", all, note.toString());
        // C4 缓冲(4 MB)边上夹了一段杂字节:算错、跳过,后面的帧照常解出来
        int[] la = Synth.noise(170 * 4096, 24, 71), lb = Synth.noise(30 * 4096, 24, 73);
        byte[] fa = Synth.frames(la, 44100, 24, 4096), fb = Synth.frames(lb, 44100, 24, 4096);
        byte[] junk = { (byte) 0xFF, (byte) 0xF8, 1, 2, 3, (byte) 0xFF, (byte) 0xF9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
        byte[] audio = new byte[fa.length + junk.length + fb.length];
        System.arraycopy(fa, 0, audio, 0, fa.length);
        System.arraycopy(junk, 0, audio, fa.length, junk.length);
        System.arraycopy(fb, 0, audio, fa.length + junk.length, fb.length);
        byte[] big = Synth.file(null, new Object[][] { blk(0, Synth.streamInfo(44100, 24, 200 * 4096, 4096, null)) }, audio, null);
        Probe.Pcm pj = Probe.decodeAll(new Rio.Mem(big), Probe.inspect(new Rio.Mem(big)), null, null);
        expect("C4 4 MB 缓冲边上夹着杂字节:跳过(记一处错),前后的帧一个不少", pj.frames == 200 * 4096 && pj.errors >= 1, "frames=" + pj.frames + " errors=" + pj.errors + " at " + fa.length);
    }

    private static void repair() {
        log.append("== 修复 ==\n");
        // R1 原地
        byte[] f = mqaFlac(Synth.noise(9000, 24, 31), 24, 352800, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=T")), blk(1, new byte[300]) }, null, null);
        MemFile mf = new MemFile("a.flac", f);
        MemWorkspace ws = new MemWorkspace();
        TagFile.FixReport r = TagFile.fix(mf, true, new TagFile.FixOptions(), ws);
        TagFile.Scan after = TagFile.scan(mf, TagRule.DEFAULT);
        FlacMeta.Layout l0 = new FlacMeta.Layout(), l1 = new FlacMeta.Layout();
        FlacMeta.readLayout(new Rio.Mem(f), l0);
        FlacMeta.readLayout(mf, l1);
        boolean audioSame = Arrays.equals(Arrays.copyOfRange(f, (int) l0.audioAt, f.length), Arrays.copyOfRange(mf.bytes(), (int) l1.audioAt, mf.n));
        expect("R1 原地修复:修好、标签完整、音频字节不变、长度不变", r.outcome == TagFile.Outcome.FIXED && r.inPlace && after.state == Classify.State.COMPLETE
                && audioSame && mf.n == f.length && after.seen.value[2].equals("352800"), r.code + " " + r.why.zh);
        TagFile.FixReport again = TagFile.fix(mf, true, new TagFile.FixOptions(), ws);
        expect("R2 再修一次:标签完整,一个字节都不动", again.outcome == TagFile.Outcome.UNCHANGED, null);
        // R3 整文件重写(没有 PADDING),前面带 ID3v2、后面带 ID3v1
        byte[] g = mqaFlac(Synth.noise(9000, 24, 33), 24, 384000, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=T")) }, Synth.id3v2(50), id3v1());
        MemFile mg = new MemFile("b.flac", g);
        MemWorkspace ws2 = new MemWorkspace();
        TagFile.FixReport r3 = TagFile.fix(mg, true, new TagFile.FixOptions(), ws2);
        TagFile.Scan a3 = TagFile.scan(mg, TagRule.DEFAULT);
        expect("R3 整文件重写(没有 PADDING、前有 ID3v2、后有 ID3v1):修好、临时文件删了、没留登记", r3.outcome == TagFile.Outcome.FIXED && !r3.inPlace
                && a3.state == Classify.State.COMPLETE && a3.seen.value[2].equals("384000") && ws2.allTempsDeleted() && ws2.journaled == null
                && Arrays.equals(Arrays.copyOfRange(g, 0, 60), Arrays.copyOfRange(mg.bytes(), 0, 60)), r3.code + " " + r3.why.zh);
        // R4 没有同步标记:不碰
        byte[] h = mqaFlac(Synth.noise(9000, 24, 35), 24, 352800, false, new Object[][] { blk(1, new byte[300]) }, null, null);
        MemFile mh = new MemFile("c.flac", h);
        TagFile.FixReport r4 = TagFile.fix(mh, true, new TagFile.FixOptions(), new MemWorkspace());
        expect("R4 不是 MQA:跳过,一个字节不动", r4.outcome == TagFile.Outcome.SKIPPED && Arrays.equals(mh.bytes(), h), null);
        // R5 写入失败(原地):原样
        MemFile m5 = new MemFile("d.flac", f);
        TagFile.FixOptions o5 = new TagFile.FixOptions();
        o5.test.failWrite = true;
        TagFile.FixReport r5 = TagFile.fix(m5, true, o5, new MemWorkspace());
        expect("R5 写入失败:报失败,文件原样", r5.outcome == TagFile.Outcome.FAILED && Arrays.equals(m5.bytes(), f), r5.code);
        // R6 修之前的指纹被换掉(模拟写完核对对不上):原地写回原来的字节
        MemFile m6 = new MemFile("e.flac", f);
        TagFile.FixOptions o6 = new TagFile.FixOptions();
        o6.test.expect = new String[] { "0000", "1111" };
        TagFile.FixReport r6 = TagFile.fix(m6, true, o6, new MemWorkspace());
        expect("R6 写完核对不过(原地):把原来的字节写回去,文件原样", r6.outcome == TagFile.Outcome.FAILED && r6.code.equals("verify_failed") && Arrays.equals(m6.bytes(), f), r6.code);
        // R7 同样的事,整文件重写:原文件根本没动
        MemFile m7 = new MemFile("f.flac", g);
        TagFile.FixOptions o7 = new TagFile.FixOptions();
        o7.test.expect = new String[] { "0000", "1111" };
        MemWorkspace ws7 = new MemWorkspace();
        TagFile.FixReport r7 = TagFile.fix(m7, true, o7, ws7);
        expect("R7 核对临时文件不过(整文件重写):原文件没动、临时文件删了", r7.outcome == TagFile.Outcome.FAILED && Arrays.equals(m7.bytes(), g) && ws7.allTempsDeleted(), r7.code);
        // R8 取消:写临时文件的时候
        MemFile m8 = new MemFile("g.flac", g);
        TagFile.FixOptions o8 = new TagFile.FixOptions();
        final boolean[] stop = { false };
        o8.cancel = new TagFile.Cancel() { public boolean requested() { return stop[0]; } };
        o8.test.afterTempWrite = new Runnable() { public void run() { stop[0] = true; } };
        MemWorkspace ws8 = new MemWorkspace();
        TagFile.FixReport r8 = TagFile.fix(m8, true, o8, ws8);
        expect("R8 写完临时文件时取消:停下,原文件没动、临时文件删了", r8.outcome == TagFile.Outcome.CANCELLED && Arrays.equals(m8.bytes(), g) && ws8.allTempsDeleted(), null);
        // R9 写回之前原文件被改了:不写
        final MemFile m9 = new MemFile("h.flac", g);
        TagFile.FixOptions o9 = new TagFile.FixOptions();
        o9.test.beforeOverwrite = new Runnable() { public void run() { m9.d[(int) (m9.n - 5)] ^= 1; } };
        TagFile.FixReport r9 = TagFile.fix(m9, true, o9, new MemWorkspace());
        byte[] g9 = g.clone();
        g9[g9.length - 5] ^= 1;
        TagFile.Scan s9 = TagFile.scan(m9, TagRule.DEFAULT);
        expect("R9 写回之前原文件被别的程序改了:不写,别人改的留着", r9.outcome == TagFile.Outcome.REFUSED && Arrays.equals(m9.bytes(), g9) && s9.state == Classify.State.REPAIRABLE, r9.code);
        // R10 写回途中被打断(进程没了):登记还在、核对过的新版本还在;下次接着写完 = 修好的文件
        MemFile m10 = new MemFile("i.flac", g);
        TagFile.FixOptions o10 = new TagFile.FixOptions();
        o10.test.duringOverwrite = new Runnable() { public void run() { throw new TagFile.Interrupt(); } };
        MemWorkspace ws10 = new MemWorkspace();
        boolean interrupted = false;
        try { TagFile.fix(m10, true, o10, ws10); } catch (TagFile.Interrupt e) { interrupted = true; }
        boolean halfway = interrupted && ws10.journaled != null && !ws10.journaled.deleted;
        TagFile.Recover resumed = halfway ? TagFile.finishOverwrite(ws10.journaled, m10, ws10.j) : TagFile.Recover.FAILED;
        TagFile.Scan s10 = TagFile.scan(m10, TagRule.DEFAULT);
        MemFile ref = new MemFile("ref.flac", g);
        TagFile.fix(ref, true, new TagFile.FixOptions(), new MemWorkspace());
        expect("R10 写回途中被打断:登记还在,接着写完与一次修好的逐字节相同", halfway && resumed == TagFile.Recover.DONE && s10.state == Classify.State.COMPLETE && Arrays.equals(m10.bytes(), ref.bytes()), null);
        // R11 只读:不写
        MemFile m11 = new MemFile("j.flac", f);
        TagFile.FixReport r11 = TagFile.fix(m11, false, new TagFile.FixOptions(), new MemWorkspace());
        expect("R11 没有写权限:报出来,不写", r11.outcome == TagFile.Outcome.REFUSED && Arrays.equals(m11.bytes(), f), null);
        // R12 音频少了一帧:不碰
        byte[] shortAudio = Arrays.copyOf(f, f.length - 2000);
        MemFile m12 = new MemFile("k.flac", shortAudio);
        TagFile.FixReport r12 = TagFile.fix(m12, true, new TagFile.FixOptions(), new MemWorkspace());
        expect("R12 音频不完整:拒绝,不碰", r12.outcome == TagFile.Outcome.REFUSED && Arrays.equals(m12.bytes(), shortAudio), r12.code);
        shrinkRewrite();
        writeBackFailures();
        // R19 原地写完、核对的时候出了意外(抛异常):一律当核对不过,把原来的字节写回去
        byte[] f19 = mqaFlac(Synth.noise(9000, 24, 81), 24, 352800, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=T")), blk(1, new byte[300]) }, null, null);
        MemFile m19 = new MemFile("r19.flac", f19);
        TagFile.FixOptions o19 = new TagFile.FixOptions();
        o19.test.afterWrite = new Runnable() { public void run() { throw new IllegalStateException("simulated"); } };
        TagFile.FixReport r19 = TagFile.fix(m19, true, o19, new MemWorkspace());
        expect("R19 原地写完之后出了意外:写回原来的字节,文件原样", r19.outcome == TagFile.Outcome.FAILED && Arrays.equals(m19.bytes(), f19), r19.code);
    }

    static void shrinkRewrite() {
        String bad16 = "Lavf61.1.100xxxx";
        byte[] f = mqaFlac(Synth.noise(9000, 24, 51), 24, 352800, true, new Object[][] { blk(4, Synth.vorbis("v", "ENCODER=" + bad16, "MQAENCODER=" + OURS, "MQAENCODER=" + OURS)) }, null, null);
        MemFile m = new MemFile("s.flac", f);
        TagFile.FixReport r = TagFile.fix(m, true, new TagFile.FixOptions(), new MemWorkspace());
        TagFile.Scan s = TagFile.scan(m, TagRule.DEFAULT);
        expect("R13 整文件重写、新文件比原来短(2 字节):截到新长度,标签完整", r.outcome == TagFile.Outcome.FIXED && !r.inPlace && m.n == f.length - 2
                && s.state == Classify.State.COMPLETE && s.seen.count[1] == 1, r.code + " len " + m.n + " vs " + f.length);
    }

    /** 写回出错的几种:盘满、写到一半出错要恢复原样、登记没落盘、接着写之前发现那个位置换了文件、临时文件坏了 */
    static void writeBackFailures() {
        byte[] g = mqaFlac(Synth.noise(9000, 24, 61), 24, 352800, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=T")) }, null, null);
        MemFile full = new MemFile("full.flac", g);
        full.maxSize = g.length;
        MemWorkspace w1 = new MemWorkspace();
        TagFile.FixReport r1 = TagFile.fix(full, true, new TagFile.FixOptions(), w1);
        expect("R14 要变长、盘满:写回失败,原文件逐字节原样,临时文件删了、登记清了", r1.outcome == TagFile.Outcome.FAILED && Arrays.equals(full.bytes(), g)
                && w1.allTempsDeleted() && w1.journaled == null, r1.code + " " + r1.why.zh);
        final MemFile flaky = new MemFile("flaky.flac", g) {
            int bigWrites = 0;
            public boolean write(long at, byte[] src, int off, int k) {
                if (k >= 1000 && bigWrites++ < 2) return false;           // 写回的两次都在第一块就失败;之后的(恢复原样)正常
                return super.write(at, src, off, k);
            }
        };
        MemWorkspace w2 = new MemWorkspace();
        TagFile.FixReport r2 = TagFile.fix(flaky, true, new TagFile.FixOptions(), w2);
        expect("R15 写回两次都出错:恢复成原样(逐字节),报失败", r2.outcome == TagFile.Outcome.FAILED && Arrays.equals(flaky.bytes(), g) && w2.journaled == null, r2.code + " " + r2.why.zh);
        MemFile m3 = new MemFile("nj.flac", g);
        MemWorkspace w3 = new MemWorkspace();
        w3.failJournal = true;
        TagFile.FixReport r3 = TagFile.fix(m3, true, new TagFile.FixOptions(), w3);
        expect("R16 登记没落盘:不写回,原文件原样,临时文件删了", r3.outcome == TagFile.Outcome.FAILED && Arrays.equals(m3.bytes(), g) && w3.allTempsDeleted(), r3.code);
        MemFile m4 = new MemFile("x.flac", g);
        TagFile.FixOptions o4 = new TagFile.FixOptions();
        o4.test.duringOverwrite = new Runnable() { public void run() { throw new TagFile.Interrupt(); } };
        MemWorkspace w4 = new MemWorkspace();
        try { TagFile.fix(m4, true, o4, w4); } catch (TagFile.Interrupt e) { }
        byte[] other = g.clone();                                  // 同样长、开头不同的另一个文件
        for (int i = 0; i + 7 <= other.length; i++) if (other[i] == (byte) 'T' && other[i + 1] == (byte) 'I' && other[i + 6] == (byte) 'T') { other[i + 6] = (byte) 'U'; break; }
        MemFile replaced = new MemFile("x.flac", other);
        TagFile.Recover rr = w4.journaled == null ? null : TagFile.finishOverwrite(w4.journaled, replaced, w4.j);
        expect("R17 接着写之前,那个位置已经换成了别的文件:不碰", rr == TagFile.Recover.NOT_OURS && Arrays.equals(replaced.bytes(), other), String.valueOf(rr));
        // R20 文件开头很大(1.5 MB 的图片块,写回要好几块才写完开头):写到第一块之后被打断,接着写仍认得出是自己的
        byte[] pic = new byte[1500000];
        for (int i = 0; i < pic.length; i++) pic[i] = (byte) (i * 31 + 7);
        byte[] gp = mqaFlac(Synth.noise(9000, 24, 65), 24, 352800, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=T")), blk(6, pic) }, null, null);
        MemFile mp = new MemFile("pic.flac", gp);
        TagFile.FixOptions op = new TagFile.FixOptions();
        op.test.duringOverwrite = new Runnable() { public void run() { throw new TagFile.Interrupt(); } };
        MemWorkspace wp = new MemWorkspace();
        try { TagFile.fix(mp, true, op, wp); } catch (TagFile.Interrupt e) { }
        TagFile.Recover rp = wp.journaled == null ? null : TagFile.finishOverwrite(wp.journaled, mp, wp.j);
        MemFile refp = new MemFile("refp.flac", gp);
        TagFile.fix(refp, true, new TagFile.FixOptions(), new MemWorkspace());
        expect("R20 开头大于一块、写到一半被打断:认得出,接着写完与一次修好的相同", rp == TagFile.Recover.DONE && Arrays.equals(mp.bytes(), refp.bytes()), String.valueOf(rp));
        // R22 恢复原样做到一半被打断(开头前一截是修之前的、后面是修之后的):仍认得出,接着写完
        if (wp.j != null) {
            byte[] mixed = refp.bytes().clone();
            System.arraycopy(gp, 0, mixed, 0, 1 << 20);
            MemFile mm = new MemFile("mix.flac", mixed);
            TagFile.Recover rm = TagFile.finishOverwrite(wp.journaled, mm, wp.j);
            expect("R22 恢复原样到一半被打断:认得出,接着写完与一次修好的相同", rm == TagFile.Recover.DONE && Arrays.equals(mm.bytes(), refp.bytes()), String.valueOf(rm));
        }
        // R21 接着写的时候又写不成:恢复成原样,报 ORIGINAL(登记可以删)
        MemFile mo = new MemFile("o.flac", gp);
        TagFile.FixOptions oo = new TagFile.FixOptions();
        oo.test.duringOverwrite = new Runnable() { public void run() { throw new TagFile.Interrupt(); } };
        MemWorkspace wo = new MemWorkspace();
        try { TagFile.fix(mo, true, oo, wo); } catch (TagFile.Interrupt e) { }
        final int[] bad = { 2 };
        MemFile moFlaky = new MemFile("o.flac", mo.bytes()) {
            public boolean write(long at, byte[] src, int off, int k) { if (k >= 1000 && bad[0]-- > 0) return false; return super.write(at, src, off, k); }
        };
        TagFile.Recover ro = wo.journaled == null ? null : TagFile.finishOverwrite(wo.journaled, moFlaky, wo.j);
        expect("R21 接着写又写不成:恢复成原样(逐字节)、报 ORIGINAL", ro == TagFile.Recover.ORIGINAL && Arrays.equals(moFlaky.bytes(), gp), String.valueOf(ro));
        if (w4.journaled != null) w4.journaled.d[w4.journaled.n - 7] ^= 1;
        byte[] before = m4.bytes();
        TagFile.Recover rb = w4.journaled == null ? null : TagFile.finishOverwrite(w4.journaled, m4, w4.j);
        expect("R18 登记的临时文件坏了:不写", rb == TagFile.Recover.TEMP_BAD && Arrays.equals(m4.bytes(), before), String.valueOf(rb));
    }

    static byte[] id3v1() {
        byte[] t = new byte[128];
        t[0] = 'T'; t[1] = 'A'; t[2] = 'G';
        return t;
    }

    static final class MemTarget implements CopyFlac.Target {
        final String name;
        MemFile made;
        boolean existed;
        MemTarget(String n, boolean exists) { name = n; existed = exists; }
        public String name() { return name; }
        public boolean exists() { return existed || made != null; }
        public Rio create() { made = new MemFile(name, null); return made; }
        public void discard() { made = null; }
        public void finish() {}
    }

    private static void copies() {
        log.append("== 另存 ==\n");
        int[] lr = Synth.noise(9000, 24, 41);
        Synth.embed(lr, 24, 700, Synth.recordBits(3, 9, false, 0, new int[0][], 1), 8);
        byte[] wav = Synth.wav(lr, 44100, 24);
        MemTarget t = new MemTarget("x.flac", false);
        MemWorkspace ws = new MemWorkspace();
        CopyFlac.Report r = CopyFlac.copy(new Rio.Mem(wav), CopyFlac.Kind.WAV_TO_FLAC, t, new CopyFlac.Options(), ws);
        boolean ok = r.outcome == CopyFlac.Outcome.CREATED && t.made != null;
        TagFile.Scan s = ok ? TagFile.scan(t.made, TagRule.DEFAULT) : null;
        Probe.Pcm p = ok ? Probe.decodeAll(t.made, s.info, null, null) : null;
        Probe.Pcm src = Probe.decodeAll(new Rio.Mem(wav), Probe.inspect(new Rio.Mem(wav)), null, null);
        expect("K1 WAV 另存为 FLAC:新文件标签完整、采样逐个相同、临时文件删了", ok && s.state == Classify.State.COMPLETE && p.md5.equals(src.md5) && ws.allTempsDeleted(), r.code + " " + r.why.zh);
        int[] l16 = Synth.noise(9000, 16, 43);
        byte[] f16 = mqaFlac(l16, 16, 88200, true, new Object[][] { blk(4, Synth.vorbis("v", "TITLE=x")), blk(1, new byte[100]) }, null, null);
        MemTarget t2 = new MemTarget("x.24bit.flac", false);
        CopyFlac.Report r2 = CopyFlac.copy(new Rio.Mem(f16), CopyFlac.Kind.TO_24BIT, t2, new CopyFlac.Options(), new MemWorkspace());
        TagFile.Scan s2 = t2.made != null ? TagFile.scan(t2.made, TagRule.DEFAULT) : null;
        expect("K2 16 bit 另存 24 bit:24 bit、标签完整、原始采样率 88200、普通标签带过去", r2.outcome == CopyFlac.Outcome.CREATED && s2 != null && s2.info.bits == 24
                && s2.state == Classify.State.COMPLETE && s2.info.origRate == 88200, r2.code + " " + r2.why.zh);
        MemTarget t3 = new MemTarget("x.flac", true);
        CopyFlac.Report r3 = CopyFlac.copy(new Rio.Mem(wav), CopyFlac.Kind.WAV_TO_FLAC, t3, new CopyFlac.Options(), new MemWorkspace());
        expect("K3 目标已经有文件:不覆盖、不写", r3.outcome == CopyFlac.Outcome.REFUSED && t3.made == null, null);
        MemTarget t4 = new MemTarget("x.flac", false);
        CopyFlac.Options o4 = new CopyFlac.Options();
        o4.tamperTags = new String[] { "Lavf", "Lavf", "352800" };
        MemWorkspace ws4 = new MemWorkspace();
        CopyFlac.Report r4 = CopyFlac.copy(new Rio.Mem(wav), CopyFlac.Kind.WAV_TO_FLAC, t4, o4, ws4);
        expect("K4 写进去的标签不对(模拟):核对不过,不留新文件", r4.outcome == CopyFlac.Outcome.FAILED && t4.made == null && ws4.allTempsDeleted(), r4.code);
        MemTarget t5 = new MemTarget("y.flac", false);
        byte[] plain = Synth.wav(Synth.noise(9000, 24, 45), 44100, 24);
        CopyFlac.Report r5 = CopyFlac.copy(new Rio.Mem(plain), CopyFlac.Kind.WAV_TO_FLAC, t5, new CopyFlac.Options(), new MemWorkspace());
        expect("K5 没有同步标记的 WAV:不转", r5.outcome == CopyFlac.Outcome.SKIPPED && t5.made == null, null);
        final MemTarget t6 = new MemTarget("z.flac", false);
        CopyFlac.Options o6 = new CopyFlac.Options();
        final MemWorkspace ws6 = new MemWorkspace();
        o6.afterWrite = new Runnable() { public void run() { MemFile tmp = ws6.temps.get(0); tmp.d[tmp.n - 300] ^= 0x10; } };
        CopyFlac.Report r6 = CopyFlac.copy(new Rio.Mem(wav), CopyFlac.Kind.WAV_TO_FLAC, t6, o6, ws6);
        expect("K6 临时文件写坏了(模拟):核对不过,不留新文件", r6.outcome == CopyFlac.Outcome.FAILED && t6.made == null && ws6.allTempsDeleted(), r6.code);
        // K7 WAV 的 LIST/INFO 放在 data 块之后(FFmpeg 也读得到):普通标签照样带到新文件里
        byte[] info = { 'I', 'N', 'F', 'O', 'I', 'N', 'A', 'M', 5, 0, 0, 0, 'S', 'o', 'n', 'g', 0, 0 };
        byte[] wav2 = Arrays.copyOf(wav, wav.length + 8 + info.length);
        int p7 = wav.length;
        wav2[p7] = 'L'; wav2[p7 + 1] = 'I'; wav2[p7 + 2] = 'S'; wav2[p7 + 3] = 'T';
        wav2[p7 + 4] = (byte) info.length;
        System.arraycopy(info, 0, wav2, p7 + 8, info.length);
        int riff = wav2.length - 8;
        wav2[4] = (byte) riff; wav2[5] = (byte) (riff >> 8); wav2[6] = (byte) (riff >> 16); wav2[7] = (byte) (riff >> 24);
        MemTarget t7 = new MemTarget("k7.flac", false);
        CopyFlac.Report r7 = CopyFlac.copy(new Rio.Mem(wav2), CopyFlac.Kind.WAV_TO_FLAC, t7, new CopyFlac.Options(), new MemWorkspace());
        boolean titled = false;
        if (t7.made != null) {
            FlacMeta.Layout l7 = new FlacMeta.Layout();
            FlacMeta.readLayout(t7.made, l7);
            List<FlacMeta.Comments> c7 = FlacMeta.commentsOf(Rio.Util.range(t7.made, l7.metaAt, l7.audioAt - l7.metaAt));
            titled = c7 != null && c7.get(0).items.contains("TITLE=Song");
        }
        expect("K7 data 块之后的 LIST/INFO 也读:标题带到新文件里", r7.outcome == CopyFlac.Outcome.CREATED && titled, r7.code);
    }

    private static void texts() {
        log.append("== 文字 ==\n");
        boolean ok = true;
        StringBuilder bad = new StringBuilder();
        for (Msg.Entry e : Msg.all()) {
            boolean zhHan = false, enHan = false;
            for (char c : e.zh.toCharArray()) if (c >= 0x4E00 && c <= 0x9FFF) zhHan = true;
            for (char c : e.en.toCharArray()) if (c >= 0x4E00 && c <= 0x9FFF) enHan = true;
            boolean places = true;
            for (int i = 0; i <= 9; i++) if (e.en.contains("{" + i + "}") && !e.zh.contains("{" + i + "}")) places = false;
            boolean letters = false;
            for (char c : e.en.toCharArray()) if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters = true;
            if (e.en.isEmpty() || e.zh.isEmpty() || (letters && !zhHan) || enHan || !places) { ok = false; bad.append(e.name).append(' '); }
        }
        expect("T1 每一句英文、中文两半都有;英文有字的中文就得有汉字、英文里没有中文字符;英文用到的位置号中文都有", ok, bad.toString());
    }
}
