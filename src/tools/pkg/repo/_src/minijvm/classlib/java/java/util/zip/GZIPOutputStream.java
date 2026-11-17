package java.util.zip;

import java.io.IOException;
import java.io.OutputStream;

public class GZIPOutputStream extends DeflaterOutputStream {
    public GZIPOutputStream(OutputStream output) throws IOException {
        this(output, 8192);
    }

    public GZIPOutputStream(OutputStream output, int size) throws IOException {
        super(output, size, 31);
    }
}
