package java.awt.image;

public class WritableRaster extends Raster {
    public WritableRaster(int[] pixels, int width, int height) {
        super(new DataBufferInt(pixels, width * height), width, height);
    }
}
