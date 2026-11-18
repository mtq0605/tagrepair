package com.omni.tagrepair.core;

import java.io.IOException;

/**
 * 按位置读写一个文件(或内存)。read 要么读满 n 字节返回 true,要么返回 false。
 * 安卓上是系统给的文件描述符(FileChannel),电脑上测试时是普通文件;核心代码只认这个接口。
 */
public interface Rio {
    long size();
    boolean read(long at, byte[] dst, int off, int n);
    /** 写;不支持写的返回 false */
    boolean write(long at, byte[] src, int off, int n);
    /** 截到这么长;不支持返回 false */
    boolean truncate(long size);
    /** 写下去的都落到盘上 */
    boolean sync();

    /** 内存里的一段(只读) */
    final class Mem implements Rio {
        private final byte[] d;
        private final int n;
        public Mem(byte[] d, int n) { this.d = d; this.n = n; }
        public Mem(byte[] d) { this(d, d.length); }
        public long size() { return n; }
        public boolean read(long at, byte[] dst, int off, int k) {
            if (at < 0 || at > n || k > n - at) return false;
            System.arraycopy(d, (int) at, dst, off, k);
            return true;
        }
        public boolean write(long at, byte[] src, int off, int k) { return false; }
        public boolean truncate(long size) { return false; }
        public boolean sync() { return true; }
    }

    /** 读一段到新数组;读不了返回 null */
    final class Util {
        private Util() {}
        public static byte[] range(Rio r, long at, long n) {
            if (n < 0 || n > Integer.MAX_VALUE - 16) return null;
            byte[] b = new byte[(int) n];
            return r.read(at, b, 0, (int) n) ? b : null;
        }
        public static void closeQuietly(java.io.Closeable c) {
            if (c == null) return;
            try { c.close(); } catch (IOException ignored) {}
        }
    }
}
