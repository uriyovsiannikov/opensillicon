/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package org.mini.fs;

import org.mini.net.SocketNative;

import java.io.IOException;

/**
 *
 * open file for random access
 *
 * filemode : r rw rws rwd
 *
 * <pre>
 *    void t17() {
 *        try {
 *            RandomAccessFile c = new RandomAccessFile("./c.txt", "rw");
 *            c.seek(0);
 *            String r = "这是一个测试";
 *            System.out.println(r);
 *            byte[] carr = r.getBytes("utf-8");
 *            c.write(carr, 0, carr.length);
 *            c.close();
 *            RandomAccessFile c1 = new RandomAccessFile("./c.txt", "r");
 *            c1.seek(0);
 *            byte[] barr = new byte[256];
 *            int len;
 *            len = c1.read(barr, 0, 256);
 *            System.out.println("len=" + len);
 *            c1.close();
 *            String s = new String(barr, 0, len, "utf-8");
 *            System.out.println(s);
 *        } catch (IOException ex) {
 *            System.out.println(ex.getMessage());
 *        }
 *    }
 * </pre>
 *
 * @author gust
 */
public class InnerRandomAccessFile extends InnerFile {

    boolean flush = false;

    public InnerRandomAccessFile(String path, String mode) throws IOException {
        this.path = path;
        boolean writable = !"r".equals(mode);
        if (!"r".equals(mode) && !"rw".equals(mode) && !"rws".equals(mode) && !"rwd".equals(mode)) {
            throw new IllegalArgumentException("Unsupported random-access mode");
        }
        flush = "rws".equals(mode) || "rwd".equals(mode);
        filePointer = openRandomFile(SocketNative.toCStyle(path), writable);
        if (filePointer == 0) {
            throw new IOException("open file error:" + path);
        }
    }

    private static native long openRandomFile(byte[] path, boolean writable);

    public void close() throws IOException {
        closeFile(getFilePointer());
        filePointer = 0;
    }

    public int read() throws IOException {
        if (filePointer == 0) throw new IOException("File is closed");
        return read0(filePointer);
    }

    public int read(byte[] bytes, int offset, int length) throws IOException {
        if (filePointer == 0) throw new IOException("File is closed");
        if (offset < 0 || length < 0 || offset > bytes.length - length)
            throw new IndexOutOfBoundsException();
        return length == 0 ? 0 : readbuf(filePointer, bytes, offset, length);
    }

    public int write(byte[] bytes, int offset, int length) throws IOException {
        if (filePointer == 0) throw new IOException("File is closed");
        if (offset < 0 || length < 0 || offset > bytes.length - length)
            throw new IndexOutOfBoundsException();
        int written = 0;
        while (written < length) {
            int count = writebuf(filePointer, bytes, offset + written, length - written);
            if (count <= 0) throw new IOException("Cannot write " + path);
            written += count;
        }
        if (flush && flush0(filePointer) != 0) throw new IOException("Cannot flush " + path);
        return written;
    }

    /** One native write for a big-endian DataOutput primitive. */
    public void writeNumber(long value, int bytes) throws IOException {
        if (filePointer == 0) throw new IOException("File is closed");
        if (bytes != 2 && bytes != 4 && bytes != 8) throw new IllegalArgumentException();
        if (writeNumber0(filePointer, value, bytes) != bytes)
            throw new IOException("Cannot write " + path);
        if (flush && flush0(filePointer) != 0) throw new IOException("Cannot flush " + path);
    }

    private static native int writeNumber0(long file, long value, int bytes);

    public long position() throws IOException {
        long position = position0(filePointer);
        if (position < 0) throw new IOException("Cannot get file position");
        return position;
    }

    public long size() throws IOException {
        long length = length0(filePointer);
        if (length < 0) throw new IOException("Cannot get file length");
        return length;
    }

    private static native long position0(long file);
    private static native long length0(long file);

    public int seek(long pos) throws IOException {
        return seek0(getFilePointer(), pos);
    }

    public int setLength(long length) throws IOException {
        return setLength0(getFilePointer(), length);
    }
}
