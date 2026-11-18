package com.omni.tagrepair;

import android.os.ParcelFileDescriptor;
import android.system.Os;
import android.system.OsConstants;

import com.omni.tagrepair.core.Rio;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;

/** 系统给的文件描述符(content:// 打开的)当 Rio 用:按位置读写(pread / pwrite),截断、落盘。 */
final class FdRio implements Rio, Closeable {
    private final ParcelFileDescriptor pfd;
    private final FileDescriptor fd;
    final boolean writable;

    FdRio(ParcelFileDescriptor pfd, boolean writable) {
        this.pfd = pfd;
        this.fd = pfd.getFileDescriptor();
        this.writable = writable;
    }

    public long size() {
        try { return Os.fstat(fd).st_size; } catch (Exception e) { return -1; }
    }

    public boolean read(long at, byte[] dst, int off, int n) {
        if (at < 0 || n < 0) return false;
        try {
            int done = 0;
            while (done < n) {
                int k = Os.pread(fd, dst, off + done, n - done, at + done);
                if (k <= 0) return false;
                done += k;
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public boolean write(long at, byte[] src, int off, int n) {
        if (!writable) return false;
        try {
            int done = 0;
            while (done < n) {
                int k = Os.pwrite(fd, src, off + done, n - done, at + done);
                if (k <= 0) return false;
                done += k;
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public boolean truncate(long size) {
        if (!writable) return false;
        try {
            if (size() > size) Os.ftruncate(fd, size);
            return size() == size;
        } catch (Exception e) {
            return false;
        }
    }

    public boolean sync() {
        if (!writable) return true;
        try { Os.fsync(fd); return true; } catch (Exception e) { return false; }
    }

    /** 能不能按位置读(管道之类的不行) */
    boolean seekable() {
        try { Os.lseek(fd, 0, OsConstants.SEEK_CUR); return true; } catch (Exception e) { return false; }
    }

    public void close() throws IOException { pfd.close(); }
}
