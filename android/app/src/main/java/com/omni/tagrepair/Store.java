package com.omni.tagrepair;

import android.content.Context;
import android.net.Uri;

import com.omni.tagrepair.core.ChannelRio;
import com.omni.tagrepair.core.TagFile;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.UUID;

/**
 * 本应用自己的目录:临时文件(files/work,不放 cache —— 系统清缓存会把核对过、还没写回的那份删掉)
 * 与写回登记(files/pending:<临时文件名>.txt 是 uri、长度、SHA-256、名字;<临时文件名>.old / .new 是修之前 / 修之后的文件开头)。
 * 登记的几个文件都先写到 .part、落盘、再改名,最后才写 .txt:.txt 在,登记就是完整的。
 * 只删自己建的、按名字认得出的临时文件;有任务在跑的时候一个都不删(见 Jobs)。
 */
final class Store {
    private final File work, pending;

    Store(Context c) {
        work = new File(c.getFilesDir(), "work");
        pending = new File(c.getFilesDir(), "pending");
        work.mkdirs();
        pending.mkdirs();
    }

    static final class FileTemp extends ChannelRio implements TagFile.Temp {
        final File f;
        final RandomAccessFile raf;
        FileTemp(File f, RandomAccessFile raf) { super(raf.getChannel(), true); this.f = f; this.raf = raf; }
        public String path() { return f.getPath(); }
        public void delete() {
            close();
            f.delete();
        }
        public void close() { try { raf.close(); } catch (Exception ignored) {} }
    }

    static boolean isOurTempName(String n) { return n.startsWith("t") && n.endsWith(".tmp") && n.length() == 1 + 36 + 4; }

    private static boolean writeDurably(File f, byte[] data) {
        File part = new File(f.getPath() + ".part");
        try {
            FileOutputStream o = new FileOutputStream(part);
            try {
                o.write(data);
                o.flush();
                o.getFD().sync();
            } finally {
                o.close();
            }
            return part.renameTo(f) && f.length() == data.length;
        } catch (Exception e) {
            part.delete();
            return false;
        }
    }

    /** 改名之后把目录也落盘:断电之后改过的名字还在 */
    private static boolean syncDir(File dir) {
        java.io.FileDescriptor fd = null;
        try {
            fd = android.system.Os.open(dir.getPath(), android.system.OsConstants.O_RDONLY, 0);
            android.system.Os.fsync(fd);
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            if (fd != null) try { android.system.Os.close(fd); } catch (Exception ignored) {}
        }
    }

    private static byte[] readAll(File f) {
        try {
            byte[] b = new byte[(int) f.length()];
            FileInputStream in = new FileInputStream(f);
            int off = 0;
            while (off < b.length) {
                int k = in.read(b, off, b.length - off);
                if (k < 0) break;
                off += k;
            }
            in.close();
            return off == b.length ? b : null;
        } catch (Exception e) {
            return null;
        }
    }

    /** 一个处理中的文件的工作区:temp 建在 work 里,登记时记下这个文件的 uri 与名字 */
    TagFile.Workspace forFile(final Uri uri, final String name) {
        return new TagFile.Workspace() {
            public TagFile.Temp newTemp() {
                try {
                    File f = new File(work, "t" + UUID.randomUUID() + ".tmp");
                    if (!f.createNewFile()) return null;
                    return new FileTemp(f, new RandomAccessFile(f, "rw"));
                } catch (Exception e) {
                    return null;
                }
            }
            public boolean journalBegin(TagFile.Temp t, TagFile.Journal j) {
                String base = new File(t.path()).getName();
                Properties p = new Properties();
                p.setProperty("uri", uri == null ? "" : uri.toString());
                p.setProperty("temp", t.path());
                p.setProperty("name", name == null ? "" : name);
                p.setProperty("oldSize", Long.toString(j.oldSize));
                p.setProperty("newSize", Long.toString(j.newSize));
                p.setProperty("oldAudioAt", Long.toString(j.oldAudioAt));
                p.setProperty("newAudioAt", Long.toString(j.newAudioAt));
                p.setProperty("newSha", j.newSha);
                p.setProperty("oldAudioSha", j.oldAudioSha);
                try {
                    java.io.StringWriter sw = new java.io.StringWriter();
                    p.store(sw, null);
                    boolean ok = writeDurably(new File(pending, base + ".old"), j.oldHead) && writeDurably(new File(pending, base + ".new"), j.newHead)
                            && writeDurably(new File(pending, base + ".txt"), sw.toString().getBytes(StandardCharsets.UTF_8));
                    if (ok) syncDir(pending);                       // 尽力:个别文件系统不支持给目录落盘,那就只靠上面每个文件的落盘
                    if (!ok) journalEnd(t);                         // 没写全:一个残片都不留
                    return ok;
                } catch (Exception e) {
                    journalEnd(t);
                    return false;
                }
            }
            public void journalEnd(TagFile.Temp t) { removeJournal(new File(t.path()).getName()); }
        };
    }

    private void removeJournal(String base) {
        new File(pending, base + ".txt").delete();       // .txt 先删:登记从这一刻起就不算数了
        new File(pending, base + ".old").delete();
        new File(pending, base + ".new").delete();
    }

    static final class Pending {
        String base;
        Uri uri;
        File temp;
        String name;
        TagFile.Journal journal = new TagFile.Journal();
    }

    List<Pending> pending() {
        List<Pending> out = new ArrayList<Pending>();
        File[] js = pending.listFiles();
        if (js == null) return out;
        for (File f : js) {
            String n = f.getName();
            if (!n.endsWith(".txt")) continue;
            try {
                Properties p = new Properties();
                byte[] txt = readAll(f);
                if (txt == null) continue;
                p.load(new java.io.StringReader(new String(txt, StandardCharsets.UTF_8)));
                Pending x = new Pending();
                x.base = n.substring(0, n.length() - 4);
                x.uri = Uri.parse(p.getProperty("uri", ""));
                x.temp = new File(work, x.base);
                x.name = p.getProperty("name", "");
                TagFile.Journal j = x.journal;
                j.oldSize = Long.parseLong(p.getProperty("oldSize"));
                j.newSize = Long.parseLong(p.getProperty("newSize"));
                j.oldAudioAt = Long.parseLong(p.getProperty("oldAudioAt"));
                j.newAudioAt = Long.parseLong(p.getProperty("newAudioAt"));
                j.newSha = p.getProperty("newSha", "");
                j.oldAudioSha = p.getProperty("oldAudioSha", "");
                j.oldHead = readAll(new File(pending, x.base + ".old"));
                j.newHead = readAll(new File(pending, x.base + ".new"));
                if (j.oldHead == null || j.newHead == null) continue;
                out.add(x);
            } catch (Exception ignored) {}
        }
        return out;
    }

    void done(Pending p) {
        removeJournal(p.base);
        p.temp.delete();
    }

    static FileTemp openTemp(File f) {
        try { return new FileTemp(f, new RandomAccessFile(f, "rw")); } catch (Exception e) { return null; }
    }

    /**
     * 没有任务在跑时(由 Jobs 保证)才调用:work 里没有登记的临时文件(上次被杀、还没登记就停下的)删掉;
     * pending 里没配齐的残片(没有 .txt 的 .old / .new / .part)删掉。只删这几种名字的文件
     */
    void removeStrayTemps() {
        List<String> keep = new ArrayList<String>();
        for (Pending p : pending()) keep.add(p.base);
        File[] fs = work.listFiles();
        if (fs != null) for (File f : fs) if (isOurTempName(f.getName()) && !keep.contains(f.getName())) f.delete();
        File[] ps = pending.listFiles();
        if (ps != null) for (File f : ps) {
            String n = f.getName();
            String base = n.endsWith(".part") ? n.substring(0, n.length() - 5) : n;
            int dot = base.lastIndexOf('.');
            String stem = dot > 0 ? base.substring(0, dot) : base;
            if (isOurTempName(stem) && !keep.contains(stem)) f.delete();
        }
    }

    /** 登记里还用得着的 uri(释放系统的长期授权时留着这些) */
    List<Uri> urisInUse() {
        List<Uri> l = new ArrayList<Uri>();
        for (Pending p : pending()) l.add(p.uri);
        return l;
    }
}
