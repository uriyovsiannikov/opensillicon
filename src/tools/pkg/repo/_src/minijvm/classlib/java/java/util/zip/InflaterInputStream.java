package java.util.zip;

import java.io.EOFException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;

public class InflaterInputStream extends FilterInputStream {
    protected final Inflater inf;
    protected final byte[] buf;
    private final boolean ownsInflater;
    private boolean closed;
    private final byte[] decoded = new byte[8192];
    private int decodedOffset;
    private int decodedLength;

    public InflaterInputStream(InputStream input) {
        this(input, new Inflater(), 8192, true);
    }

    public InflaterInputStream(InputStream input, Inflater inflater) {
        this(input, inflater, 8192, false);
    }

    public InflaterInputStream(InputStream input, Inflater inflater, int size) {
        this(input, inflater, size, false);
    }

    protected InflaterInputStream(InputStream input, Inflater inflater, int size, boolean owned) {
        super(input);
        if (input == null || inflater == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("Buffer size must be positive");
        inf = inflater;
        buf = new byte[size];
        ownsInflater = owned;
    }

    public int read() throws IOException {
        if (closed) throw new IOException("Stream is closed");
        if (decodedOffset == decodedLength) {
            int count = inflate(decoded, 0, decoded.length);
            if (count < 0) return -1;
            decodedOffset = 0;
            decodedLength = count;
        }
        return decoded[decodedOffset++] & 255;
    }

    public int read(byte[] output, int offset, int length) throws IOException {
        if (closed) throw new IOException("Stream is closed");
        if (offset < 0 || length < 0 || offset > output.length - length)
            throw new IndexOutOfBoundsException();
        if (length == 0) return 0;
        if (decodedOffset < decodedLength) {
            int count = Math.min(length, decodedLength - decodedOffset);
            System.arraycopy(decoded, decodedOffset, output, offset, count);
            decodedOffset += count;
            return count;
        }
        return inflate(output, offset, length);
    }

    private int inflate(byte[] output, int offset, int length) throws IOException {
        try {
            while (!inf.finished()) {
                int read = inf.inflate(output, offset, length);
                if (read > 0) return read;
                if (inf.finished()) break;
                if (inf.needsDictionary()) throw new IOException("Compressed data requires a dictionary");
                if (!inf.needsInput()) throw new IOException("Decompression made no progress");
                read = in.read(buf);
                if (read == 0) {
                    int value = in.read();
                    if (value < 0) throw new EOFException("Truncated compressed data");
                    buf[0] = (byte) value;
                    read = 1;
                }
                if (read < 0) throw new EOFException("Truncated compressed data");
                inf.setInput(buf, 0, read);
            }
            return -1;
        } catch (DataFormatException failure) {
            throw new IOException(failure.getMessage());
        }
    }

    public long skip(long count) throws IOException {
        if (count < 0) throw new IllegalArgumentException("Negative skip length");
        byte[] scratch = new byte[4096];
        long remaining = count;
        while (remaining > 0) {
            int read = read(scratch, 0, (int) Math.min(remaining, scratch.length));
            if (read < 0) break;
            remaining -= read;
        }
        return count - remaining;
    }

    public synchronized void mark(int limit) {}

    public synchronized void reset() throws IOException {
        throw new IOException("Mark/reset is unavailable for compressed streams");
    }

    public int available() throws IOException {
        if (closed) throw new IOException("Stream is closed");
        return decodedOffset < decodedLength || !inf.finished() ? 1 : 0;
    }

    public void close() throws IOException {
        if (!closed) {
            closed = true;
            try {
                in.close();
            } finally {
                if (ownsInflater) inf.end();
            }
        }
    }

    public boolean markSupported() { return false; }
}
