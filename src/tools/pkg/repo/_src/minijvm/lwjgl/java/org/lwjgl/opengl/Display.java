package org.lwjgl.opengl;

import java.awt.AWTError;
import java.awt.BorderLayout;
import java.awt.Canvas;
import java.awt.Dimension;
import java.awt.Frame;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import org.lwjgl.LWJGLException;

public final class Display {
    private static Canvas parent;
    private static Canvas drawable;
    private static Frame ownedFrame;
    private static DisplayMode mode = new DisplayMode(640, 480);
    private static String title = "Game";
    private static boolean closeRequested;

    private Display() {}

    public static synchronized void setParent(Canvas canvas) throws LWJGLException {
        if (isCreated()) {
            throw new LWJGLException("Destroy the OpenGL context before changing its Canvas");
        }
        parent = canvas;
    }

    public static synchronized void setDisplayMode(DisplayMode newMode) throws LWJGLException {
        if (newMode == null) {
            throw new NullPointerException("mode");
        }
        if (isCreated()) {
            throw new LWJGLException("Destroy the OpenGL context before changing its display mode");
        }
        mode = newMode;
    }

    public static synchronized DisplayMode getDisplayMode() {
        return mode;
    }

    public static DisplayMode getDesktopDisplayMode() {
        long size = nDesktopSize();
        if (size == 0) {
            throw new IllegalStateException("Cannot open the X11 display");
        }
        return new DisplayMode((int) (size >>> 32), (int) size);
    }

    public static synchronized void setTitle(String newTitle) {
        title = newTitle == null ? "" : newTitle;
        if (ownedFrame != null) {
            ownedFrame.setTitle(title);
        }
    }

    public static void setFullscreen(boolean fullscreen) throws LWJGLException {
        if (fullscreen) {
            throw new LWJGLException("Fullscreen is unavailable with the TinyX software backend");
        }
    }

    public static void create() throws LWJGLException {
        create(new PixelFormat());
    }

    public static synchronized void create(PixelFormat format) throws LWJGLException {
        if (format == null) {
            throw new NullPointerException("format");
        }
        if (isCreated()) {
            throw new IllegalStateException("OpenGL context already created");
        }
        Frame frame = null;
        Canvas canvas = parent;
        try {
            if (canvas == null) {
                canvas = new Canvas();
                canvas.setPreferredSize(new Dimension(mode.getWidth(), mode.getHeight()));
                frame = new Frame(title);
                frame.add(canvas, BorderLayout.CENTER);
                frame.pack();
                frame.setLocationRelativeTo(null);
                frame.addWindowListener(new WindowAdapter() {
                    @Override
                    public void windowClosing(WindowEvent event) {
                        requestClose();
                    }
                });
                frame.setVisible(true);
            }
            if (!canvas.isDisplayable()
                    || !nCreate(canvas, canvas.getWidth(), canvas.getHeight(), format.depthBits)) {
                throw new LWJGLException("Cannot create a software OpenGL context on this Canvas");
            }
        } catch (AWTError failure) {
            if (frame != null) {
                frame.dispose();
            }
            throw new LWJGLException("Cannot open the OpenGL window", failure);
        } catch (LWJGLException failure) {
            if (frame != null) {
                frame.dispose();
            }
            throw failure;
        }
        drawable = canvas;
        ownedFrame = frame;
        closeRequested = false;
    }

    private static synchronized void requestClose() {
        closeRequested = true;
    }

    public static synchronized boolean isCreated() {
        return drawable != null;
    }

    public static synchronized boolean isCloseRequested() {
        return closeRequested || (drawable != null && !drawable.isDisplayable());
    }

    public static synchronized boolean isActive() {
        return drawable != null && drawable.isDisplayable() && nActive();
    }

    public static synchronized void swapBuffers() throws LWJGLException {
        if (drawable == null) {
            throw new IllegalStateException("OpenGL context is unavailable");
        }
        if (!nPresent(drawable, drawable.getWidth(), drawable.getHeight())) {
            throw new LWJGLException("Cannot present the software OpenGL frame to TinyX");
        }
    }

    public static void update() {
        try {
            swapBuffers();
        } catch (LWJGLException failure) {
            throw new IllegalStateException(failure);
        }
    }

    public static synchronized void destroy() {
        if (drawable != null) {
            org.lwjgl.input.Keyboard.destroy();
            org.lwjgl.input.Mouse.destroy();
            nDestroy();
            GL11.releaseArrays();
            GLContext.clearCapabilities();
            drawable = null;
        }
        if (ownedFrame != null) {
            ownedFrame.dispose();
            ownedFrame = null;
        }
        closeRequested = false;
    }

    private static native boolean nCreate(Canvas canvas, int width, int height, int depthBits);
    private static native boolean nPresent(Canvas canvas, int width, int height);
    private static native boolean nActive();
    private static native long nDesktopSize();
    private static native void nDestroy();
}
