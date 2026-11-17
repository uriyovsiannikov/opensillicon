package org.lwjgl.input;

import java.nio.IntBuffer;
import org.lwjgl.LWJGLException;

public final class Cursor {
    private long handle;

    public Cursor(int width, int height, int xHotspot, int yHotspot, int images,
            IntBuffer pixels, IntBuffer delays) throws LWJGLException {
        if (width < 1 || width > 256 || height < 1 || height > 256 || images != 1 ||
                xHotspot < 0 || xHotspot >= width || yHotspot < 0 || yHotspot >= height ||
                pixels.remaining() < width * height) {
            throw new LWJGLException("Unsupported cursor dimensions, hotspot or animation");
        }
        int[] copy = new int[width * height];
        for (int i = 0; i < copy.length; i++) copy[i] = pixels.get(pixels.position() + i);
        handle = nCreate(width, height, xHotspot, yHotspot, copy);
        if (handle == 0) throw new LWJGLException("Cannot create the X11 cursor");
    }

    public void destroy() {
        if (handle != 0) {
            nDestroy(handle);
            handle = 0;
        }
    }

    private static native long nCreate(int width, int height, int xHotspot, int yHotspot, int[] pixels);
    private static native void nDestroy(long handle);
}
