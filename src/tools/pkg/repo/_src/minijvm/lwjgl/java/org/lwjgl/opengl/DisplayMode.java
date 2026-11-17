package org.lwjgl.opengl;

public final class DisplayMode {
    private final int width;
    private final int height;

    public DisplayMode(int width, int height) {
        if (width < 1 || height < 1) {
            throw new IllegalArgumentException("Display size must be positive");
        }
        this.width = width;
        this.height = height;
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
}
