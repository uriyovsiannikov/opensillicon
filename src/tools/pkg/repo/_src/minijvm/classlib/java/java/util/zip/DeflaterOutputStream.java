package java.util.zip;

import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class DeflaterOutputStream extends FilterOutputStream {
    private final ZipStream stream;
    private final byte[] buffer;
    private final byte[] single = new byte[1];
    private static final byte[] EMPTY = new byte[0];
    private boolean finished;
    private boolean closed;

    public DeflaterOutputStream(OutputStream output) {
        this(output, 8192, 15);
    }

    protected DeflaterOutputStream(OutputStream output, int size, int windowBits) {
        super(output);
        if (output == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("Buffer size must be positive");
        buffer = new byte[size];
        stream = new ZipStream(true, windowBits);
    }

    public void write(int value) throws IOException {
        single[0] = (byte) value;
        write(single, 0, 1);
    }

    public void write(byte[] input, int offset, int length) throws IOException {
        if (closed || finished) throw new IOException("Compression stream has finished");
        if (offset < 0 || length < 0 || offset > input.length - length)
            throw new IndexOutOfBoundsException();
        while (length > 0) {
            int status = stream.process(input, offset, length, buffer, 0, buffer.length, 0);
            int consumed = stream.progress[0], produced = stream.progress[1];
            if (status < 0 || consumed == 0 && produced == 0)
                throw new IOException("Compression failed: " + status);
            out.write(buffer, 0, produced);
            offset += consumed;
            length -= consumed;
        }
    }

    public void finish() throws IOException {
        if (closed) throw new IOException("Stream is closed");
        while (!finished) {
            int status = stream.process(EMPTY, 0, 0, buffer, 0, buffer.length, 4);
            out.write(buffer, 0, stream.progress[1]);
            finished = status == 1;
            if (status < 0 || !finished && stream.progress[1] == 0)
                throw new IOException("Cannot finish compressed data: " + status);
        }
    }

    public void flush() throws IOException {
        if (closed) throw new IOException("Stream is closed");
        out.flush();
    }

    public void close() throws IOException {
        if (!closed) {
            try {
                finish();
            } finally {
                closed = true;
                stream.end();
                out.close();
            }
        }
    }
}
