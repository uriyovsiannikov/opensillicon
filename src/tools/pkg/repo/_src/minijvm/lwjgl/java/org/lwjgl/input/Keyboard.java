package org.lwjgl.input;

import org.lwjgl.LWJGLException;
import org.lwjgl.opengl.Display;

public final class Keyboard {
    private static boolean created;
    private static int[] event;
    private static final String[] names = new String[256];

    static {
        String[] initial = {"NONE","ESCAPE","1","2","3","4","5","6","7","8","9","0",
            "MINUS","EQUALS","BACK","TAB","Q","W","E","R","T","Y","U","I","O","P",
            "LBRACKET","RBRACKET","RETURN","LCONTROL","A","S","D","F","G","H","J","K","L",
            "SEMICOLON","APOSTROPHE","GRAVE","LSHIFT","BACKSLASH","Z","X","C","V","B","N",
            "M","COMMA","PERIOD","SLASH","RSHIFT","MULTIPLY","LMENU","SPACE","CAPITAL",
            "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","NUMLOCK","SCROLL"};
        System.arraycopy(initial, 0, names, 0, initial.length);
        names[87] = "F11"; names[88] = "F12";
        names[157] = "RCONTROL"; names[184] = "RMENU";
        names[199] = "HOME"; names[200] = "UP"; names[201] = "PRIOR";
        names[203] = "LEFT"; names[205] = "RIGHT"; names[207] = "END";
        names[208] = "DOWN"; names[209] = "NEXT"; names[210] = "INSERT"; names[211] = "DELETE";
    }

    private Keyboard() {}

    public static void create() throws LWJGLException {
        if (!Display.isCreated() || !nCreate()) throw new LWJGLException("No X11 input window");
        created = true;
    }

    public static void destroy() {
        nDestroy();
        created = false;
        event = null;
    }

    public static boolean isCreated() { return created; }

    public static boolean next() {
        event = created ? nNext() : null;
        return event != null;
    }

    public static int getEventKey() { return event == null ? 0 : event[0]; }
    public static char getEventCharacter() { return event == null ? 0 : (char) event[1]; }
    public static boolean getEventKeyState() { return event != null && event[2] != 0; }

    public static boolean isKeyDown(int key) {
        if (key < 0 || key >= names.length) throw new IndexOutOfBoundsException("key");
        return created && nIsKeyDown(key);
    }

    public static String getKeyName(int key) {
        if (key < 0 || key >= names.length) return null;
        return names[key];
    }

    public static void enableRepeatEvents(boolean enabled) {
        nRepeat(enabled);
    }

    private static native boolean nCreate();
    private static native void nDestroy();
    private static native int[] nNext();
    private static native boolean nIsKeyDown(int key);
    private static native void nRepeat(boolean enabled);
}
