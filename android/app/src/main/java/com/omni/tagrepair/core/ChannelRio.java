package com.omni.tagrepair.core;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

/** 一个 FileChannel(安卓上来自系统给的文件描述符,电脑上来自 RandomAccessFile)当 Rio 用。 */
public class ChannelRio implements Rio {
    protected final FileChannel ch;
    private final boolean writable;

    public ChannelRio(FileChannel ch, boolean writable) {
        this.ch = ch;
        this.writable = writable;
    }

    public long size() {
        try { return ch.size(); } catch (IOException e) { return -1; }
    }

    public boolean read(long at, byte[] dst, int off, int n) {
        if (at < 0 || n < 0) return false;
        ByteBuffer b = ByteBuffer.wrap(dst, off, n);
        try {
            while (b.hasRemaining()) {
                int k = ch.read(b, at + (b.position() - off));
                if (k < 0) return false;
            }
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public boolean write(long at, byte[] src, int off, int n) {
        if (!writable) return false;
        ByteBuffer b = ByteBuffer.wrap(src, off, n);
        try {
            while (b.hasRemaining()) ch.write(b, at + (b.position() - off));
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public boolean truncate(long size) {
        if (!writable) return false;
        try {
            if (ch.size() > size) ch.truncate(size);
            return ch.size() == size;
        } catch (IOException e) {
            return false;
        }
    }

    public boolean sync() {
        if (!writable) return true;
        try { ch.force(true); return true; } catch (IOException e) { return false; }
    }
}
