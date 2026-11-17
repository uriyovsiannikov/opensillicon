package org.lwjgl.opengl;

public final class GL13 {
    public static final int GL_TEXTURE0 = 0x84c0;
    public static final int GL_TEXTURE1 = 0x84c1;

    private GL13() {}

    public static native void glActiveTexture(int texture);
    public static native void glClientActiveTexture(int texture);
    public static native void glMultiTexCoord2f(int texture, float s, float t);
}
