package org.lwjgl.input;

import org.lwjgl.LWJGLException;
import org.lwjgl.opengl.Display;

public final class Mouse {
    private static boolean created;
    private static int[] event;

    private Mouse() {}

    public static void create() throws LWJGLException {
        if (!Display.isCreated() || !nCreate()) throw new LWJGLException("No X11 input window");
        created = true;
    }

    public static void destroy() {
        nDestroy();
        event = null;
        created = false;
    }

    public static boolean isCreated() { return created; }
    public static boolean next() {
        event = created ? nNext() : null;
        return event != null;
    }

    public static int getEventButton() { return event == null ? -1 : event[0]; }
    public static boolean getEventButtonState() { return event != null && event[1] != 0; }
    public static int getEventX() { return event == null ? 0 : event[2]; }
    public static int getEventY() { return event == null ? 0 : event[3]; }
    public static int getEventDWheel() { return event == null ? 0 : event[4]; }
    public static int getX() { return created ? nValue(0) : 0; }
    public static int getY() { return created ? nValue(1) : 0; }
    public static int getDX() { return created ? nValue(2) : 0; }
    public static int getDY() { return created ? nValue(3) : 0; }
    public static boolean isButtonDown(int button) {
        if (button < 0 || button >= 3) throw new IndexOutOfBoundsException("button");
        return created && nValue(4 + button) != 0;
    }

    public static void setGrabbed(boolean grabbed) {
        if (!created || !nGrab(grabbed)) throw new IllegalStateException("Cannot grab the X11 pointer");
    }

    public static void setCursorPosition(int x, int y) {
        if (created) nPosition(x, y);
    }

    private static native boolean nCreate();
    private static native void nDestroy();
    private static native int[] nNext();
    private static native int nValue(int value);
    private static native boolean nGrab(boolean grabbed);
    private static native void nPosition(int x, int y);
}
