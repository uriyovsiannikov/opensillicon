package java.awt.image;

public final class DataBufferInt extends DataBuffer {
    private final int[] data;

    public DataBufferInt(int size) {
        this(new int[size], size);
    }

    public DataBufferInt(int[] data, int size) {
        super(TYPE_INT, size);
        if (size < 0 || size > data.length) throw new IllegalArgumentException("Invalid buffer size");
        this.data = data;
    }

    public int[] getData() { return data; }
    public int getElem(int index) { return data[index]; }
    public void setElem(int index, int value) { data[index] = value; }
}
