import com.omni.tagrepair.core.*;

import java.io.File;
import java.io.RandomAccessFile;
import java.util.UUID;

/**
 * 电脑上跑安卓版的核心代码(同一份 Java 源码),拿来和 Windows 版的 tagrepair.exe 逐字节对照。不进安卓包。
 *   java DeskTool scan <文件>…
 *   java DeskTool fix <文件>…              (原地修;临时文件放在 <文件>.tmpdir 里)
 *   java DeskTool copy24 <文件> <输出文件>
 *   java DeskTool wav <文件> <输出文件>
 *   java DeskTool decode <文件>…            (整轨样本 MD5 / 采样数 / 坏帧数;FLAC 再核对 STREAMINFO 里的 MD5)
 *   java DeskTool selftest
 */
public class DeskTool {
    static final class FileTemp extends ChannelRio implements TagFile.Temp {
        final RandomAccessFile raf;
        final File f;
        FileTemp(File f, RandomAccessFile raf) { super(raf.getChannel(), true); this.f = f; this.raf = raf; }
        public String path() { return f.getPath(); }
        public void delete() { close(); f.delete(); }
        public void close() { try { raf.close(); } catch (Exception ignored) {} }
    }

    static TagFile.Workspace workspace(final File dir) {
        return new TagFile.Workspace() {
            public TagFile.Temp newTemp() {
                try {
                    dir.mkdirs();
                    File f = new File(dir, "t" + UUID.randomUUID() + ".tmp");
                    return new FileTemp(f, new RandomAccessFile(f, "rw"));
                } catch (Exception e) { return null; }
            }
            public boolean journalBegin(TagFile.Temp t, TagFile.Journal j) { return true; }
            public void journalEnd(TagFile.Temp t) {}
        };
    }

    public static void main(String[] a) throws Exception {
        String cmd = a[0];
        if (cmd.equals("selftest")) {
            SelfTest.Result r = SelfTest.run(workspace(new File(System.getProperty("java.io.tmpdir"), "tagrepair_selftest")));
            System.out.println(r.log);
            System.out.println("pass " + r.pass + " fail " + r.fail);
            System.exit(r.fail == 0 ? 0 : 1);
        }
        if (cmd.equals("meta")) {
            for (int i = 1; i < a.length; i++) {
                RandomAccessFile raf = new RandomAccessFile(a[i], "r");
                Rio rio = new ChannelRio(raf.getChannel(), false);
                FlacMeta.Layout l = new FlacMeta.Layout();
                FlacMeta.readLayout(rio, l);
                byte[] m = Rio.Util.range(rio, l.metaAt + 38, l.audioAt - l.metaAt - 38);
                System.out.println(a[i] + " metaAfterStreamInfo=" + m.length + " sha=" + Crc.hex(Crc.md("SHA-256").digest(m)).substring(0, 16));
                raf.close();
            }
            return;
        }
        if (cmd.equals("copy24") || cmd.equals("wav")) {
            RandomAccessFile in = new RandomAccessFile(a[1], "r");
            final File out = new File(a[2]);
            CopyFlac.Target t = new CopyFlac.Target() {
                RandomAccessFile raf;
                public String name() { return out.getName(); }
                public boolean exists() { return out.exists(); }
                public Rio create() { try { raf = new RandomAccessFile(out, "rw"); return new ChannelRio(raf.getChannel(), true); } catch (Exception e) { return null; } }
                public void discard() { try { raf.close(); } catch (Exception ignored) {} out.delete(); }
                public void finish() { try { raf.close(); } catch (Exception ignored) {} }
            };
            CopyFlac.Report r = CopyFlac.copy(new ChannelRio(in.getChannel(), false), cmd.equals("wav") ? CopyFlac.Kind.WAV_TO_FLAC : CopyFlac.Kind.TO_24BIT, t,
                    new CopyFlac.Options(), workspace(new File(out.getPath() + ".tmpdir")));
            System.out.println(r.outcome + " " + r.code + " " + r.why.both() + " frames=" + r.frames + " md5=" + r.pcmMd5);
            return;
        }
        for (int i = 1; i < a.length; i++) {
            File f = new File(a[i]);
            if (cmd.equals("scan")) {
                RandomAccessFile raf = new RandomAccessFile(f, "r");
                TagFile.Scan s = TagFile.scan(new ChannelRio(raf.getChannel(), false), TagRule.DEFAULT);
                raf.close();
                StringBuilder b = new StringBuilder(f.getName()).append(" | ").append(Classify.stateCode(s.state)).append(" | ").append(TagFile.stateMsg(s).zh)
                        .append(" | container=").append(s.info.container).append(" codec=").append(s.info.codec).append(" rate=").append(s.info.rate)
                        .append(" bits=").append(s.info.bits).append(" ch=").append(s.info.channels).append(" marker=").append(s.info.marker)
                        .append(" origRate=").append(s.info.origRate);
                for (int k = 0; k < 3; k++) b.append(" ").append(TagRule.keyName(k)).append("=").append(s.seen.count[k]).append(":").append(TagRule.shown(s.seen.value[k]));
                b.append(" actions=").append(s.canRepair ? "repair " : "").append(s.canWavToFlac ? "wav_to_flac " : "").append(s.canCopy24 ? "copy_24bit" : "");
                System.out.println(b);
            } else if (cmd.equals("fix")) {
                RandomAccessFile raf = new RandomAccessFile(f, "rw");
                TagFile.FixReport r = TagFile.fix(new ChannelRio(raf.getChannel(), true), true, new TagFile.FixOptions(), workspace(new File(f.getPath() + ".tmpdir")));
                raf.close();
                new File(f.getPath() + ".tmpdir").delete();
                System.out.println(f.getName() + " | " + r.outcome + " " + r.code + " | " + r.why.zh + " | mode=" + (r.inPlace ? "in_place" : "rewrite") + " sha="
                        + r.audioSha256 + " md5=" + r.pcmMd5 + " frames=" + r.pcmFrames);
            } else if (cmd.equals("decode")) {
                RandomAccessFile raf = new RandomAccessFile(f, "r");
                Rio rio = new ChannelRio(raf.getChannel(), false);
                Probe.Info info = Probe.inspect(rio);
                long t0 = System.nanoTime();
                Probe.Pcm p = Probe.decodeAll(rio, info, null, null);
                long ms = (System.nanoTime() - t0) / 1000000;
                String native5 = "";
                if (info.layout != null) {
                    final java.security.MessageDigest md = Crc.md("MD5");
                    final int bits = info.bits, bytes = (bits + 7) / 8;
                    final byte[] buf = new byte[1 << 16];
                    Probe.decodeAll(rio, info, null, new Probe.Sink() {
                        public void samples(int[] lr, int n) {
                            int q = 0;
                            for (int j = 0; j < n; j++) {
                                int v = lr[j] >> (32 - bits);
                                for (int bb = 0; bb < bytes; bb++) buf[q++] = (byte) (v >>> (8 * bb));
                                if (q + 8 > buf.length) { md.update(buf, 0, q); q = 0; }
                            }
                            md.update(buf, 0, q);
                        }
                    });
                    byte[] want = new byte[16];
                    System.arraycopy(info.layout.streamInfo, 18, want, 0, 16);
                    String w = Crc.hex(want), g = Crc.hex(md.digest());
                    native5 = w.equals("00000000000000000000000000000000") ? " streaminfo_md5=unset" : (w.equals(g) ? " streaminfo_md5=OK" : " streaminfo_md5=MISMATCH");
                }
                raf.close();
                System.out.println(f.getName() + " | md5=" + p.md5 + " frames=" + p.frames + " errors=" + p.errors + native5 + " " + ms + "ms");
            }
        }
    }
}
