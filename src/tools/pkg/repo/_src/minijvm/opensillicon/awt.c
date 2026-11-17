/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "jvm_util.h"

static Display *display;
static Atom wm_protocols;
static Atom wm_delete;

Display *awt_x_display(void)
{
    return display;
}

static void store_window_name(Window window, Instance *title, Runtime *runtime)
{
    Utf8String *name = utf8_create();
    jstring_2_utf8(title, name, runtime);
    XStoreName(display, window, utf8_cstr(name));
    utf8_destory(name);
}

static s32 create_window(Runtime *runtime, JClass *clazz)
{
    if (!display) {
        display = XOpenDisplay(NULL);
        if (!display) {
            push_long(runtime->stack, 0);
            return RUNTIME_STATUS_NORMAL;
        }
        wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
        wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    }

    LocalVarItem *args = runtime->localvar;
    Instance *title = localvar_getRefer(args, 0);
    int width = localvar_getInt(args, 1);
    int height = localvar_getInt(args, 2);
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
        0, 0, width, height, 0, 0, BlackPixel(display, screen));

    store_window_name(window, title, runtime);
    XSelectInput(display, window, StructureNotifyMask);
    XSetWMProtocols(display, window, &wm_delete, 1);
    push_long(runtime->stack, window);
    return RUNTIME_STATUS_NORMAL;
}

static s32 set_title(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    store_window_name(localvar_getLong(args, 0), localvar_getRefer(args, 2), runtime);
    XFlush(display);
    return RUNTIME_STATUS_NORMAL;
}

static s32 create_canvas(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    Window parent = localvar_getLong(args, 0);
    int width = localvar_getInt(args, 2);
    int height = localvar_getInt(args, 3);
    Window window = XCreateSimpleWindow(display, parent, 0, 0, width, height, 0, 0,
        BlackPixel(display, DefaultScreen(display)));
    XMapWindow(display, window);
    XFlush(display);
    push_long(runtime->stack, window);
    return RUNTIME_STATUS_NORMAL;
}

static s32 center(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    Window window = localvar_getLong(args, 0);
    int screen = DefaultScreen(display);
    int x = (DisplayWidth(display, screen) - localvar_getInt(args, 2)) / 2;
    int y = (DisplayHeight(display, screen) - localvar_getInt(args, 3)) / 2;
    XSizeHints hints = {.flags = USPosition, .x = x < 0 ? 0 : x, .y = y < 0 ? 0 : y};
    XSetWMNormalHints(display, window, &hints);
    XMoveWindow(display, window, hints.x, hints.y);
    XFlush(display);
    return RUNTIME_STATUS_NORMAL;
}

static s32 resize(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    XResizeWindow(display, localvar_getLong(args, 0),
        localvar_getInt(args, 2), localvar_getInt(args, 3));
    XFlush(display);
    return RUNTIME_STATUS_NORMAL;
}

static s32 set_mapped(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    Window window = localvar_getLong(args, 0);
    if (localvar_getInt(args, 2)) {
        XMapWindow(display, window);
    } else {
        XUnmapWindow(display, window);
    }
    XFlush(display);
    return RUNTIME_STATUS_NORMAL;
}

static s32 destroy(Runtime *runtime, JClass *clazz)
{
    XDestroyWindow(display, localvar_getLong(runtime->localvar, 0));
    XFlush(display);
    return RUNTIME_STATUS_NORMAL;
}

static s32 fill_rect(Runtime *runtime, JClass *clazz)
{
    LocalVarItem *args = runtime->localvar;
    Window window = localvar_getLong(args, 0);
    unsigned rgb = localvar_getInt(args, 2);
    int screen = DefaultScreen(display);
    XColor color = {
        .red = ((rgb >> 16) & 255) * 257,
        .green = ((rgb >> 8) & 255) * 257,
        .blue = (rgb & 255) * 257
    };
    Colormap colormap = DefaultColormap(display, screen);
    if (XAllocColor(display, colormap, &color)) {
        XGCValues values = {.foreground = color.pixel};
        GC gc = XCreateGC(display, window, GCForeground, &values);
        XFillRectangle(display, window, gc, localvar_getInt(args, 3),
            localvar_getInt(args, 4), localvar_getInt(args, 5), localvar_getInt(args, 6));
        XFreeGC(display, gc);
        XFreeColors(display, colormap, &color.pixel, 1, 0);
        XFlush(display);
    }
    return RUNTIME_STATUS_NORMAL;
}

static s32 render_text(Runtime *runtime, JClass *clazz)
{
    Instance *text = localvar_getRefer(runtime->localvar, 0);
    Instance *mask = NULL;
    if (display) {
        XFontStruct *font = XLoadQueryFont(display, "fixed");
        if (font) {
            int width = XTextWidth(font, text->arr_body, text->arr_length);
            int height = font->ascent + font->descent;
            if (width > 0 && width <= 4096 && height > 0 && height <= 256) {
                Pixmap pixmap = XCreatePixmap(display, DefaultRootWindow(display), width, height, 1);
                XGCValues values = {.foreground = 0, .background = 0, .font = font->fid};
                GC gc = XCreateGC(display, pixmap, GCForeground | GCBackground | GCFont, &values);
                XFillRectangle(display, pixmap, gc, 0, 0, width, height);
                XSetForeground(display, gc, 1);
                XDrawString(display, pixmap, gc, 0, font->ascent, text->arr_body, text->arr_length);
                XImage *image = XGetImage(display, pixmap, 0, 0, width, height, 1, ZPixmap);
                if (image) {
                    mask = jarray_create_by_type_index(runtime, 3 + width * height, DATATYPE_INT);
                    int *pixels = (int *) mask->arr_body;
                    pixels[0] = width;
                    pixels[1] = height;
                    pixels[2] = font->ascent;
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++) {
                            pixels[3 + y * width + x] = XGetPixel(image, x, y);
                        }
                    }
                    XDestroyImage(image);
                }
                XFreeGC(display, gc);
                XFreePixmap(display, pixmap);
            }
            XFreeFont(display, font);
        }
    }
    push_ref(runtime->stack, mask);
    return RUNTIME_STATUS_NORMAL;
}

static s32 poll_window(Runtime *runtime, JClass *clazz)
{
    Window window = localvar_getLong(runtime->localvar, 0);
    XEvent event;
    s64 result = 0;
    if (XCheckTypedWindowEvent(display, window, ClientMessage, &event) &&
        event.xclient.message_type == wm_protocols && event.xclient.format == 32 &&
        (Atom) event.xclient.data.l[0] == wm_delete) {
        result = -1;
    } else if (XCheckTypedWindowEvent(display, window, ConfigureNotify, &event)) {
        result = ((uint64_t) event.xconfigure.width << 32) | (uint32_t) event.xconfigure.height;
    }
    push_long(runtime->stack, result);
    return RUNTIME_STATUS_NORMAL;
}

void reg_awt_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        {"java/awt/X11Peer", "createWindow", "(Ljava/lang/String;II)J", create_window},
        {"java/awt/X11Peer", "setTitle", "(JLjava/lang/String;)V", set_title},
        {"java/awt/X11Peer", "createCanvas", "(JII)J", create_canvas},
        {"java/awt/X11Peer", "center", "(JII)V", center},
        {"java/awt/X11Peer", "resize", "(JII)V", resize},
        {"java/awt/X11Peer", "setMapped", "(JZ)V", set_mapped},
        {"java/awt/X11Peer", "destroy", "(J)V", destroy},
        {"java/awt/X11Peer", "fillRect", "(JIIIII)V", fill_rect},
        {"java/awt/X11Peer", "renderText", "([B)[I", render_text},
        {"java/awt/X11Peer", "poll", "(J)J", poll_window}
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
