package org.lwjgl.input;

import org.lwjgl.LWJGLException;

public final class Controllers {
    private Controllers() {}

    public static void create() throws LWJGLException {
        throw new LWJGLException("Game controllers are unavailable on OpenSillicon");
    }
}
