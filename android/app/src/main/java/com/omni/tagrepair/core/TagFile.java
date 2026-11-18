package com.omni.tagrepair.core;

import java.util.Arrays;
import java.util.List;

/**
 * 一个文件:看它是哪一类(scan)、把三个识别标签修好(fix)。对应 Windows 版 src/tagfile.cpp,守的规矩相同:
 *   · 音频数据里没有同步标记的绝不写;结构走不通、解码有错、音频不完整的不碰;标签完整的一个字节都不动;
 *   · 音频帧到文件末尾的字节前后逐字节相同(SHA-256 前后各算一遍),STREAMINFO 不变;整轨解出的样本 MD5 前后相同;
 *   · 原地改写只在"长度能靠 PADDING 找平、而且改动的字节全落在同一个扇区里"时用;读回不对就把原来的字节写回去;
 *   · 其余的先在本应用自己的目录里写一个完整的新文件、从头核对,再整个写回原文件,写回之后逐字节核对。
 * 安卓上和 Windows 不同的只有最后这一步:系统给的文件(content://)不能"换入",只能写回。写回之前先登记(Workspace.journalBegin):
 * 写到一半被打断(进程被杀、断电)时,核对过的新版本还在本应用的目录里,下次打开时接着写完(见 Journal)。
 */
public final class TagFile {
    private TagFile() {}

    public static final class Scan {
        public Probe.Info info = new Probe.Info();
        public boolean nativeFlac, flacOk;
        public Msg.Entry flacWhy;
        public FlacMeta.Layout layout = new FlacMeta.Layout();
        public TagRule.Seen seen = new TagRule.Seen();
        public int bad;
        public String[] fin = { "", "", "" };
        public Classify.State state = Classify.State.ERROR;
        public boolean canRepair, canWavToFlac, canCopy24;
    }

    public static Scan scan(Rio file, TagRule.Rule rule) {
        Scan out = new Scan();
        out.info = Probe.inspect(file);
        Classify.Facts f = new Classify.Facts();
        f.opened = out.info.opened;
        f.losslessPcm = out.info.losslessPcm;
        f.marker = out.info.marker;
        out.nativeFlac = out.info.opened && out.info.container.equals("flac");
        f.nativeFlac = out.nativeFlac;
        if (out.nativeFlac) {
            FlacMeta.LayoutResult lr = FlacMeta.readLayout(file, out.layout);
            byte[] meta = null;
            List<FlacMeta.Comments> vcs = null;
            if (lr.err != FlacMeta.Err.OK) {
                out.flacWhy = lr.why;
            } else if ((meta = Rio.Util.range(file, out.layout.metaAt, out.layout.audioAt - out.layout.metaAt)) == null) {
                out.flacWhy = Msg.LayMetaUnreadable;
            } else if ((vcs = FlacMeta.commentsOf(meta)) == null) {
                out.flacWhy = Msg.LayBadComment;
            } else {
                out.flacOk = true;
                out.seen = FlacMeta.seenIn(vcs);
                out.fin = FlacMeta.chooseFinal(vcs, out.info.origRate, rule);
            }
            f.flacDamaged = !out.flacOk;
        } else {
            for (int k = 0; k < TagRule.KEYS; k++) {
                out.seen.count[k] = out.info.has[k] ? 1 : 0;
                out.seen.value[k] = out.info.value[k];
                String ok = out.info.has[k] ? TagRule.firstAcceptable(k, out.info.value[k], out.info.origRate, rule) : null;
                if (ok != null) { out.fin[k] = ok; continue; }
                if (k != TagRule.ORIG_RATE) out.fin[k] = rule.encoderTag;
                else if (out.info.origRate > 0) out.fin[k] = Integer.toString(out.info.origRate);
            }
            f.wavPcm = out.info.container.equals("wav") && (out.info.codec.equals("pcm_s16le") || out.info.codec.equals("pcm_s24le"))
                    && (out.info.bits == 16 || out.info.bits == 24);
        }
        out.bad = TagRule.problems(out.seen, out.info.origRate, rule);
        f.bad = out.bad;
        f.rateAvailable = !out.fin[TagRule.ORIG_RATE].isEmpty();
        out.state = Classify.classify(f);
        out.canRepair = out.state == Classify.State.REPAIRABLE;
        out.canWavToFlac = out.state == Classify.State.CONVERTIBLE;
        out.canCopy24 = out.info.marker && out.info.bits == 16
                && (out.state == Classify.State.CONVERTIBLE || (out.flacOk && (out.state == Classify.State.COMPLETE || out.state == Classify.State.REPAIRABLE)));
        return out;
    }

    /** 键名列出来(英文用逗号、中文用顿号) */
    public static Msg.Text keyList(int mask) {
        StringBuilder en = new StringBuilder(), zh = new StringBuilder();
        for (int k = 0; k < TagRule.KEYS; k++)
            if ((mask & (1 << k)) != 0) {
                if (en.length() > 0) { en.append(", "); zh.append("、"); }
                en.append(TagRule.keyName(k));
                zh.append(TagRule.keyName(k));
            }
        return new Msg.Text(en.toString(), zh.toString());
    }

    public static Msg.Text stateMsg(Scan s) {
        switch (s.state) {
        case ERROR: return Msg.of(Msg.StError, s.info.error.empty() ? Msg.of(Msg.WhyUnreadable) : s.info.error);
        case NOT_LOSSLESS: return Msg.of(Msg.StNotLossless, Msg.data(s.info.codec));
        case NO_MARKER: return Msg.of(Msg.StNoMarker);
        case COMPLETE: return Msg.of(Msg.StComplete);
        case REPAIRABLE:
            return s.bad == (TagRule.BAD_ENCODER | TagRule.BAD_FORMAT_ENCODER | TagRule.BAD_ORIG_RATE) && s.seen.count[0] == 0 && s.seen.count[1] == 0
                    && s.seen.count[2] == 0 ? Msg.of(Msg.StRepairMissing) : Msg.of(Msg.StRepairIncomplete, keyList(s.bad));
        case RATE_UNKNOWN: return Msg.of(Msg.StRateUnknown);
        case CONVERTIBLE: return Msg.of(Msg.StConvertible);
        case UNSUPPORTED:
            if (s.info.container.equals("wav"))
                return s.fin[TagRule.ORIG_RATE].isEmpty() && (s.info.bits == 16 || s.info.bits == 24) ? Msg.of(Msg.StWavNoRate)
                        : Msg.of(Msg.StWavFormat, Msg.data(s.info.codec));
            return Msg.of(Msg.StUnsupported, Msg.data(s.info.container));
        case DAMAGED: return Msg.of(Msg.StDamaged, s.flacWhy != null ? Msg.of(s.flacWhy) : Msg.EMPTY);
        default: return Msg.EMPTY;
        }
    }

    // ---------------------------------------------------------------------------
    // 修
    // ---------------------------------------------------------------------------
    public enum Outcome { FIXED, UNCHANGED, WOULD_FIX, SKIPPED, REFUSED, FAILED, CANCELLED }

    /** 本应用自己的临时文件 */
    public interface Temp extends Rio {
        String path();
        void delete();
        void close();                                     // 留着文件、只放掉打开的句柄
    }

    /**
     * 写回登记:写回原文件之前落盘,写回做完(成功,或者原样恢复)之后删掉。
     * 有了它,写到一半进程没了也能分清原文件现在是什么样、接着怎么做(见 finishOverwrite)。
     */
    public static final class Journal {
        public long oldSize, newSize, oldAudioAt, newAudioAt;
        public String newSha = "", oldAudioSha = "";
        public byte[] oldHead = new byte[0], newHead = new byte[0];   // 文件开头到音频帧之前的字节(修之前 / 修之后)
    }

    /** 安卓 / 电脑各自提供:临时文件放哪、写回之前登记 */
    public interface Workspace {
        Temp newTemp();                                   // 建不了返回 null
        /** 登记,落盘了才返回 true;返回 false 时不写回(原文件不动) */
        boolean journalBegin(Temp temp, Journal j);
        void journalEnd(Temp temp);
    }

    /** 取消:看到就停的地方与 Windows 版相同 */
    public interface Cancel {
        boolean requested();
    }

    public interface Progress {
        void at(double f);
    }

    /** 自检用的钩子(正常使用时全是 null) */
    public static class Hooks {
        public Runnable afterWrite;                       // 原地写完、核对之前
        public Runnable afterTempWrite;
        public String[] expect;                           // 非 null:修之前记下的两个指纹被换成这两个(模拟"前后对不上")
        public boolean failWrite;                         // 当作写入失败
        public Runnable beforeOverwrite;                  // 写回之前
        public Runnable duringOverwrite;                  // 写回途中(模拟被打断:抛出 Interrupt)
        public boolean failJournal;                       // 登记没落盘
    }

    /** 自检模拟"写回途中进程没了" */
    public static final class Interrupt extends RuntimeException {
        public Interrupt() { super("simulated interruption"); }
    }

    public static final class FixOptions {
        public boolean dryRun;
        public TagRule.Rule rule = TagRule.DEFAULT;
        public long sectorBytes = FlacMeta.SECTOR_BYTES;
        public Progress progress;
        public Cancel cancel;
        public Hooks test = new Hooks();
    }

    public static final class FixReport {
        public Outcome outcome = Outcome.FAILED;
        public String code = "";
        public Msg.Text why = Msg.EMPTY;
        public Scan scan = new Scan();
        public boolean inPlace;
        public int wrote;
        public TagRule.Seen after = new TagRule.Seen();
        public String audioSha256 = "", pcmMd5 = "";
        public long audioBytes, pcmFrames;
        public String leftAt = "";                        // 写回没成功时:修好并核对过的那份在本应用的哪里
    }

    private static FixReport refuse(FixReport r, Outcome o, String code, Msg.Text why) {
        r.outcome = o;
        r.code = code;
        r.why = why;
        return r;
    }

    private static boolean cancelled(FixOptions o) { return o.cancel != null && o.cancel.requested(); }
    private static void at(FixOptions o, double f) { if (o.progress != null) o.progress.at(f); }

    static final class Expect {
        long metaAt, audioAt, audioBytes;
        byte[] streamInfo, prefix, meta;
        TagRule.Rule rule;
        String[] fin;
        int origRate;
        String audioSha, pcmMd5;
        long pcmFrames;
        int pcmErrors;
    }

    /** 核对写完的文件。返回 null = 通过;否则是原因。stoppable 时看到取消就停(stopped[0] = true) */
    static Msg.Text verify(Rio h, Expect e, final FixOptions opt, boolean stoppable, final double from, final double to, boolean[] stopped,
                           TagRule.Seen[] seenOut) {
        stopped[0] = false;
        FlacMeta.Layout l = new FlacMeta.Layout();
        FlacMeta.LayoutResult lr = FlacMeta.readLayout(h, l);
        if (lr.err != FlacMeta.Err.OK) return Msg.of(Msg.VerLayout, lr.why != null ? Msg.of(lr.why) : Msg.EMPTY);
        if (l.metaAt != e.metaAt || l.audioAt != e.audioAt) return Msg.of(Msg.VerAudioStart);
        if (l.fileSize - l.audioAt != e.audioBytes) return Msg.of(Msg.VerAudioLength);
        if (!Arrays.equals(l.streamInfo, e.streamInfo)) return Msg.of(Msg.VerStreamInfo);
        byte[] got = Rio.Util.range(h, 0, l.metaAt);
        if (got == null || !Arrays.equals(got, e.prefix)) return Msg.of(Msg.VerPrefix);
        got = Rio.Util.range(h, l.metaAt, l.audioAt - l.metaAt);
        if (got == null || !Arrays.equals(got, e.meta)) return Msg.of(Msg.VerMeta);
        TagRule.Seen s = FlacMeta.seenInMeta(got);
        if (s == null || TagRule.problems(s, e.origRate, e.rule) != 0) return Msg.of(Msg.VerTags);
        seenOut[0] = s;
        for (int k = 0; k < TagRule.KEYS; k++) if (!s.value[k].equals(e.fin[k])) return Msg.of(Msg.VerTag, Msg.data(TagRule.keyName(k)));
        String sha = Crc.sha256(h, l.audioAt, l.fileSize);
        if (sha == null || !sha.equals(e.audioSha)) return Msg.of(Msg.VerSha);
        Probe.Info info = Probe.inspect(h);
        if (!info.opened || !info.marker || info.origRate != e.origRate) return Msg.of(Msg.AVerNoMarker);
        final boolean stop = stoppable;
        Probe.Pcm pcm = Probe.decodeAll(h, info, new Probe.Watch() {
            public boolean tick(double f) {
                at(opt, from + (to - from) * f);
                return !(stop && cancelled(opt));
            }
        }, null);
        if (pcm.cancelled) { stopped[0] = true; return Msg.EMPTY; }
        if (pcm.ioError || pcm.errors != e.pcmErrors || !pcm.md5.equals(e.pcmMd5) || pcm.frames != e.pcmFrames) return Msg.of(Msg.AVerPcm);
        return null;
    }

    private static boolean copyRange(Rio from, long at, long end, Rio to, long toAt, FixOptions opt, boolean[] stopped) {
        byte[] buf = new byte[1 << 20];
        stopped[0] = false;
        while (at < end) {
            int k = (int) Math.min(buf.length, end - at);
            if (!from.read(at, buf, 0, k) || !to.write(toAt, buf, 0, k)) return false;
            at += k;
            toAt += k;
            if (cancelled(opt)) { stopped[0] = true; return false; }
        }
        return true;
    }

    /**
     * 上次写回被打断(登记过、没写完)时接着写:temp 是核对过的新版本,size / sha256 是登记时记下的。
     * temp 本身先核对一遍(大小、SHA-256 与登记的相同),不对就不写(返回原因);原文件已经是新版本了就不用再写。返回 null = 好了
     */
    /** DONE = 现在是修好的版本;ORIGINAL = 写不成、已恢复成原样(原文件完好,登记可以删);其余见下 */
    public enum Recover { DONE, ORIGINAL, NOT_OURS, TEMP_BAD, FAILED }

    /**
     * 上次写回被打断(登记过、没写完)时接着做。temp 是核对过的新版本,j 是登记的内容。
     *   · temp 本身先核对(大小、SHA-256 与登记的相同),不对就什么都不写(TEMP_BAD);
     *   · 原文件还是不是"这一个":大小是修之前或修之后的长度;开头那一段是"前面一截已经写成修之后的、后面还是修之前的"
     *     (写回从头往后写,停在哪都是这个样子;一截都没写 = 原样,全写完 = 新版本)——
     *     对不上(比如这个位置上已经换成了别的文件)就不碰(NOT_OURS);
     *   · 已经是新版本:DONE;否则按 writeBack 写成新版本(写不成就恢复成原样:ORIGINAL)。
     */
    public static Recover finishOverwrite(Rio temp, Rio target, Journal j) {
        if (temp.size() != j.newSize || !j.newSha.equals(Crc.sha256(temp, 0, j.newSize))) return Recover.TEMP_BAD;
        long sz = target.size();
        if (sz != j.oldSize && sz != j.newSize) return Recover.NOT_OURS;
        if (!headIsOursPartly(target, sz, j)) return Recover.NOT_OURS;
        if (sz == j.newSize && j.newSha.equals(Crc.sha256(target, 0, sz))) return Recover.DONE;
        WriteBack w = writeBack(temp, target, j, new Hooks());
        if (w == WriteBack.DONE) return Recover.DONE;
        return w == WriteBack.UNTOUCHED || w == WriteBack.ROLLED_BACK ? Recover.ORIGINAL : Recover.FAILED;
    }

    /** 文件开头 = 修之后的开头的前 w 个字节 + 修之前的开头从 w 起的部分(w 从 0 到整段都可以) */
    static boolean headIsOursPartly(Rio target, long sz, Journal j) {
        int span = (int) Math.min(sz, Math.max(j.newHead.length, j.oldHead.length));
        byte[] h = Rio.Util.range(target, 0, span);
        if (h == null) return false;
        int w = 0;
        while (w < h.length && w < j.newHead.length && h[w] == j.newHead[w]) w++;
        if (w >= j.newHead.length) return true;                      // 修之后的开头整段都在
        // 恢复原样(restoreOld)做到一半:前面一截已经写回修之前的、后面还是修之后的 —— 也是自己的
        int v = 0;
        while (v < h.length && v < j.oldHead.length && h[v] == j.oldHead[v]) v++;
        if (v >= j.oldHead.length) return true;
        boolean restoring = true;
        for (int i = v; i < Math.min(h.length, j.newHead.length); i++) if (h[i] != j.newHead[i]) { restoring = false; break; }
        if (restoring) return true;
        for (int i = w; i < Math.min(h.length, j.oldHead.length); i++) if (h[i] != j.oldHead[i]) return false;     // 修之前开头以外的是音频字节,不比
        return true;
    }

    public enum WriteBack { DONE, UNTOUCHED, ROLLED_BACK, STUCK }

    /**
     * 把核对过的新版本(temp)写回原文件,写完逐字节核对。顺序:
     *   1. 新文件比原来长:先把原文件在末尾延长到新长度(这一步不碰已有的字节;盘满在这里就失败,原文件原样 → UNTOUCHED);
     *   2. 从头整个写一遍、截到新长度、落盘、SHA-256 与 temp 相同 → DONE(不同就再写一遍);
     *   3. 还不对:按登记里修之前的开头 + temp 里的音频字节写回原样、截回原长度、核对(开头相同、音频 SHA-256 与修之前相同)
     *      → ROLLED_BACK;连这一步也做不成 → STUCK(登记留着,下次接着做)。
     */
    static WriteBack writeBack(Rio temp, Rio target, Journal j, Hooks test) {
        if (j.newSize > j.oldSize) {
            if (!target.write(j.newSize - 1, new byte[1], 0, 1) || !target.sync() || target.size() != j.newSize) {
                target.truncate(j.oldSize);
                byte[] h = Rio.Util.range(target, 0, j.oldHead.length);
                if (target.size() == j.oldSize && h != null && Arrays.equals(h, j.oldHead)) return WriteBack.UNTOUCHED;
                return restoreOld(temp, target, j) ? WriteBack.ROLLED_BACK : WriteBack.STUCK;
            }
        }
        for (int attempt = 0; attempt < 2; attempt++) {
            if (overwriteFrom(temp, target, test) && target.size() == j.newSize && j.newSha.equals(Crc.sha256(target, 0, j.newSize))) return WriteBack.DONE;
        }
        return restoreOld(temp, target, j) ? WriteBack.ROLLED_BACK : WriteBack.STUCK;
    }

    /** 恢复成修之前的样子:修之前的开头 + 音频字节(从 temp 里拿,与原来的逐字节相同),再核对 */
    static boolean restoreOld(Rio temp, Rio target, Journal j) {
        if (!target.write(0, j.oldHead, 0, j.oldHead.length)) return false;
        byte[] buf = new byte[1 << 20];
        long from = j.newAudioAt, to = j.oldAudioAt, end = j.newSize;
        while (from < end) {
            int k = (int) Math.min(buf.length, end - from);
            if (!temp.read(from, buf, 0, k) || !target.write(to, buf, 0, k)) return false;
            from += k;
            to += k;
        }
        if (!target.truncate(j.oldSize) || !target.sync()) return false;
        byte[] h = Rio.Util.range(target, 0, j.oldHead.length);
        return target.size() == j.oldSize && h != null && Arrays.equals(h, j.oldHead) && j.oldAudioSha.equals(Crc.sha256(target, j.oldAudioAt, j.oldSize));
    }

    /** 整个 src 拷进 dst,再核对长度与 SHA-256(另存修好的那份时用) */
    public static boolean copyVerified(Rio src, Rio dst, long size, String sha256) {
        if (src.size() != size || !sha256.equals(Crc.sha256(src, 0, size))) return false;
        return overwriteFrom(src, dst, new Hooks()) && dst.size() == size && sha256.equals(Crc.sha256(dst, 0, size));
    }

    /** 整个 src 拷进 dst(从 0 起),再截到 src 的长度 */
    static boolean overwriteFrom(Rio src, Rio dst, Hooks test) {
        byte[] buf = new byte[1 << 20];
        long n = src.size();
        for (long at = 0; at < n; ) {
            int k = (int) Math.min(buf.length, n - at);
            if (!src.read(at, buf, 0, k) || !dst.write(at, buf, 0, k)) return false;
            at += k;
            if (test.duringOverwrite != null && at >= n / 2) { Runnable r = test.duringOverwrite; test.duringOverwrite = null; r.run(); }
        }
        return dst.truncate(n) && dst.sync();
    }

    /**
     * file:要修的文件,已经按读写打开(writable = false:只能读,这时能修的也只说没有写权限)。
     * ws:临时文件与写回登记;dryRun 时可以是 null。
     */
    public static FixReport fix(Rio file, boolean writable, FixOptions opt, Workspace ws) {
        FixReport r = new FixReport();
        boolean[] stopped = { false };
        // ---- 1. 看一眼:不是能修的那一类就到此为止 ----
        if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
        r.scan = scan(file, opt.rule);
        if (r.scan.state == Classify.State.COMPLETE) return refuse(r, Outcome.UNCHANGED, "", Msg.of(Msg.FixComplete));
        if (r.scan.state != Classify.State.REPAIRABLE) return refuse(r, Outcome.SKIPPED, Classify.stateCode(r.scan.state), stateMsg(r.scan));
        at(opt, 0.05);
        if (!writable && !opt.dryRun) return refuse(r, Outcome.REFUSED, "access_denied", Msg.of(Msg.AFixNoWrite));
        FlacMeta.Layout lay = r.scan.layout;

        // ---- 2. 算出修好之后的元数据 ----
        byte[] oldMeta = Rio.Util.range(file, lay.metaAt, lay.audioAt - lay.metaAt), prefix = Rio.Util.range(file, 0, lay.metaAt);
        if (oldMeta == null || prefix == null) return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixReadFailed));
        FlacMeta.Plan plan = new FlacMeta.Plan();
        switch (FlacMeta.planFix(oldMeta, r.scan.fin, lay.metaAt, opt.sectorBytes, plan)) {
        case OK: break;
        case TOO_BIG: return refuse(r, Outcome.REFUSED, "too_big", Msg.of(Msg.FixTooBig));
        case BAD_META:
        case BAD_COMMENT: return refuse(r, Outcome.REFUSED, "damaged", Msg.of(Msg.FixMetaDamaged));
        default: return refuse(r, Outcome.FAILED, "internal", Msg.of(Msg.FixInternalLength));
        }
        if (plan.mode == FlacMeta.Mode.NO_CHANGE) return refuse(r, Outcome.UNCHANGED, "", Msg.of(Msg.FixComplete));
        r.wrote = r.scan.bad;
        r.inPlace = plan.mode == FlacMeta.Mode.IN_PLACE;

        // ---- 3. 修之前的两个指纹 ----
        Expect e = new Expect();
        e.metaAt = lay.metaAt;
        e.audioAt = lay.metaAt + plan.newMeta.length;
        e.audioBytes = lay.fileSize - lay.audioAt;
        e.streamInfo = lay.streamInfo.clone();
        e.prefix = prefix;
        e.meta = plan.newMeta;
        e.rule = opt.rule;
        e.fin = r.scan.fin.clone();
        e.origRate = r.scan.info.origRate;
        if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
        e.audioSha = Crc.sha256(file, lay.audioAt, lay.fileSize);
        if (e.audioSha == null) return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixReadFailed));
        at(opt, 0.1);
        if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
        final FixOptions o = opt;
        Probe.Pcm pcm = Probe.decodeAll(file, r.scan.info, new Probe.Watch() {
            public boolean tick(double f) {
                at(o, 0.1 + 0.4 * f);
                return !cancelled(o);
            }
        }, null);
        if (pcm.cancelled) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
        if (pcm.ioError) return refuse(r, Outcome.REFUSED, "decode_error", Msg.of(Msg.FixDecodeFailed, Msg.of(Msg.LayIo)));
        if (lay.totalSamples > 0 ? pcm.frames != lay.totalSamples : pcm.errors != 0) {
            if (lay.totalSamples > 0)
                return refuse(r, Outcome.REFUSED, pcm.errors != 0 ? "decode_error" : "incomplete_audio",
                        Msg.of(pcm.errors != 0 ? Msg.FixAudioBadFrames : Msg.FixAudioShort, Msg.num(pcm.frames), Msg.num(lay.totalSamples), Msg.num(pcm.errors)));
            return refuse(r, Outcome.REFUSED, "decode_error", Msg.of(Msg.FixDecodeErrors, Msg.num(pcm.errors)));
        }
        e.pcmMd5 = pcm.md5;
        e.pcmFrames = pcm.frames;
        e.pcmErrors = pcm.errors;
        r.audioSha256 = e.audioSha;
        r.audioBytes = e.audioBytes;
        r.pcmMd5 = pcm.md5;
        r.pcmFrames = pcm.frames;
        final String audioShaRead = e.audioSha;
        if (opt.test.expect != null) { e.audioSha = opt.test.expect[0]; e.pcmMd5 = opt.test.expect[1]; }

        // ---- 4. 写之前:要写的字节先用自己的解析器在内存里过一遍 ----
        {
            byte[] head = new byte[prefix.length + plan.newMeta.length + 2];
            System.arraycopy(prefix, 0, head, 0, prefix.length);
            System.arraycopy(plan.newMeta, 0, head, prefix.length, plan.newMeta.length);
            head[head.length - 2] = (byte) 0xFF;
            head[head.length - 1] = (byte) 0xF8;
            FlacMeta.Layout nl = new FlacMeta.Layout();
            TagRule.Seen s = FlacMeta.seenInMeta(plan.newMeta);
            if (FlacMeta.readLayout(new Rio.Mem(head), nl).err != FlacMeta.Err.OK || nl.audioAt != e.audioAt || !Arrays.equals(nl.streamInfo, lay.streamInfo)
                    || s == null || TagRule.problems(s, e.origRate, opt.rule) != 0 || (plan.sameSize && e.audioAt != lay.audioAt))
                return refuse(r, Outcome.FAILED, "internal", Msg.of(Msg.FixInternalCheck));
        }
        if (opt.dryRun) return refuse(r, Outcome.WOULD_FIX, "", Msg.of(Msg.FixWouldFix, keyList(r.wrote)));

        TagRule.Seen[] after = { null };
        if (r.inPlace) {
            // ---- 5a. 原地改写:只写变了的那一段(在同一个扇区里);读回不对就写回去 ----
            if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
            long wAt = lay.metaAt + plan.diffAt;
            int n = plan.diffEnd - plan.diffAt;
            boolean wrote = false;
            Msg.Text why;
            try {
                wrote = !opt.test.failWrite && file.write(wAt, plan.newMeta, plan.diffAt, n) && file.sync();
                if (wrote && opt.test.afterWrite != null) opt.test.afterWrite.run();
                why = wrote ? verify(file, e, opt, false, 0.55, 1.0, stopped, after) : null;
            } catch (Throwable t) {                       // 写了之后出什么意外(内存不够之类):一律当核对不过,写回原来的字节
                why = Msg.of(Msg.AVerWhole).plus(Msg.data(" (" + t.getClass().getSimpleName() + ")"));
                wrote = true;
            }
            if (!wrote || why != null) {
                byte[] back;
                boolean restored = file.write(lay.metaAt, oldMeta, 0, oldMeta.length) && file.sync()
                        && (back = Rio.Util.range(file, lay.metaAt, oldMeta.length)) != null && Arrays.equals(back, oldMeta);
                Msg.Text tail = Msg.of(restored ? Msg.FixTailRestored : Msg.FixTailNotRestored);
                if (!wrote) return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixWriteFailed, Msg.of(Msg.LayIo)).plus(tail));
                return refuse(r, Outcome.FAILED, "verify_failed", why.plus(tail));
            }
            r.after = after[0];
            at(opt, 1.0);
            r.outcome = Outcome.FIXED;
            return r;
        }

        // ---- 5b. 整文件重写:本应用的目录里写一个完整的新文件 → 核对 → 写回原文件 → 逐字节核对 ----
        if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelled));
        Temp tmp = ws == null ? null : ws.newTemp();
        if (tmp == null) return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixTempCreate, Msg.of(Msg.LayIo)));
        boolean keepTemp = false;
        try {
            if (!(!opt.test.failWrite && tmp.write(0, prefix, 0, prefix.length) && tmp.write(prefix.length, plan.newMeta, 0, plan.newMeta.length)
                    && copyRange(file, lay.audioAt, lay.fileSize, tmp, e.audioAt, opt, stopped) && tmp.sync())) {
                if (stopped[0]) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelledTemp));
                return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixTempWrite, Msg.of(Msg.LayIo)));
            }
            at(opt, 0.6);
            if (opt.test.afterTempWrite != null) opt.test.afterTempWrite.run();
            if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelledTemp));
            Msg.Text why = verify(tmp, e, opt, true, 0.6, 0.9, stopped, after);
            if (stopped[0]) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelledTemp));
            if (why != null) return refuse(r, Outcome.FAILED, "verify_failed", why.plus(Msg.of(Msg.FixTailTempGone)));
            String tmpSha = Crc.sha256(tmp, 0, tmp.size());
            if (tmpSha == null) return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixTempWrite, Msg.of(Msg.LayIo)));
            if (cancelled(opt)) return refuse(r, Outcome.CANCELLED, "cancelled", Msg.of(Msg.FixCancelledTemp));
            // 写回之前再看一眼原文件:大小、开头到音频帧之前的字节都还是刚才读的那样
            if (opt.test.beforeOverwrite != null) opt.test.beforeOverwrite.run();
            byte[] headNow = Rio.Util.range(file, 0, lay.audioAt);
            byte[] headWas = new byte[prefix.length + oldMeta.length];
            System.arraycopy(prefix, 0, headWas, 0, prefix.length);
            System.arraycopy(oldMeta, 0, headWas, prefix.length, oldMeta.length);
            if (file.size() != lay.fileSize || headNow == null || !Arrays.equals(headNow, headWas) || !audioShaRead.equals(Crc.sha256(file, lay.audioAt, lay.fileSize)))
                return refuse(r, Outcome.REFUSED, "changed", Msg.of(Msg.AFixChanged));
            // 从这里起不看取消:登记、写回、核对,做完这个文件
            Journal j = new Journal();
            j.oldSize = lay.fileSize;
            j.newSize = tmp.size();
            j.oldAudioAt = lay.audioAt;
            j.newAudioAt = e.audioAt;
            j.newSha = tmpSha;
            j.oldAudioSha = audioShaRead;
            j.oldHead = headWas;
            j.newHead = new byte[prefix.length + plan.newMeta.length];
            System.arraycopy(prefix, 0, j.newHead, 0, prefix.length);
            System.arraycopy(plan.newMeta, 0, j.newHead, prefix.length, plan.newMeta.length);
            if (opt.test.failJournal || !ws.journalBegin(tmp, j))     // 登记没落盘:不写回(原文件没动过)
                return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixTempWrite, Msg.of(Msg.LayIo)).plus(Msg.of(Msg.FixTailTempGone)));
            keepTemp = true;                              // 登记过了:从这里起出什么事都不删它
            WriteBack wb = writeBack(tmp, file, j, opt.test);
            if (wb == WriteBack.UNTOUCHED || wb == WriteBack.ROLLED_BACK) {
                ws.journalEnd(tmp);
                keepTemp = false;
                return refuse(r, Outcome.FAILED, "io", Msg.of(Msg.FixWriteFailed, Msg.of(Msg.LayIo)).plus(Msg.of(Msg.FixTailRestored)));
            }
            if (wb == WriteBack.STUCK) {
                r.leftAt = tmp.path();
                tmp.close();
                return refuse(r, Outcome.FAILED, "swap_incomplete", Msg.of(Msg.AFixOverwriteRetried, Msg.of(Msg.AVerWhole)));
            }
            ws.journalEnd(tmp);
            keepTemp = false;
            r.after = after[0];
            at(opt, 1.0);
            r.outcome = Outcome.FIXED;
            return r;
        } finally {
            if (!keepTemp) tmp.delete();
        }
    }
}
