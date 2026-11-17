package java.awt;

final class X11Peer {
    private X11Peer() {}

    static native long createWindow(String title, int width, int height);
    static native void setTitle(long window, String title);
    static native long createCanvas(long parent, int width, int height);
    static native void center(long window, int width, int height);
    static native void resize(long window, int width, int height);
    static native void setMapped(long window, boolean mapped);
    static native void destroy(long window);
    static native void fillRect(long window, int rgb, int x, int y, int width, int height);
    static native int[] renderText(byte[] text);

    // -1 means close, 0 means no event, otherwise width and height occupy 32 bits each.
    static native long poll(long window);
}
