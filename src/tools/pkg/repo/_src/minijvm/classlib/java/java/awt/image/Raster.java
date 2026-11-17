package java.awt.image;

public class Raster {
    private final DataBuffer data;
    private final int width;
    private final int height;

    protected Raster(DataBuffer data, int width, int height) {
        this.data = data;
        this.width = width;
        this.height = height;
    }

    public DataBuffer getDataBuffer() { return data; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public int getMinX() { return 0; }
    public int getMinY() { return 0; }
}
