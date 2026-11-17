package org.lwjgl.opengl;

public final class ContextCapabilities {
    public final boolean OpenGL13;
    public final boolean GL_ARB_multitexture;
    public final boolean GL_ARB_occlusion_query = false;
    public final boolean GL_ARB_vertex_buffer_object = false;
    public final boolean GL_NV_fog_distance;

    ContextCapabilities() {
        String version = GL11.glGetString(GL11.GL_VERSION);
        OpenGL13 = version != null && !version.startsWith("1.0") &&
                !version.startsWith("1.1") && !version.startsWith("1.2");
        String extensions = " " + GL11.glGetString(0x1f03) + " ";
        GL_ARB_multitexture = extensions.indexOf(" GL_ARB_multitexture ") >= 0;
        GL_NV_fog_distance = extensions.indexOf(" GL_NV_fog_distance ") >= 0;
    }
}
