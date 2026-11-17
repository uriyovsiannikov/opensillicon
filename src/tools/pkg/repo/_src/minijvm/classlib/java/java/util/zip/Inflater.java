package java.util.zip;

public class Inflater {
    private final ZipStream stream;
    private byte[] input = new byte[0];
    private int offset;
    private int remaining;
    private boolean finished;
    private boolean dictionary;

    public Inflater() { this(false); }
    public Inflater(boolean nowrap) { this(nowrap ? -15 : 15); }
    Inflater(int windowBits) { stream = new ZipStream(false, windowBits); }

    public void setInput(byte[] bytes) { setInput(bytes, 0, bytes.length); }

    public void setInput(byte[] bytes, int offset, int length) {
        if (offset < 0 || length < 0 || offset > bytes.length - length)
            throw new ArrayIndexOutOfBoundsException();
        this.input = bytes;
        this.offset = offset;
        remaining = length;
    }

    public int inflate(byte[] output) throws DataFormatException {
        return inflate(output, 0, output.length);
    }

    public int inflate(byte[] output, int offset, int length) throws DataFormatException {
        if (offset < 0 || length < 0 || offset > output.length - length)
            throw new ArrayIndexOutOfBoundsException();
        if (length == 0 || finished) return 0;
        int result = stream.process(input, this.offset, remaining, output, offset, length, 0);
        this.offset += stream.progress[0];
        remaining -= stream.progress[0];
        finished = result == 1;
        dictionary = result == 2;
        if (result < 0 && result != -5) throw new DataFormatException("inflate returned " + result);
        return stream.progress[1];
    }

    public boolean needsInput() { return remaining == 0; }
    public boolean needsDictionary() { return dictionary; }
    public boolean finished() { return finished; }
    public int getRemaining() { return remaining; }

    public void reset() {
        stream.reset();
        offset = remaining = 0;
        finished = dictionary = false;
        input = new byte[0];
    }

    public void end() { stream.end(); }
}
