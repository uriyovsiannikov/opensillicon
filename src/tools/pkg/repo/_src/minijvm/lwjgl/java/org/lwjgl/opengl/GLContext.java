package org.lwjgl.opengl;

public final class GLContext {
    private static ContextCapabilities capabilities;

    private GLContext() {}

    public static ContextCapabilities getCapabilities() {
        synchronized (Display.class) {
            if (!Display.isCreated()) {
                throw new IllegalStateException("No OpenGL context is available");
            }
            if (capabilities == null) {
                capabilities = new ContextCapabilities();
            }
            return capabilities;
        }
    }

    static void clearCapabilities() {
        synchronized (Display.class) {
            capabilities = null;
        }
    }
}
