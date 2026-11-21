/* SPDX-License-Identifier: MIT */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <string.h>
#include <stdlib.h>
#include "jvm_util.h"
#include "profile.h"

Display *awt_x_display(void);
Window lwjgl_canvas_window(void);
Window lwjgl_parent_window(void);
void lwjgl_canvas_size(int *width, int *height);

enum { QUEUE_CAPACITY = 256 };
struct event_queue {
    int values[QUEUE_CAPACITY][5];
    unsigned read, write, count;
};

static struct {
    int keyboard, mouse, repeat, grabbed;
    int trace;
    int keys[256], buttons[3];
    int x, y, dx, dy;
    Cursor invisible;
    struct event_queue keyboard_events, mouse_events;
} input;

static void queue_event(struct event_queue *queue, int a, int b, int c, int d, int e)
{
    if (queue->count == QUEUE_CAPACITY) {
        queue->read = (queue->read + 1) % QUEUE_CAPACITY;
        queue->count--;
    }
    int *values = queue->values[queue->write];
    values[0] = a; values[1] = b; values[2] = c; values[3] = d; values[4] = e;
    queue->write = (queue->write + 1) % QUEUE_CAPACITY;
    queue->count++;
}

static s32 next_event(Runtime *runtime, struct event_queue *queue, int length)
{
    Instance *event = NULL;
    if (queue->count) {
        if (input.trace && queue == &input.mouse_events && queue->values[queue->read][0] >= 0)
            jvm_printf("INPUT delivered button %d state %d at %d %d\n", queue->values[queue->read][0],
                queue->values[queue->read][1], queue->values[queue->read][2], queue->values[queue->read][3]);
        event = jarray_create_by_type_index(runtime, length, DATATYPE_INT);
        memcpy(event->arr_body, queue->values[queue->read], length * sizeof(int));
        queue->read = (queue->read + 1) % QUEUE_CAPACITY;
        queue->count--;
    }
    push_ref(runtime->stack, event);
    return RUNTIME_STATUS_NORMAL;
}

static int key_code(KeySym symbol)
{
    static const struct { KeySym symbol; int code; } keys[] = {
        {XK_Escape,1}, {XK_1,2}, {XK_2,3}, {XK_3,4}, {XK_4,5}, {XK_5,6},
        {XK_6,7}, {XK_7,8}, {XK_8,9}, {XK_9,10}, {XK_0,11}, {XK_minus,12},
        {XK_equal,13}, {XK_BackSpace,14}, {XK_Tab,15}, {XK_q,16}, {XK_w,17},
        {XK_e,18}, {XK_r,19}, {XK_t,20}, {XK_y,21}, {XK_u,22}, {XK_i,23},
        {XK_o,24}, {XK_p,25}, {XK_bracketleft,26}, {XK_bracketright,27},
        {XK_Return,28}, {XK_Control_L,29}, {XK_a,30}, {XK_s,31}, {XK_d,32},
        {XK_f,33}, {XK_g,34}, {XK_h,35}, {XK_j,36}, {XK_k,37}, {XK_l,38},
        {XK_semicolon,39}, {XK_apostrophe,40}, {XK_grave,41}, {XK_Shift_L,42},
        {XK_backslash,43}, {XK_z,44}, {XK_x,45}, {XK_c,46}, {XK_v,47},
        {XK_b,48}, {XK_n,49}, {XK_m,50}, {XK_comma,51}, {XK_period,52},
        {XK_slash,53}, {XK_Shift_R,54}, {XK_KP_Multiply,55}, {XK_Alt_L,56},
        {XK_space,57}, {XK_Caps_Lock,58}, {XK_F1,59}, {XK_F2,60}, {XK_F3,61},
        {XK_F4,62}, {XK_F5,63}, {XK_F6,64}, {XK_F7,65}, {XK_F8,66},
        {XK_F9,67}, {XK_F10,68}, {XK_Num_Lock,69}, {XK_Scroll_Lock,70},
        {XK_KP_Home,71}, {XK_KP_Up,72}, {XK_KP_Prior,73}, {XK_KP_Subtract,74},
        {XK_KP_Left,75}, {XK_KP_Begin,76}, {XK_KP_Right,77}, {XK_KP_Add,78},
        {XK_KP_End,79}, {XK_KP_Down,80}, {XK_KP_Next,81}, {XK_KP_Insert,82},
        {XK_KP_Delete,83}, {XK_F11,87}, {XK_F12,88}, {XK_KP_Enter,156},
        {XK_Control_R,157}, {XK_KP_Divide,181}, {XK_Alt_R,184}, {XK_Home,199},
        {XK_Up,200}, {XK_Prior,201}, {XK_Left,203}, {XK_Right,205},
        {XK_End,207}, {XK_Down,208}, {XK_Next,209}, {XK_Insert,210},
        {XK_Delete,211}, {XK_Super_L,219}, {XK_Super_R,220},
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].symbol == symbol) return keys[i].code;
    }
    return 0;
}

static void release_keys(void)
{
    for (int code = 1; code < 256; code++) {
        if (input.keys[code]) {
            input.keys[code] = 0;
            queue_event(&input.keyboard_events, code, 0, 0, 0, 0);
        }
    }
    for (int button = 0; button < 3; button++) {
        if (input.buttons[button]) {
            input.buttons[button] = 0;
            queue_event(&input.mouse_events, button, 0, input.x, input.y, 0);
        }
    }
}

static void pump_events(void)
{
    PROFILE_SCOPE(PROFILE_INPUT);
    Display *display = awt_x_display();
    Window canvas = lwjgl_canvas_window(), parent = lwjgl_parent_window();
    if (!display || !canvas) return;
    long mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask;
    XEvent event;
    for (int processed = 0; processed < QUEUE_CAPACITY &&
        (XCheckWindowEvent(display, canvas, mask, &event) ||
         XCheckWindowEvent(display, parent, mask, &event)); processed++) {
        if (event.type == KeyPress || event.type == KeyRelease) {
            if (!input.keyboard) continue;
            if (event.type == KeyRelease && XEventsQueued(display, QueuedAfterReading)) {
                XEvent next;
                XPeekEvent(display, &next);
                if (next.type == KeyPress && next.xkey.window == event.xkey.window &&
                    next.xkey.keycode == event.xkey.keycode && next.xkey.time == event.xkey.time) {
                    XNextEvent(display, &event);
                    if (!input.repeat) continue;
                }
            }
            int code = key_code(XLookupKeysym(&event.xkey, 0));
            int pressed = event.type == KeyPress;
            char character[8];
            KeySym symbol;
            int length = XLookupString(&event.xkey, character, sizeof(character), &symbol, NULL);
            if (!code) continue;
            if (pressed && input.keys[code] && !input.repeat) continue;
            input.keys[code] = pressed;
            queue_event(&input.keyboard_events, code,
                pressed && length == 1 ? (unsigned char) character[0] : 0, pressed, 0, 0);
        } else if (event.type == FocusOut) {
            /* The window manager focuses the frame, then a click focuses its
             * Canvas. That transfer is not a loss of focus for the game. A
             * queued FocusOut may also be older than a subsequent FocusIn. */
            Window focus;
            int revert;
            XGetInputFocus(display, &focus, &revert);
            if (focus != canvas && focus != parent) release_keys();
        } else if (event.type == MotionNotify && input.mouse) {
            int width, height;
            lwjgl_canvas_size(&width, &height);
            int x = event.xmotion.x, y = height - 1 - event.xmotion.y;
            if (input.grabbed) {
                input.dx += x - width / 2;
                input.dy += height / 2 - event.xmotion.y;
                if (x != width / 2 || event.xmotion.y != height / 2) {
                    XWarpPointer(display, None, canvas, 0, 0, 0, 0, width / 2, height / 2);
                }
            } else {
                input.dx += x - input.x;
                input.dy += y - input.y;
            }
            input.x = x;
            input.y = y;
            queue_event(&input.mouse_events, -1, 0, x, y, 0);
        } else if ((event.type == ButtonPress || event.type == ButtonRelease) && input.mouse) {
            int width, height;
            lwjgl_canvas_size(&width, &height);
            input.x = event.xbutton.x;
            input.y = height - 1 - event.xbutton.y;
            unsigned button = event.xbutton.button;
            int pressed = event.type == ButtonPress;
            if (input.trace) jvm_printf("INPUT received button %u state %d at %d %d size %d %d\n",
                button, pressed, input.x, input.y, width, height);
            if (pressed) XSetInputFocus(display, canvas, RevertToParent, CurrentTime);
            if ((button == Button4 || button == Button5) && pressed) {
                queue_event(&input.mouse_events, -1, 0, input.x, input.y,
                    button == Button4 ? 120 : -120);
            } else if (button >= Button1 && button <= Button3) {
                int index = button == Button1 ? 0 : button == Button3 ? 1 : 2;
                input.buttons[index] = pressed;
                queue_event(&input.mouse_events, index, pressed, input.x, input.y, 0);
            }
        }
    }
    XFlush(display);
}

static int select_input(void)
{
    Display *display = awt_x_display();
    Window windows[] = {lwjgl_canvas_window(), lwjgl_parent_window()};
    if (!display || !windows[0]) return 0;
    long mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask;
    for (unsigned i = 0; i < 2; i++) {
        XWindowAttributes attributes;
        if (XGetWindowAttributes(display, windows[i], &attributes)) {
            XSelectInput(display, windows[i], attributes.your_event_mask | mask);
        }
    }
    XFlush(display);
    return 1;
}

static s32 keyboard_create(Runtime *runtime, JClass *clazz)
{
    input.keyboard = select_input();
    push_int(runtime->stack, input.keyboard);
    return RUNTIME_STATUS_NORMAL;
}

static s32 keyboard_destroy(Runtime *runtime, JClass *clazz)
{
    input.keyboard = 0;
    memset(input.keys, 0, sizeof(input.keys));
    memset(&input.keyboard_events, 0, sizeof(input.keyboard_events));
    return RUNTIME_STATUS_NORMAL;
}

static s32 keyboard_next(Runtime *runtime, JClass *clazz)
{
    pump_events();
    return next_event(runtime, &input.keyboard_events, 3);
}

static s32 keyboard_state(Runtime *runtime, JClass *clazz)
{
    pump_events();
    push_int(runtime->stack, input.keys[localvar_getInt(runtime->localvar, 0)]);
    return RUNTIME_STATUS_NORMAL;
}

static s32 keyboard_repeat(Runtime *runtime, JClass *clazz)
{
    input.repeat = localvar_getInt(runtime->localvar, 0);
    return RUNTIME_STATUS_NORMAL;
}

static s32 mouse_create(Runtime *runtime, JClass *clazz)
{
    Utf8String *trace_key = utf8_create_c("minijvm.input.trace");
    Utf8String *trace = hashtable_get(runtime->jvm->sys_prop, trace_key);
    input.trace = trace && utf8_equals_c(trace, "true");
    utf8_destory(trace_key);
    input.mouse = select_input();
    if (input.mouse) {
        Window root, child;
        int root_x, root_y, x, y, width, height;
        unsigned buttons;
        if (XQueryPointer(awt_x_display(), lwjgl_canvas_window(), &root, &child,
            &root_x, &root_y, &x, &y, &buttons)) {
            lwjgl_canvas_size(&width, &height);
            input.x = x;
            input.y = height - 1 - y;
        }
    }
    push_int(runtime->stack, input.mouse);
    return RUNTIME_STATUS_NORMAL;
}

static void release_pointer(void)
{
    Display *display = awt_x_display();
    if (display && input.grabbed) {
        XUngrabPointer(display, CurrentTime);
        XUndefineCursor(display, lwjgl_canvas_window());
    }
    input.grabbed = 0;
}

static s32 mouse_destroy(Runtime *runtime, JClass *clazz)
{
    release_pointer();
    input.mouse = 0;
    memset(input.buttons, 0, sizeof(input.buttons));
    memset(&input.mouse_events, 0, sizeof(input.mouse_events));
    return RUNTIME_STATUS_NORMAL;
}

static s32 mouse_next(Runtime *runtime, JClass *clazz)
{
    pump_events();
    return next_event(runtime, &input.mouse_events, 5);
}

static s32 mouse_value(Runtime *runtime, JClass *clazz)
{
    pump_events();
    int value = localvar_getInt(runtime->localvar, 0);
    int result = 0;
    switch (value) {
    case 0: result = input.x; break;
    case 1: result = input.y; break;
    case 2: result = input.dx; input.dx = 0; break;
    case 3: result = input.dy; input.dy = 0; break;
    case 4: case 5: case 6: result = input.buttons[value - 4]; break;
    }
    push_int(runtime->stack, result);
    return RUNTIME_STATUS_NORMAL;
}

static s32 mouse_grab(Runtime *runtime, JClass *clazz)
{
    Display *display = awt_x_display();
    Window canvas = lwjgl_canvas_window();
    int grabbed = localvar_getInt(runtime->localvar, 0);
    int ok = 1;
    if (!grabbed) {
        release_pointer();
    } else if (!input.grabbed && display && canvas) {
        if (!input.invisible) {
            static const char empty = 0;
            Pixmap bitmap = XCreateBitmapFromData(display, canvas, &empty, 1, 1);
            XColor color = {0};
            input.invisible = XCreatePixmapCursor(display, bitmap, bitmap, &color, &color, 0, 0);
            XFreePixmap(display, bitmap);
        }
        ok = XGrabPointer(display, canvas, True,
            PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
            GrabModeAsync, canvas, input.invisible, CurrentTime) == GrabSuccess;
        if (ok) {
            int width, height;
            lwjgl_canvas_size(&width, &height);
            input.grabbed = 1;
            XWarpPointer(display, None, canvas, 0, 0, 0, 0, width / 2, height / 2);
        }
    } else if (!display || !canvas) {
        ok = 0;
    }
    input.dx = input.dy = 0;
    XFlush(display);
    push_int(runtime->stack, ok);
    return RUNTIME_STATUS_NORMAL;
}

static s32 mouse_position(Runtime *runtime, JClass *clazz)
{
    int width, height;
    lwjgl_canvas_size(&width, &height);
    int x = localvar_getInt(runtime->localvar, 0);
    int y = localvar_getInt(runtime->localvar, 1);
    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    if (y >= height) y = height - 1;
    input.x = x;
    input.y = y;
    XWarpPointer(awt_x_display(), None, lwjgl_canvas_window(), 0, 0, 0, 0, x, height - 1 - y);
    XFlush(awt_x_display());
    return RUNTIME_STATUS_NORMAL;
}

static s32 cursor_create(Runtime *runtime, JClass *clazz)
{
    Display *display = awt_x_display();
    int width = localvar_getInt(runtime->localvar, 0), height = localvar_getInt(runtime->localvar, 1);
    int x = localvar_getInt(runtime->localvar, 2), y = localvar_getInt(runtime->localvar, 3);
    Instance *pixels = localvar_getRefer(runtime->localvar, 4);
    int stride = (width + 7) / 8;
    char *source = calloc(height, stride), *mask = calloc(height, stride);
    Cursor cursor = None;
    if (display && source && mask) {
        unsigned *colors = (unsigned *) pixels->arr_body;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                unsigned color = colors[row * width + column];
                int offset = (height - 1 - row) * stride + column / 8;
                unsigned bit = 1u << (column % 8);
                if (color >> 24 >= 128) mask[offset] |= bit;
                if ((color & 0xffffff) >= 0x808080) source[offset] |= bit;
            }
        }
        Pixmap bitmap = XCreateBitmapFromData(display, DefaultRootWindow(display), source, width, height);
        Pixmap alpha = XCreateBitmapFromData(display, DefaultRootWindow(display), mask, width, height);
        XColor white = {.red = 65535, .green = 65535, .blue = 65535}, black = {0};
        cursor = XCreatePixmapCursor(display, bitmap, alpha, &white, &black, x, height - 1 - y);
        XFreePixmap(display, bitmap);
        XFreePixmap(display, alpha);
    }
    free(source);
    free(mask);
    push_long(runtime->stack, cursor);
    return RUNTIME_STATUS_NORMAL;
}

static s32 cursor_destroy(Runtime *runtime, JClass *clazz)
{
    XFreeCursor(awt_x_display(), localvar_getLong(runtime->localvar, 0));
    return RUNTIME_STATUS_NORMAL;
}

void lwjgl_input_close(void)
{
    release_pointer();
    if (input.invisible) XFreeCursor(awt_x_display(), input.invisible);
    memset(&input, 0, sizeof(input));
}

#define INPUT_METHOD(cls, name, signature, fn) {"org/lwjgl/input/" cls, name, signature, fn}

void reg_lwjgl_input_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        INPUT_METHOD("Keyboard", "nCreate", "()Z", keyboard_create),
        INPUT_METHOD("Keyboard", "nDestroy", "()V", keyboard_destroy),
        INPUT_METHOD("Keyboard", "nNext", "()[I", keyboard_next),
        INPUT_METHOD("Keyboard", "nIsKeyDown", "(I)Z", keyboard_state),
        INPUT_METHOD("Keyboard", "nRepeat", "(Z)V", keyboard_repeat),
        INPUT_METHOD("Mouse", "nCreate", "()Z", mouse_create),
        INPUT_METHOD("Mouse", "nDestroy", "()V", mouse_destroy),
        INPUT_METHOD("Mouse", "nNext", "()[I", mouse_next),
        INPUT_METHOD("Mouse", "nValue", "(I)I", mouse_value),
        INPUT_METHOD("Mouse", "nGrab", "(Z)Z", mouse_grab),
        INPUT_METHOD("Mouse", "nPosition", "(II)V", mouse_position),
        INPUT_METHOD("Cursor", "nCreate", "(IIII[I)J", cursor_create),
        INPUT_METHOD("Cursor", "nDestroy", "(J)V", cursor_destroy),
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
