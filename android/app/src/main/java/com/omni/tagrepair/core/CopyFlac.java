package com.omni.tagrepair.core;

import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

/**
 * 另存:新写一个 FLAC(原文件只读,从不修改)。对应 Windows 版 src/copy_flac.cpp,两种、守的规矩相同:
 *   WAV_TO_FLAC  带同步标记的 WAV → <文件名>.flac(采样率、声道数、位深相同,无损编码,带三个识别标签和 WAV 里的普通标签)
 *   TO_24BIT     带同步标记的 16 bit 文件(WAV 或 FLAC)→ <文件名>.24bit.flac(每个采样原样放在高 16 位,低 8 位补零)
 *   · 没有同步标记的绝不转;从不覆盖已有的文件;
 *   · 先写进本应用目录里的临时文件,从头到尾解一遍核对(采样个数、全部采样的 MD5 与源相同;24 bit 的另外逐个看低 8 位是零、
 *     高 16 位就是源的采样;文件头里的 MD5 就是这些采样的;新文件扫描出来"有同步标记、标签完整"),通过了才放到目标位置,
 *     放好之后再逐字节核对一遍;有一处对不上就不留。
 * 音频帧是本工具自己的 FlacEncoder 编的(Windows 版用 FFmpeg 的编码器),所以新文件的音频帧字节与 Windows 版的不同,
 * 解出来的采样相同、元数据块相同。
 */
public final class CopyFlac {
    private CopyFlac() {}

    public enum Kind { WAV_TO_FLAC, TO_24BIT }
    public enum Outcome { CREATED, WOULD_CREATE, SKIPPED, REFUSED, FAILED, CANCELLED }

    public static final int PAD_TO_24 = 8;
    static final int NEW_PADDING = 2048;

    /** 新文件放哪:安卓上是用户选的文件夹里的一个新文档 */
    public interface Target {
        String name();
        boolean exists();
        Rio create();             // 建不了返回 null
        void discard();           // 建了、但没核对过:删掉
        void finish();            // 放好、核对过了:关掉(有的文件提供者关的时候才真正存下)
    }

    public static final class Options {
        public boolean dryRun;
        public TagRule.Rule rule = TagRule.DEFAULT;
        public TagFile.Progress progress;
        public TagFile.Cancel cancel;
        public String[] tamperTags;       // 自检:把要写的标签换成这些(模拟写错了标签)
        public Runnable afterWrite;       // 自检:临时文件写完、核对之前
    }

    public static final class Report {
        public Outcome outcome = Outcome.FAILED;
        public String code = "";
        public Msg.Text why = Msg.EMPTY;
        public TagFile.Scan scan;
        public int bits;
        public long frames;
        public String pcmMd5 = "";
        public String[] fin = { "", "", "" };
    }

    public static String targetName(String sourceName, Kind kind) {
        int dot = sourceName.lastIndexOf('.');
        String base = dot > 0 ? sourceName.substring(0, dot) : sourceName;
        return base + (kind == Kind.TO_24BIT ? ".24bit.flac" : ".flac");
    }

    /** 容器元数据键 → Vorbis 注释的键(空串 = 不带过去)。与 Windows 版 commentKeyFor 相同 */
    public static String commentKeyFor(String containerKey) {
        if (containerKey.isEmpty()) return "";
        StringBuilder k = new StringBuilder();
        for (int i = 0; i < containerKey.length(); i++) {
            char c = containerKey.charAt(i);
            if (c < 0x20 || c > 0x7D || c == '=') return "";
            k.append(c >= 'a' && c <= 'z' ? (char) (c - 32) : c);
        }
        String s = k.toString();
        for (int i = 0; i < TagRule.KEYS; i++) if (s.equals(TagRule.keyName(i))) return "";
        if (s.equals("ALBUM_ARTIST")) return "ALBUMARTIST";
        if (s.equals("TRACK")) return "TRACKNUMBER";
        if (s.equals("DISC")) return "DISCNUMBER";
        return s;
    }

    /**
     * 值转成 UTF-8(字节串):本来就是合法的 UTF-8 原样用;不是的话按 GBK 转(中文 Windows 上 WAV 的 INFO 块按惯例是这个编码,
     * Windows 版在中文系统上按系统的代码页转,也就是它);两样都不是的不带过去。null = 不带
     */
    static String toUtf8Value(String v) {
        if (TagRule.validUtf8(v)) return v;
        try {
            CharsetDecoder d = Charset.forName("GBK").newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT);
            String s = d.decode(ByteBuffer.wrap(TagRule.bToBytes(v))).toString();
            if (s.isEmpty()) return null;
            byte[] u = s.getBytes(StandardCharsets.UTF_8);
            return TagRule.bytesToB(u, 0, u.length);
        } catch (Exception e) {
            return null;
        }
    }

    static byte[] buildMeta(Rio src, TagFile.Scan scan, String[] fin) {
        FlacMeta.Comments c = new FlacMeta.Comments();
        c.vendor = FlacMeta.NEW_VENDOR;
        List<FlacMeta.MetaBlock> carried = new ArrayList<FlacMeta.MetaBlock>();
        if (scan.nativeFlac) {
            if (!scan.flacOk) return null;
            byte[] meta = Rio.Util.range(src, scan.layout.metaAt, scan.layout.audioAt - scan.layout.metaAt);
            List<FlacMeta.MetaBlock> blocks = meta == null ? null : FlacMeta.splitBlocks(meta, meta.length);
            if (blocks == null) return null;
            for (FlacMeta.MetaBlock b : blocks) {
                if (b.type == FlacMeta.VORBIS_COMMENT) {
                    FlacMeta.Comments one = FlacMeta.parseComments(b.body, 0, b.body.length);
                    if (one == null) return null;
                    c.items.addAll(one.items);
                } else if (b.type == FlacMeta.PICTURE || b.type == FlacMeta.CUESHEET) {
                    carried.add(b);
                }
            }
        } else {
            for (String[] kv : scan.info.meta) {
                String key = commentKeyFor(kv[0]);
                if (key.isEmpty() || kv[1].isEmpty()) continue;
                String value = toUtf8Value(kv[1]);
                if (value == null || value.isEmpty()) continue;
                c.items.add(key + "=" + value);
            }
        }
        List<FlacMeta.Comments> vcs = new ArrayList<FlacMeta.Comments>();
        vcs.add(c);
        FlacMeta.applyTags(vcs, fin);
        List<FlacMeta.MetaBlock> blocks = new ArrayList<FlacMeta.MetaBlock>();
        byte[] body = FlacMeta.buildComments(vcs.get(0));
        if (body.length > FlacMeta.MAX_BLOCK_BODY) return null;
        blocks.add(new FlacMeta.MetaBlock(FlacMeta.VORBIS_COMMENT, body));
        blocks.addAll(carried);
        blocks.add(new FlacMeta.MetaBlock(FlacMeta.PADDING, new byte[NEW_PADDING]));
        return FlacMeta.joinBlocks(blocks);
    }

    static final class Want {
        int rate, bits, padBits, origRate;
        long frames;
        String srcMd5, srcNativeMd5;
        byte[] headerMd5;
        String[] fin;
        TagRule.Rule rule;
    }

    /** 核对写好的临时文件;null = 通过。stopped[0] = 看到取消停了 */
    static Msg.Entry verifyCopy(Rio h, final Want w, final Options opt, boolean[] stopped) {
        stopped[0] = false;
        FlacMeta.Layout lay = new FlacMeta.Layout();
        if (FlacMeta.readLayout(h, lay).err != FlacMeta.Err.OK) return Msg.CvLayout;
        if (lay.sampleRate != w.rate || lay.channels != 2 || lay.bits != w.bits) return Msg.CvFormat;
        if (lay.totalSamples != w.frames) return Msg.CvCount;
        for (int i = 0; i < 16; i++) if (lay.streamInfo[18 + i] != w.headerMd5[i]) return Msg.CvHeaderMd5;
        TagFile.Scan s = TagFile.scan(h, w.rule);
        if (s.state != Classify.State.COMPLETE || !s.info.marker || s.info.origRate != w.origRate || s.info.bits != w.bits) return Msg.CvScan;
        for (int k = 0; k < TagRule.KEYS; k++) if (s.seen.count[k] != 1 || !s.seen.value[k].equals(w.fin[k])) return Msg.CvTags;
        final MessageDigest nativeMd = Crc.md("MD5"), high16 = Crc.md("MD5");
        final boolean[] lowBitsZero = { true };
        final byte[] raw = new byte[1 << 17], rawHigh = new byte[1 << 17];
        final int shift = 32 - w.bits, bytes = w.bits / 8;
        Probe.Pcm pcm = Probe.decodeAll(h, s.info, new Probe.Watch() {
            public boolean tick(double f) {
                if (opt.progress != null) opt.progress.at(0.55 + 0.35 * f);
                return !(opt.cancel != null && opt.cancel.requested());
            }
        }, new Probe.Sink() {
            public void samples(int[] lr, int count) {
                int p = 0, ph = 0;
                for (int i = 0; i < count; i++) {
                    int v = lr[i] >> shift;
                    for (int b = 0; b < bytes; b++) raw[p++] = (byte) (v >>> (8 * b));
                    if (w.padBits != 0) {
                        if ((v & ((1 << w.padBits) - 1)) != 0) lowBitsZero[0] = false;
                        int hi = v >> w.padBits;
                        rawHigh[ph++] = (byte) hi;
                        rawHigh[ph++] = (byte) (hi >>> 8);
                    }
                    if (p + 8 > raw.length) { nativeMd.update(raw, 0, p); p = 0; }
                    if (ph + 8 > rawHigh.length) { high16.update(rawHigh, 0, ph); ph = 0; }
                }
                nativeMd.update(raw, 0, p);
                if (w.padBits != 0) high16.update(rawHigh, 0, ph);
            }
        });
        if (pcm.cancelled) { stopped[0] = true; return null; }
        if (pcm.ioError || pcm.errors != 0) return Msg.CvDecode;
        if (pcm.frames != w.frames) return Msg.CvCount;
        if (w.padBits != 0 && (!lowBitsZero[0] || !Crc.hex(high16.digest()).equals(w.srcNativeMd5))) return Msg.CvLowBits;
        if (!pcm.md5.equals(w.srcMd5)) return Msg.CvSamples;
        byte[] md = nativeMd.digest();
        for (int i = 0; i < 16; i++) if (md[i] != lay.streamInfo[18 + i]) return Msg.CvHeaderMd5;
        return null;
    }

    private static Report done(Report r, Outcome o, String code, Msg.Text why) {
        r.outcome = o;
        r.code = code;
        r.why = why;
        return r;
    }

    private static boolean cancelled(Options o) { return o.cancel != null && o.cancel.requested(); }

    public static Report copy(Rio src, Kind kind, Target target, Options opt, TagFile.Workspace ws) {
        Report r = new Report();
        if (cancelled(opt)) return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));
        r.scan = TagFile.scan(src, opt.rule);
        if (kind == Kind.WAV_TO_FLAC && !r.scan.canWavToFlac)
            return done(r, Outcome.SKIPPED, Classify.stateCode(r.scan.state), Msg.of(Msg.CopySkipNotWav, TagFile.stateMsg(r.scan)));
        if (kind == Kind.TO_24BIT && !r.scan.canCopy24)
            return done(r, Outcome.SKIPPED, r.scan.info.marker && r.scan.info.bits == 16 ? Classify.stateCode(r.scan.state) : "not_16bit",
                    Msg.of(Msg.CopySkipNot16, TagFile.stateMsg(r.scan)));
        String[] write = r.scan.fin.clone();
        r.fin = r.scan.fin.clone();
        if (opt.tamperTags != null) write = opt.tamperTags.clone();
        r.bits = kind == Kind.TO_24BIT ? 24 : r.scan.info.bits;
        if (target == null) return done(r, Outcome.REFUSED, "no_folder", Msg.of(Msg.ACopyNoFolder));
        Msg.Text targetName = Msg.data(target.name());
        if (target.exists()) return done(r, Outcome.REFUSED, "target_exists", Msg.of(Msg.CopyTargetExists, targetName));
        if (opt.dryRun) return done(r, Outcome.WOULD_CREATE, "", Msg.of(Msg.CopyWould, targetName));
        byte[] meta = buildMeta(src, r.scan, write);
        if (meta == null) return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyEncode));
        if (opt.progress != null) opt.progress.at(0.05);

        if (cancelled(opt)) return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));
        TagFile.Temp tmp = ws.newTemp();
        if (tmp == null) return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyTempCreate, Msg.of(Msg.LayIo)));
        try {
            Probe.FlacWritten wr = new Probe.FlacWritten();
            final Options o = opt;
            boolean okWrite = Probe.writeFlacCopy(src, r.scan.info, tmp, r.bits, kind == Kind.TO_24BIT ? PAD_TO_24 : 0, meta, wr, new Probe.Watch() {
                public boolean tick(double f) {
                    if (cancelled(o)) return false;
                    if (o.progress != null) o.progress.at(0.05 + 0.5 * f);
                    return true;
                }
            });
            if (!okWrite) {
                switch (wr.fail) {
                case CANCELLED: return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));
                case SOURCE: return done(r, Outcome.FAILED, "decode_error", Msg.of(Msg.CopyDecode, Msg.of(Msg.LayIo)));
                case WRITE: return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyTempWrite, Msg.of(Msg.LayIo)));
                default: return done(r, Outcome.FAILED, "encode_failed", Msg.of(Msg.CopyEncode));
                }
            }
            long declared = r.scan.nativeFlac ? r.scan.layout.totalSamples : 0;
            if (declared > 0 ? wr.frames != declared : wr.decodeErrors != 0)
                return done(r, Outcome.REFUSED, wr.decodeErrors != 0 ? "decode_error" : "incomplete_audio",
                        Msg.of(Msg.CopyDecode, Msg.data(wr.frames + " / " + declared + " / " + wr.decodeErrors)));
            if (!tmp.sync()) return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyTempWrite, Msg.of(Msg.LayIo)));
            if (opt.afterWrite != null) opt.afterWrite.run();

            if (cancelled(opt)) return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));
            Want want = new Want();
            want.rate = wr.rate;
            want.bits = r.bits;
            want.padBits = kind == Kind.TO_24BIT ? PAD_TO_24 : 0;
            want.origRate = r.scan.info.origRate;
            want.frames = wr.frames;
            want.srcMd5 = wr.srcMd5;
            want.srcNativeMd5 = wr.srcNativeMd5;
            want.headerMd5 = wr.outMd5;
            want.fin = r.fin;
            want.rule = opt.rule;
            boolean[] stopped = { false };
            Msg.Entry vwhy = verifyCopy(tmp, want, opt, stopped);
            if (stopped[0]) return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));
            if (vwhy != null) return done(r, Outcome.FAILED, "verify_failed", Msg.of(Msg.CopyVerifyFailed, Msg.of(vwhy)));
            String tmpSha = Crc.sha256(tmp, 0, tmp.size());
            if (tmpSha == null) return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyTempWrite, Msg.of(Msg.LayIo)));
            if (cancelled(opt)) return done(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.CopyCancelled));

            // 放到目标位置:目标名下这时冒出了文件也不盖
            if (target.exists()) return done(r, Outcome.REFUSED, "target_exists", Msg.of(Msg.CopyTargetExists, targetName));
            Rio out = target.create();
            if (out == null) return done(r, Outcome.FAILED, "io", Msg.of(Msg.ACopyCreateFailed, Msg.of(Msg.LayIo)));
            boolean placed = TagFile.overwriteFrom(tmp, out, new TagFile.Hooks()) && out.size() == tmp.size() && tmpSha.equals(Crc.sha256(out, 0, out.size()));
            if (!placed) {
                target.discard();
                return done(r, Outcome.FAILED, "io", Msg.of(Msg.CopyRename, Msg.of(Msg.AVerWhole)));
            }
            target.finish();
            if (opt.progress != null) opt.progress.at(1.0);
            r.frames = wr.frames;
            r.pcmMd5 = wr.srcMd5;
            return done(r, Outcome.CREATED, "", kind == Kind.TO_24BIT ? Msg.of(Msg.CopyCreated24, targetName, Msg.num(wr.frames))
                    : Msg.of(Msg.CopyCreated, targetName, Msg.num(r.bits), Msg.num(wr.frames)));
        } finally {
            tmp.delete();
        }
    }
}
