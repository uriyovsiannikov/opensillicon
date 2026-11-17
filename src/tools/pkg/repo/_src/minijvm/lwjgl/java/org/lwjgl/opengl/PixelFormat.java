package org.lwjgl.opengl;

public final class PixelFormat {
    final int depthBits;

    public PixelFormat() {
        this(8);
    }

    private PixelFormat(int depthBits) {
        this.depthBits = depthBits;
    }

    public PixelFormat withDepthBits(int depthBits) {
        if (depthBits < 0 || depthBits > 32) {
            throw new IllegalArgumentException("Depth buffer size must be between 0 and 32");
        }
        return new PixelFormat(depthBits);
    }
}
