package java.util.zip;

import java.io.IOException;
import java.io.InputStream;

public class GZIPInputStream extends InflaterInputStream {
    public GZIPInputStream(InputStream input) throws IOException {
        this(input, 8192);
    }

    public GZIPInputStream(InputStream input, int size) throws IOException {
        super(input, new Inflater(31), size, true);
    }
}
