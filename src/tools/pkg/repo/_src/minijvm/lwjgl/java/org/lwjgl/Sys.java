package org.lwjgl;

public final class Sys {
    private Sys() {}

    public static String getVersion() {
        return "OpenSillicon OSMesa";
    }

    public static boolean openURL(String url) {
        return false;
    }

    public static long getTime() {
        return System.currentTimeMillis();
    }

    public static long getTimerResolution() {
        return 1000;
    }
}
