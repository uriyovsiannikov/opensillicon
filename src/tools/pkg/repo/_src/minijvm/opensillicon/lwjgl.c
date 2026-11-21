/* SPDX-License-Identifier: MIT */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>

#include "jvm_util.h"
#include "profile.h"

Display *awt_x_display(void);
void lwjgl_input_close(void);

static struct {
    OSMesaContext context;
    XImage *image;
    unsigned char *previous;
    Pixmap backing;
    int previous_valid;
    GC gc;
    Window canvas;
    Window parent;
    Visual *visual;
    int depth;
} surface;

static struct client_array {
    int length;
    int components;
    int element_bytes;
    int stride;
} client_arrays[35];


Window lwjgl_canvas_window(void) { return surface.canvas; }
Window lwjgl_parent_window(void) { return surface.parent; }
void lwjgl_canvas_size(int *width, int *height)
{
    *width = surface.image ? surface.image->width : 0;
    *height = surface.image ? surface.image->height : 0;
}

static Window canvas_window(Runtime *runtime)
{
    Instance *canvas = localvar_getRefer(runtime->localvar, 0);
    FieldInfo *peer = find_fieldInfo_by_name_c("java/awt/Component", "peer", "J", NULL, runtime);
    return peer ? getFieldLong(getInstanceFieldPtr(canvas, peer)) : None;
}

static XImage *make_image(Display *display, int width, int height)
{
    // Limit allocation to 64 MiB and accept the native TinyX TrueColor layout.
    if (width < 1 || height < 1 || width > 4096 || height > 4096) {
        return NULL;
    }
    XImage *image = XCreateImage(display, surface.visual, surface.depth, ZPixmap, 0,
        NULL, width, height, 32, 0);
    if (!image) {
        return NULL;
    }
    if (image->bits_per_pixel != 32 || image->byte_order != LSBFirst ||
        image->red_mask != 0xff0000 || image->green_mask != 0xff00 || image->blue_mask != 0xff) {
        XDestroyImage(image);
        return NULL;
    }
    image->data = calloc(height, image->bytes_per_line);
    if (!image->data) {
        XDestroyImage(image);
        return NULL;
    }
    return image;
}

static int bind_image(Display *display, int width, int height)
{
    XImage *image = make_image(display, width, height);
    if (!image) {
        return 0;
    }
    if (!OSMesaMakeCurrent(surface.context, image->data, GL_UNSIGNED_BYTE, width, height)) {
        XDestroyImage(image);
        return 0;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    if (surface.image) {
        XDestroyImage(surface.image);
    }
    free(surface.previous);
    if (surface.backing) XFreePixmap(display, surface.backing);
    surface.previous = malloc((size_t)image->bytes_per_line * image->height);
    surface.backing = surface.previous ? XCreatePixmap(display, surface.canvas,
        width, height, surface.depth) : None;
    surface.previous_valid = 0;
    surface.image = image;
    return 1;
}

static void destroy_surface(Display *display)
{
    lwjgl_input_close();
    if (surface.context) {
        OSMesaDestroyContext(surface.context);
    }
    if (surface.image) {
        XDestroyImage(surface.image);
    }
    free(surface.previous);
    if (surface.backing) XFreePixmap(display, surface.backing);
    if (surface.gc) {
        XFreeGC(display, surface.gc);
    }
    memset(&surface, 0, sizeof(surface));
    memset(client_arrays, 0, sizeof(client_arrays));
}

static s32 create(Runtime *runtime, JClass *clazz)
{
    Display *display = awt_x_display();
    Window canvas = canvas_window(runtime);
    XWindowAttributes attributes;
    Window root, *children;
    unsigned count;
    if (!display || !canvas || surface.context ||
        !XGetWindowAttributes(display, canvas, &attributes) ||
        !XQueryTree(display, canvas, &root, &surface.parent, &children, &count)) {
        push_int(runtime->stack, 0);
        return RUNTIME_STATUS_NORMAL;
    }
    if (children) {
        XFree(children);
    }

    LocalVarItem *args = runtime->localvar;
    surface.visual = attributes.visual;
    surface.depth = attributes.depth;
    surface.canvas = canvas;
    surface.gc = XCreateGC(display, canvas, 0, NULL);
    if (surface.gc) XSetGraphicsExposures(display, surface.gc, False);
    surface.context = OSMesaCreateContextExt(OSMESA_BGRA, localvar_getInt(args, 3), 0, 0, NULL);
    int ok = surface.gc && surface.context &&
        bind_image(display, localvar_getInt(args, 1), localvar_getInt(args, 2));
    if (!ok) {
        destroy_surface(display);
    }
    push_int(runtime->stack, ok);
    return RUNTIME_STATUS_NORMAL;
}

/* Keep an unclipped server-side copy. Every present repaints the window,
 * including after exposure, but only changed pixels cross the socket. */
static void present_image(Display *display)
{
    XImage *image = surface.image;
    int left = 0, top = 0, right = image->width, bottom = image->height;
    if (!surface.previous || !surface.backing) {
        PROFILE_SCOPE(PROFILE_X11_SEND);
        jvm_profile_x11_bytes((uint64_t)image->width*image->height*4);
        XPutImage(display, surface.canvas, surface.gc, image, 0, 0, 0, 0,
            image->width, image->height);
        XFlush(display);
        return;
    }
    if (surface.previous_valid) {
        PROFILE_SCOPE(PROFILE_X11_SCAN);
        left = image->width; right = 0; top = image->height; bottom = 0;
        for (int y = 0; y < image->height; ++y) {
            const uint32_t *row = (const uint32_t *)(image->data + (size_t)y * image->bytes_per_line);
            const uint32_t *old = (const uint32_t *)(surface.previous + (size_t)y * image->bytes_per_line);
            if (!memcmp(row, old, (size_t)image->width * 4)) continue;
            int x = 0, end = image->width;
            while (x < end && row[x] == old[x]) ++x;
            while (end > x && row[end - 1] == old[end - 1]) --end;
            if (x < left) left = x;
            if (end > right) right = end;
            if (y < top) top = y;
            bottom = y + 1;
        }
    }
    if (right > left && bottom > top) {
        { PROFILE_SCOPE(PROFILE_X11_SEND);
        jvm_profile_x11_bytes((uint64_t)(right-left)*(bottom-top)*4);
        XPutImage(display, surface.backing, surface.gc, image, left, top, left, top,
            right - left, bottom - top);
        }
        PROFILE_SCOPE(PROFILE_X11_SCAN);
        for (int y = top; y < bottom; ++y)
            memcpy(surface.previous + (size_t)y * image->bytes_per_line + left * 4,
                image->data + (size_t)y * image->bytes_per_line + left * 4,
                (size_t)(right - left) * 4);
    }
    surface.previous_valid = 1;
    PROFILE_SCOPE(PROFILE_X11_COPY);
    XCopyArea(display, surface.backing, surface.canvas, surface.gc, 0, 0,
        image->width, image->height, 0, 0);
    XFlush(display);
}

static s32 present(Runtime *runtime, JClass *clazz)
{
    Display *display = awt_x_display();
    int ok = surface.context && canvas_window(runtime) == surface.canvas;
    if (ok) {
        LocalVarItem *args = runtime->localvar;
        int width = localvar_getInt(args, 1);
        int height = localvar_getInt(args, 2);
        { PROFILE_SCOPE(PROFILE_FINISH); glFinish(); }
        { PROFILE_SCOPE(PROFILE_X11);
        present_image(display);
        }
        jvm_profile_frame();
        if (width != surface.image->width || height != surface.image->height) {
            ok = bind_image(display, width, height);
        }
    }
    push_int(runtime->stack, ok);
    return RUNTIME_STATUS_NORMAL;
}

static s32 active(Runtime *runtime, JClass *clazz)
{
    Window focus;
    int revert;
    XGetInputFocus(awt_x_display(), &focus, &revert);
    push_int(runtime->stack, focus == surface.canvas || focus == surface.parent);
    return RUNTIME_STATUS_NORMAL;
}

static s32 desktop_size(Runtime *runtime, JClass *clazz)
{
    Display *display = awt_x_display();
    int temporary = !display;
    if (temporary) {
        display = XOpenDisplay(NULL);
    }
    uint64_t size = 0;
    if (display) {
        int screen = DefaultScreen(display);
        size = ((uint64_t) DisplayWidth(display, screen) << 32) |
            (uint32_t) DisplayHeight(display, screen);
        if (temporary) {
            XCloseDisplay(display);
        }
    }
    push_long(runtime->stack, size);
    return RUNTIME_STATUS_NORMAL;
}

static s32 destroy(Runtime *runtime, JClass *clazz)
{
    destroy_surface(awt_x_display());
    return RUNTIME_STATUS_NORMAL;
}

static GLdouble arg_double(Runtime *runtime, int slot)
{
    Long2Double bits = {.l = localvar_getLong(runtime->localvar, slot)};
    return bits.d;
}

static GLfloat arg_float(Runtime *runtime, int slot)
{
    Int2Float bits = {.i = localvar_getInt(runtime->localvar, slot)};
    return bits.f;
}

#define ARG_INT(slot) localvar_getInt(runtime->localvar, slot)
#define ARG_FLOAT(slot) arg_float(runtime, slot)

#define GL_VOID(name, ...) \
    static s32 call_##name(Runtime *runtime, JClass *clazz) \
    { \
        PROFILE_SCOPE(PROFILE_GL); \
        name(__VA_ARGS__); \
        return RUNTIME_STATUS_NORMAL; \
    }

GL_VOID(glClear, ARG_INT(0))
GL_VOID(glClearColor, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2), ARG_FLOAT(3))
GL_VOID(glViewport, ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3))
GL_VOID(glEnable, ARG_INT(0))
GL_VOID(glDisable, ARG_INT(0))
GL_VOID(glAlphaFunc, ARG_INT(0), ARG_FLOAT(1))
GL_VOID(glBlendFunc, ARG_INT(0), ARG_INT(1))
GL_VOID(glDepthFunc, ARG_INT(0))
GL_VOID(glDepthMask, ARG_INT(0))
GL_VOID(glBegin, ARG_INT(0))
GL_VOID(glEnd)
GL_VOID(glColor3f, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2))
GL_VOID(glColor4f, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2), ARG_FLOAT(3))
GL_VOID(glVertex3f, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2))
GL_VOID(glMatrixMode, ARG_INT(0))
GL_VOID(glLoadIdentity)
GL_VOID(glFlush)

GL_VOID(glBindTexture, ARG_INT(0), ARG_INT(1))
GL_VOID(glCallList, ARG_INT(0))
GL_VOID(glColorMask, ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3))
GL_VOID(glColorMaterial, ARG_INT(0), ARG_INT(1))
GL_VOID(glCopyTexSubImage2D, ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4), ARG_INT(5), ARG_INT(6), ARG_INT(7))
GL_VOID(glCullFace, ARG_INT(0))
GL_VOID(glDeleteLists, ARG_INT(0), ARG_INT(1))
GL_VOID(glDisableClientState, ARG_INT(0))

GL_VOID(glEnableClientState, ARG_INT(0))
GL_VOID(glEndList)
GL_VOID(glFogf, ARG_INT(0), ARG_FLOAT(1))
GL_VOID(glFogi, ARG_INT(0), ARG_INT(1))
GL_VOID(glLineWidth, ARG_FLOAT(0))
GL_VOID(glNewList, ARG_INT(0), ARG_INT(1))
GL_VOID(glNormal3f, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2))
GL_VOID(glOrtho, arg_double(runtime, 0), arg_double(runtime, 2), arg_double(runtime, 4), arg_double(runtime, 6), arg_double(runtime, 8), arg_double(runtime, 10))
GL_VOID(glPixelStorei, ARG_INT(0), ARG_INT(1))
GL_VOID(glPolygonOffset, ARG_FLOAT(0), ARG_FLOAT(1))
GL_VOID(glPopMatrix)
GL_VOID(glPushMatrix)
GL_VOID(glRotatef, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2), ARG_FLOAT(3))
GL_VOID(glScaled, arg_double(runtime, 0), arg_double(runtime, 2), arg_double(runtime, 4))
GL_VOID(glScalef, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2))
GL_VOID(glShadeModel, ARG_INT(0))
GL_VOID(glTexGeni, ARG_INT(0), ARG_INT(1), ARG_INT(2))
GL_VOID(glTexParameteri, ARG_INT(0), ARG_INT(1), ARG_INT(2))
GL_VOID(glTranslatef, ARG_FLOAT(0), ARG_FLOAT(1), ARG_FLOAT(2))
GL_VOID(glClearDepth, arg_double(runtime, 0))
GL_VOID(glTexCoord2f, ARG_FLOAT(0), ARG_FLOAT(1))
GL_VOID(glActiveTexture, ARG_INT(0))
GL_VOID(glClientActiveTexture, ARG_INT(0))
GL_VOID(glMultiTexCoord2f, ARG_INT(0), ARG_FLOAT(1), ARG_FLOAT(2))
static s32 call_glGenLists(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    push_int(runtime->stack, glGenLists(ARG_INT(0)));
    return RUNTIME_STATUS_NORMAL;
}

static s32 call_glGetError(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    push_int(runtime->stack, glGetError());
    return RUNTIME_STATUS_NORMAL;
}

static s32 call_glGetString(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    const GLubyte *value = glGetString(ARG_INT(0));
    push_ref(runtime->stack, value ? jstring_create_cstr((const char *) value, runtime) : NULL);
    return RUNTIME_STATUS_NORMAL;
}

static s32 bad_buffer(Runtime *runtime)
{
    push_ref(runtime->stack, exception_create_str(JVM_EXCEPTION_ILLEGALARGUMENT, runtime,
        "Unsupported OpenGL state or insufficient pixel data"));
    return RUNTIME_STATUS_EXCEPTION;
}

static s32 mult_matrix(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 0);
    glMultMatrixf((float *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static s32 fog_vector(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 1);
    glFogfv(ARG_INT(0), (float *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static s32 light_vector(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 2);
    glLightfv(ARG_INT(0), ARG_INT(1), (float *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static s32 light_model_vector(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 1);
    glLightModelfv(ARG_INT(0), (float *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static s32 texgen_vector(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 2);
    glTexGenfv(ARG_INT(0), ARG_INT(1), (float *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static int query_count(GLenum parameter)
{
    switch (parameter) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
        return 16;
    case GL_VIEWPORT:
    case GL_CURRENT_COLOR:
    case GL_COLOR_CLEAR_VALUE:
    case GL_FOG_COLOR:
        return 4;
    case GL_DEPTH_RANGE:
        return 2;
    case GL_MAX_TEXTURE_SIZE:
    case GL_MATRIX_MODE:
    case GL_TEXTURE_BINDING_2D:
    case GL_UNPACK_ALIGNMENT:
    case GL_PACK_ALIGNMENT:
    case GL_MAX_TEXTURE_UNITS:
        return 1;
    default:
        return 0;
    }
}

static s32 get_float(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int count = query_count(ARG_INT(0));
    if (!count) return bad_buffer(runtime);
    Instance *values = jarray_create_by_type_index(runtime, count, DATATYPE_FLOAT);
    glGetFloatv(ARG_INT(0), (float *) values->arr_body);
    push_ref(runtime->stack, values);
    return RUNTIME_STATUS_NORMAL;
}

static s32 get_integer(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int count = query_count(ARG_INT(0));
    if (!count) return bad_buffer(runtime);
    Instance *values = jarray_create_by_type_index(runtime, count, DATATYPE_INT);
    glGetIntegerv(ARG_INT(0), (int *) values->arr_body);
    push_ref(runtime->stack, values);
    return RUNTIME_STATUS_NORMAL;
}

static s32 gen_textures(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int count = ARG_INT(0);
    Instance *values = jarray_create_by_type_index(runtime, count, DATATYPE_INT);
    glGenTextures(count, (GLuint *) values->arr_body);
    push_ref(runtime->stack, values);
    return RUNTIME_STATUS_NORMAL;
}

static s32 delete_textures(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 0);
    glDeleteTextures(values->arr_length, (GLuint *) values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static s32 call_lists(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *values = localvar_getRefer(runtime->localvar, 0);
    glCallLists(values->arr_length, GL_UNSIGNED_INT, values->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

static int pixel_bytes(int width, int height, int format, int type, int pack)
{
    int channels;
    switch (format) {
    case GL_RGBA:
    case GL_BGRA:
        channels = 4;
        break;
    case GL_RGB:
    case GL_BGR:
        channels = 3;
        break;
    default:
        return -1;
    }
    if (type != GL_UNSIGNED_BYTE &&
        !(channels == 4 && (type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_INT_8_8_8_8))) {
        return -1;
    }
    if (width < 0 || height < 0) return -1;
    if (!width || !height) return 0;
    GLint alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
    glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, &alignment);
    glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, &row_length);
    glGetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, &skip_rows);
    glGetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    if (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) return -1;
    int64_t row_bytes = (int64_t) (row_length ? row_length : width) * channels;
    int64_t stride = (row_bytes + alignment - 1) & -(int64_t) alignment;
    int64_t length = ((int64_t) skip_rows + height - 1) * stride +
        ((int64_t) skip_pixels + width) * channels;
    return length > INT32_MAX || length < 0 ? -1 : (int) length;
}

static s32 get_pixel_bytes(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int bytes = pixel_bytes(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4));
    if (bytes < 0) return bad_buffer(runtime);
    push_int(runtime->stack, bytes);
    return RUNTIME_STATUS_NORMAL;
}

static void *pixel_pointer(Instance *array, int offset)
{
    return array ? array->arr_body + offset : NULL;
}

static int valid_pixels(Instance *array, int offset, int bytes)
{
    return array && offset >= 0 && bytes >= 0 && offset <= array->arr_length - bytes;
}

static s32 tex_image(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *pixels = localvar_getRefer(runtime->localvar, 8);
    int offset = ARG_INT(9);
    int bytes = pixel_bytes(ARG_INT(3), ARG_INT(4), ARG_INT(6), ARG_INT(7), 0);
    if (pixels && !valid_pixels(pixels, offset, bytes)) return bad_buffer(runtime);
    glTexImage2D(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4),
        ARG_INT(5), ARG_INT(6), ARG_INT(7), pixel_pointer(pixels, offset));
    return RUNTIME_STATUS_NORMAL;
}

static s32 tex_sub_image(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *pixels = localvar_getRefer(runtime->localvar, 8);
    int offset = ARG_INT(9);
    int bytes = pixel_bytes(ARG_INT(4), ARG_INT(5), ARG_INT(6), ARG_INT(7), 0);
    if (!valid_pixels(pixels, offset, bytes)) return bad_buffer(runtime);
    glTexSubImage2D(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4),
        ARG_INT(5), ARG_INT(6), ARG_INT(7), pixel_pointer(pixels, offset));
    return RUNTIME_STATUS_NORMAL;
}

static s32 read_pixels(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    Instance *pixels = localvar_getRefer(runtime->localvar, 6);
    int offset = ARG_INT(7);
    int bytes = pixel_bytes(ARG_INT(2), ARG_INT(3), ARG_INT(4), ARG_INT(5), 1);
    if (!valid_pixels(pixels, offset, bytes)) return bad_buffer(runtime);
    glReadPixels(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4),
        ARG_INT(5), pixel_pointer(pixels, offset));
    return RUNTIME_STATUS_NORMAL;
}


static s32 bind_pointer(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int kind = ARG_INT(0), size = ARG_INT(1), type = ARG_INT(2), stride = ARG_INT(3);
    Instance *buffer = localvar_getRefer(runtime->localvar, 4);
    int offset = ARG_INT(5), length = ARG_INT(6);
    int unit = 0;
    int bytes = type == GL_FLOAT ? 4 : type == GL_SHORT ? 2 : 1;
    if (kind < 0 || kind > 3 || offset < 0 || length < 0 || stride < 0 ||
        size < 1 || size > 4 ||
        (kind == 0 && size < 2) || (kind == 2 && size < 3) || (kind == 3 && size != 3) ||
        (type != GL_FLOAT && type != GL_SHORT && type != GL_BYTE && type != GL_UNSIGNED_BYTE)) {
        return bad_buffer(runtime);
    }
    if (kind == 1) {
        glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &unit);
        unit -= GL_TEXTURE0;
        if (unit < 0 || unit >= 32) return bad_buffer(runtime);
    }
    FieldInfo *field = find_fieldInfo_by_name_c("java/nio/Buffer", "address", "J", NULL, runtime);
    if (!field || !buffer) return bad_buffer(runtime);
    uintptr_t address = getFieldLong(getInstanceFieldPtr(buffer, field));
    if (!address) return bad_buffer(runtime);
    void *data = (void *) (address + offset);
    int index = kind == 0 ? 0 : kind == 1 ? 3 + unit : kind == 2 ? 1 : 2;
    switch (kind) {
    case 0: glVertexPointer(size, type, stride, data); break;
    case 1: glTexCoordPointer(size, type, stride, data); break;
    case 2: glColorPointer(size, type, stride, data); break;
    case 3: glNormalPointer(type, stride, data); break;
    }
    client_arrays[index] = (struct client_array) {length, size, bytes, stride};
    push_int(runtime->stack, unit);
    return RUNTIME_STATUS_NORMAL;
}

static int array_fits(int index, int first, int count)
{
    struct client_array *array = &client_arrays[index];
    int element = array->components * array->element_bytes;
    int stride = array->stride ? array->stride : element;
    int64_t required = ((int64_t) first + count - 1) * stride + element;
    return element > 0 && required <= array->length;
}

static s32 draw_arrays(Runtime *runtime, JClass *clazz)
{
    PROFILE_SCOPE(PROFILE_GL);
    int first = ARG_INT(1), count = ARG_INT(2);
    if (first >= 0 && count > 0) {
        if ((glIsEnabled(GL_VERTEX_ARRAY) && !array_fits(0, first, count)) ||
            (glIsEnabled(GL_COLOR_ARRAY) && !array_fits(1, first, count)) ||
            (glIsEnabled(GL_NORMAL_ARRAY) && !array_fits(2, first, count))) {
            return bad_buffer(runtime);
        }
        GLint active, units;
        glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        for (int unit = 0; unit < units && unit < 32; unit++) {
            glClientActiveTexture(GL_TEXTURE0 + unit);
            if (glIsEnabled(GL_TEXTURE_COORD_ARRAY) && !array_fits(3 + unit, first, count)) {
                glClientActiveTexture(active);
                return bad_buffer(runtime);
            }
        }
        glClientActiveTexture(active);
    }
    glDrawArrays(ARG_INT(0), first, count);
    return RUNTIME_STATUS_NORMAL;
}

#define GL_METHOD(name, sig) {"org/lwjgl/opengl/GL11", #name, sig, call_##name}
#define DISPLAY_METHOD(name, sig, fn) {"org/lwjgl/opengl/Display", name, sig, fn}

void reg_lwjgl_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        {"org/lwjgl/opengl/GL11", "nMultMatrix", "([F)V", mult_matrix},
        {"org/lwjgl/opengl/GL11", "nFog", "(I[F)V", fog_vector},
        {"org/lwjgl/opengl/GL11", "nLight", "(II[F)V", light_vector},
        {"org/lwjgl/opengl/GL11", "nLightModel", "(I[F)V", light_model_vector},
        {"org/lwjgl/opengl/GL11", "nTexGen", "(II[F)V", texgen_vector},
        {"org/lwjgl/opengl/GL11", "nGetFloat", "(I)[F", get_float},
        {"org/lwjgl/opengl/GL11", "nGetInteger", "(I)[I", get_integer},
        {"org/lwjgl/opengl/GL11", "nGenTextures", "(I)[I", gen_textures},
        {"org/lwjgl/opengl/GL11", "nDeleteTextures", "([I)V", delete_textures},
        {"org/lwjgl/opengl/GL11", "nCallLists", "([I)V", call_lists},
        {"org/lwjgl/opengl/GL11", "nPixelBytes", "(IIIIZ)I", get_pixel_bytes},
        {"org/lwjgl/opengl/GL11", "nTexImage2D", "(IIIIIIII[BI)V", tex_image},
        {"org/lwjgl/opengl/GL11", "nTexSubImage2D", "(IIIIIIII[BI)V", tex_sub_image},
        {"org/lwjgl/opengl/GL11", "nReadPixels", "(IIIIII[BI)V", read_pixels},
        {"org/lwjgl/opengl/GL11", "nPointer", "(IIIILjava/nio/Buffer;II)I", bind_pointer},
        DISPLAY_METHOD("nCreate", "(Ljava/awt/Canvas;III)Z", create),
        DISPLAY_METHOD("nPresent", "(Ljava/awt/Canvas;II)Z", present),
        DISPLAY_METHOD("nActive", "()Z", active),
        DISPLAY_METHOD("nDesktopSize", "()J", desktop_size),
        DISPLAY_METHOD("nDestroy", "()V", destroy),
        {"org/lwjgl/opengl/GL11", "glBindTexture", "(II)V", call_glBindTexture},
        {"org/lwjgl/opengl/GL11", "glCallList", "(I)V", call_glCallList},
        {"org/lwjgl/opengl/GL11", "glColorMask", "(ZZZZ)V", call_glColorMask},
        {"org/lwjgl/opengl/GL11", "glColorMaterial", "(II)V", call_glColorMaterial},
        {"org/lwjgl/opengl/GL11", "glCopyTexSubImage2D", "(IIIIIIII)V", call_glCopyTexSubImage2D},
        {"org/lwjgl/opengl/GL11", "glCullFace", "(I)V", call_glCullFace},
        {"org/lwjgl/opengl/GL11", "glDeleteLists", "(II)V", call_glDeleteLists},
        {"org/lwjgl/opengl/GL11", "glDisableClientState", "(I)V", call_glDisableClientState},
        {"org/lwjgl/opengl/GL11", "glDrawArrays", "(III)V", draw_arrays},
        {"org/lwjgl/opengl/GL11", "glEnableClientState", "(I)V", call_glEnableClientState},
        {"org/lwjgl/opengl/GL11", "glEndList", "()V", call_glEndList},
        {"org/lwjgl/opengl/GL11", "glFogf", "(IF)V", call_glFogf},
        {"org/lwjgl/opengl/GL11", "glFogi", "(II)V", call_glFogi},
        {"org/lwjgl/opengl/GL11", "glLineWidth", "(F)V", call_glLineWidth},
        {"org/lwjgl/opengl/GL11", "glNewList", "(II)V", call_glNewList},
        {"org/lwjgl/opengl/GL11", "glNormal3f", "(FFF)V", call_glNormal3f},
        {"org/lwjgl/opengl/GL11", "glOrtho", "(DDDDDD)V", call_glOrtho},
        {"org/lwjgl/opengl/GL11", "glPixelStorei", "(II)V", call_glPixelStorei},
        {"org/lwjgl/opengl/GL11", "glPolygonOffset", "(FF)V", call_glPolygonOffset},
        {"org/lwjgl/opengl/GL11", "glPopMatrix", "()V", call_glPopMatrix},
        {"org/lwjgl/opengl/GL11", "glPushMatrix", "()V", call_glPushMatrix},
        {"org/lwjgl/opengl/GL11", "glRotatef", "(FFFF)V", call_glRotatef},
        {"org/lwjgl/opengl/GL11", "glScaled", "(DDD)V", call_glScaled},
        {"org/lwjgl/opengl/GL11", "glScalef", "(FFF)V", call_glScalef},
        {"org/lwjgl/opengl/GL11", "glShadeModel", "(I)V", call_glShadeModel},
        {"org/lwjgl/opengl/GL11", "glTexGeni", "(III)V", call_glTexGeni},
        {"org/lwjgl/opengl/GL11", "glTexParameteri", "(III)V", call_glTexParameteri},
        {"org/lwjgl/opengl/GL11", "glTranslatef", "(FFF)V", call_glTranslatef},
        {"org/lwjgl/opengl/GL11", "glClearDepth", "(D)V", call_glClearDepth},
        {"org/lwjgl/opengl/GL11", "glTexCoord2f", "(FF)V", call_glTexCoord2f},
        {"org/lwjgl/opengl/GL13", "glActiveTexture", "(I)V", call_glActiveTexture},
        {"org/lwjgl/opengl/GL13", "glClientActiveTexture", "(I)V", call_glClientActiveTexture},
        {"org/lwjgl/opengl/GL13", "glMultiTexCoord2f", "(IFF)V", call_glMultiTexCoord2f},
        GL_METHOD(glGenLists, "(I)I"),
        GL_METHOD(glClear, "(I)V"),
        GL_METHOD(glClearColor, "(FFFF)V"),
        GL_METHOD(glViewport, "(IIII)V"),
        GL_METHOD(glEnable, "(I)V"),
        GL_METHOD(glDisable, "(I)V"),
        GL_METHOD(glAlphaFunc, "(IF)V"),
        GL_METHOD(glBlendFunc, "(II)V"),
        GL_METHOD(glDepthFunc, "(I)V"),
        GL_METHOD(glDepthMask, "(Z)V"),
        GL_METHOD(glBegin, "(I)V"),
        GL_METHOD(glEnd, "()V"),
        GL_METHOD(glColor3f, "(FFF)V"),
        GL_METHOD(glColor4f, "(FFFF)V"),
        GL_METHOD(glVertex3f, "(FFF)V"),
        GL_METHOD(glMatrixMode, "(I)V"),
        GL_METHOD(glLoadIdentity, "()V"),
        GL_METHOD(glFlush, "()V"),
        GL_METHOD(glGetError, "()I"),
        GL_METHOD(glGetString, "(I)Ljava/lang/String;"),
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
