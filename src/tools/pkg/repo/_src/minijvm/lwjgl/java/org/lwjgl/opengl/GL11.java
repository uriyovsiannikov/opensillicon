package org.lwjgl.opengl;

import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.ShortBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;

public final class GL11 {
    public static final int GL_NO_ERROR = 0;
    public static final int GL_DEPTH_BUFFER_BIT = 0x0100;
    public static final int GL_COLOR_BUFFER_BIT = 0x4000;
    public static final int GL_TRIANGLES = 0x0004;
    public static final int GL_LESS = 0x0201;
    public static final int GL_GREATER = 0x0204;
    public static final int GL_SRC_ALPHA = 0x0302;
    public static final int GL_ONE_MINUS_SRC_ALPHA = 0x0303;
    public static final int GL_DEPTH_TEST = 0x0b71;
    public static final int GL_ALPHA_TEST = 0x0bc0;
    public static final int GL_BLEND = 0x0be2;
    public static final int GL_MODELVIEW = 0x1700;
    public static final int GL_PROJECTION = 0x1701;
    public static final int GL_VENDOR = 0x1f00;
    public static final int GL_RENDERER = 0x1f01;
    public static final int GL_VERSION = 0x1f02;

    private GL11() {}

    public static native void glClear(int mask);
    public static native void glClearColor(float red, float green, float blue, float alpha);
    public static native void glViewport(int x, int y, int width, int height);
    public static native void glEnable(int capability);
    public static native void glDisable(int capability);
    public static native void glAlphaFunc(int function, float reference);
    public static native void glBlendFunc(int source, int destination);
    public static native void glDepthFunc(int function);
    public static native void glDepthMask(boolean enabled);
    public static native void glBegin(int mode);
    public static native void glEnd();
    public static native void glColor3f(float red, float green, float blue);
    public static native void glColor4f(float red, float green, float blue, float alpha);
    public static native void glVertex3f(float x, float y, float z);
    public static native void glMatrixMode(int mode);
    public static native void glLoadIdentity();
    public static native void glFlush();
    public static native int glGetError();
    public static native String glGetString(int name);

    public static native void glBindTexture(int target, int texture);
    public static native void glCallList(int list);
    public static native void glColorMask(boolean red, boolean green, boolean blue, boolean alpha);
    public static native void glColorMaterial(int face, int mode);
    public static native void glCopyTexSubImage2D(int target, int level, int xoffset, int yoffset, int x, int y, int width, int height);
    public static native void glCullFace(int mode);
    public static native void glDeleteLists(int list, int range);
    public static native void glDisableClientState(int state);
    public static native void glDrawArrays(int mode, int first, int count);
    public static native void glEnableClientState(int state);
    public static native void glEndList();
    public static native void glFogf(int parameter, float value);
    public static native void glFogi(int parameter, int value);
    public static native void glLineWidth(float width);
    public static native void glNewList(int list, int mode);
    public static native void glNormal3f(float x, float y, float z);
    public static native void glOrtho(double left, double right, double bottom, double top, double near, double far);
    public static native void glPixelStorei(int parameter, int value);
    public static native void glPolygonOffset(float factor, float units);
    public static native void glPopMatrix();
    public static native void glPushMatrix();
    public static native void glRotatef(float angle, float x, float y, float z);
    public static native void glScaled(double x, double y, double z);
    public static native void glScalef(float x, float y, float z);
    public static native void glShadeModel(int mode);
    public static native void glTexGeni(int coordinate, int parameter, int value);
    public static native void glTexParameteri(int target, int parameter, int value);
    public static native void glTranslatef(float x, float y, float z);
    public static native void glClearDepth(double depth);
    public static native void glTexCoord2f(float s, float t);
    public static native int glGenLists(int range);

    public static void glMultMatrix(FloatBuffer buffer) {
        if (buffer.remaining() < 16) throw new IllegalArgumentException("Sixteen matrix values are required");
        float[] values = new float[16];
        for (int i = 0; i < 16; i++) values[i] = buffer.get(buffer.position() + i);
        nMultMatrix(values);
    }

    private static native void nMultMatrix(float[] values);

    private static float[] vector(FloatBuffer buffer) {
        if (buffer.remaining() < 4) throw new IllegalArgumentException("Four float values are required");
        float[] values = new float[4];
        for (int i = 0; i < 4; i++) values[i] = buffer.get(buffer.position() + i);
        return values;
    }

    public static void glFog(int parameter, FloatBuffer buffer) {
        nFog(parameter, vector(buffer));
    }

    public static void glLight(int light, int parameter, FloatBuffer buffer) {
        nLight(light, parameter, vector(buffer));
    }

    public static void glLightModel(int parameter, FloatBuffer buffer) {
        nLightModel(parameter, vector(buffer));
    }

    public static void glTexGen(int coordinate, int parameter, FloatBuffer buffer) {
        nTexGen(coordinate, parameter, vector(buffer));
    }

    public static void glGetFloat(int parameter, FloatBuffer buffer) {
        float[] values = nGetFloat(parameter);
        if (buffer.remaining() < values.length || buffer.isReadOnly())
            throw new IllegalArgumentException("Output buffer cannot hold the requested state");
        for (int i = 0; i < values.length; i++) buffer.put(buffer.position() + i, values[i]);
    }

    public static void glGetInteger(int parameter, IntBuffer buffer) {
        int[] values = nGetInteger(parameter);
        if (buffer.remaining() < values.length || buffer.isReadOnly())
            throw new IllegalArgumentException("Output buffer cannot hold the requested state");
        for (int i = 0; i < values.length; i++) buffer.put(buffer.position() + i, values[i]);
    }

    public static void glGenTextures(IntBuffer buffer) {
        if (buffer.isReadOnly()) throw new IllegalArgumentException("Read-only texture buffer");
        int[] values = nGenTextures(buffer.remaining());
        for (int i = 0; i < values.length; i++) buffer.put(buffer.position() + i, values[i]);
    }

    public static void glDeleteTextures(IntBuffer buffer) {
        int[] values = new int[buffer.remaining()];
        for (int i = 0; i < values.length; i++) values[i] = buffer.get(buffer.position() + i);
        nDeleteTextures(values);
    }

    public static void glCallLists(IntBuffer buffer) {
        int[] values = new int[buffer.remaining()];
        for (int i = 0; i < values.length; i++) values[i] = buffer.get(buffer.position() + i);
        nCallLists(values);
    }

    private static void checkPixels(ByteBuffer buffer, int width, int height, int format, int type, boolean pack) {
        int bytes = nPixelBytes(width, height, format, type, pack);
        if (!buffer.hasArray() || bytes > buffer.remaining() || (pack && buffer.isReadOnly())) {
            throw new IllegalArgumentException("Pixel buffer is too small or inaccessible");
        }
    }

    public static void glTexImage2D(int target, int level, int internalFormat, int width,
            int height, int border, int format, int type, ByteBuffer pixels) {
        if (pixels != null) checkPixels(pixels, width, height, format, type, false);
        nTexImage2D(target, level, internalFormat, width, height, border, format, type,
                pixels == null ? null : pixels.array(), pixels == null ? 0 : pixels.arrayOffset() + pixels.position());
    }

    public static void glTexSubImage2D(int target, int level, int x, int y, int width,
            int height, int format, int type, ByteBuffer pixels) {
        checkPixels(pixels, width, height, format, type, false);
        nTexSubImage2D(target, level, x, y, width, height, format, type,
                pixels.array(), pixels.arrayOffset() + pixels.position());
    }

    public static void glReadPixels(int x, int y, int width, int height,
            int format, int type, ByteBuffer pixels) {
        checkPixels(pixels, width, height, format, type, true);
        nReadPixels(x, y, width, height, format, type,
                pixels.array(), pixels.arrayOffset() + pixels.position());
    }

    private static native void nFog(int parameter, float[] values);
    private static native void nLight(int light, int parameter, float[] values);
    private static native void nLightModel(int parameter, float[] values);
    private static native void nTexGen(int coordinate, int parameter, float[] values);
    private static native float[] nGetFloat(int parameter);
    private static native int[] nGetInteger(int parameter);
    private static native int[] nGenTextures(int count);
    private static native void nDeleteTextures(int[] textures);
    private static native void nCallLists(int[] lists);
    private static native int nPixelBytes(int width, int height, int format, int type, boolean pack);
    private static native void nTexImage2D(int target, int level, int internalFormat, int width,
            int height, int border, int format, int type, byte[] pixels, int offset);
    private static native void nTexSubImage2D(int target, int level, int x, int y, int width,
            int height, int format, int type, byte[] pixels, int offset);
    private static native void nReadPixels(int x, int y, int width, int height,
            int format, int type, byte[] pixels, int offset);

    // OpenGL retains client-array pointers until the corresponding binding changes.
    private static Buffer vertexArray;
    private static Buffer normalArray;
    private static Buffer colorArray;
    private static final Buffer[] textureArrays = new Buffer[32];

    private static int bufferOffset(Buffer buffer, int elementSize) {
        int arrayOffset = 0;
        if (buffer instanceof ByteBuffer && ((ByteBuffer) buffer).hasArray())
            arrayOffset = ((ByteBuffer) buffer).arrayOffset();
        else if (buffer instanceof FloatBuffer && ((FloatBuffer) buffer).hasArray())
            arrayOffset = ((FloatBuffer) buffer).arrayOffset();
        else if (buffer instanceof ShortBuffer && ((ShortBuffer) buffer).hasArray())
            arrayOffset = ((ShortBuffer) buffer).arrayOffset();
        return (arrayOffset + buffer.position()) * elementSize;
    }

    public static void glVertexPointer(int size, int stride, FloatBuffer buffer) {
        nPointer(0, size, 0x1406, stride, buffer, bufferOffset(buffer, 4), buffer.remaining() * 4);
        vertexArray = buffer;
    }

    public static void glTexCoordPointer(int size, int stride, FloatBuffer buffer) {
        int unit = nPointer(1, size, 0x1406, stride, buffer, bufferOffset(buffer, 4), buffer.remaining() * 4);
        textureArrays[unit] = buffer;
    }

    public static void glTexCoordPointer(int size, int stride, ShortBuffer buffer) {
        int unit = nPointer(1, size, 0x1402, stride, buffer, bufferOffset(buffer, 2), buffer.remaining() * 2);
        textureArrays[unit] = buffer;
    }

    public static void glColorPointer(int size, boolean unsigned, int stride, ByteBuffer buffer) {
        nPointer(2, size, unsigned ? 0x1401 : 0x1400, stride, buffer, bufferOffset(buffer, 1), buffer.remaining());
        colorArray = buffer;
    }

    public static void glNormalPointer(int stride, ByteBuffer buffer) {
        nPointer(3, 3, 0x1400, stride, buffer, bufferOffset(buffer, 1), buffer.remaining());
        normalArray = buffer;
    }

    public static void glVertexPointer(int size, int type, int stride, long offset) {
        throw new UnsupportedOperationException("Vertex buffer objects are unavailable");
    }

    public static void glTexCoordPointer(int size, int type, int stride, long offset) {
        throw new UnsupportedOperationException("Vertex buffer objects are unavailable");
    }

    public static void glColorPointer(int size, int type, int stride, long offset) {
        throw new UnsupportedOperationException("Vertex buffer objects are unavailable");
    }

    public static void glNormalPointer(int type, int stride, long offset) {
        throw new UnsupportedOperationException("Vertex buffer objects are unavailable");
    }

    static void releaseArrays() {
        vertexArray = normalArray = colorArray = null;
        java.util.Arrays.fill(textureArrays, null);
    }

    private static native int nPointer(int kind, int size, int type, int stride,
            Buffer buffer, int byteOffset, int byteLength);
}
