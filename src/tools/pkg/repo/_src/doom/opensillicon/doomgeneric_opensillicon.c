/*
 * doomgeneric back-end for OpenSillicon: X11 window through plain Xlib.
 *
 * Notes about the target X server (tinyx/kdrive, opensillicon driver):
 *   - 24-bit TrueColor, 32 bits per pixel, little-endian, no MIT-SHM and no
 *     XKB extension.  The frame therefore goes out through XPutImage and the
 *     key translation uses core XLookupKeysym instead of the Xkb helpers.
 *   - Keys are auto-repeated by faking release/press pairs.  Since the server
 *     cannot report detectable auto-repeat, matching releases are filtered out
 *     so that Doom keeps seeing a held key.
 *
 * Build with DOOMGENERIC_RESX/DOOMGENERIC_RESY set (see src/user/Makefile).
 */

#include "doomkeys.h"
#include "doomgeneric.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#define KEYQUEUE_SIZE 256

static Display *display;
static Window window;
static GC gc;
static XImage *image;
static int screen;
static Atom wm_delete;

static unsigned short key_queue[KEYQUEUE_SIZE];
static unsigned int key_queue_write;
static unsigned int key_queue_read;

static void queue_key(int pressed, unsigned int key_code)
{
	static const struct {
		KeySym sym;
		unsigned char key;
	} map[] = {
		{ XK_Return,     KEY_ENTER },
		{ XK_Escape,     KEY_ESCAPE },
		{ XK_Tab,        KEY_TAB },
		{ XK_BackSpace,  KEY_BACKSPACE },
		{ XK_Left,       KEY_LEFTARROW },
		{ XK_Right,      KEY_RIGHTARROW },
		{ XK_Up,         KEY_UPARROW },
		{ XK_Down,       KEY_DOWNARROW },
		{ XK_Control_L,  KEY_FIRE },
		{ XK_Control_R,  KEY_FIRE },
		{ XK_space,      KEY_USE },
		{ XK_Shift_L,    KEY_RSHIFT },
		{ XK_Shift_R,    KEY_RSHIFT },
		{ XK_Alt_L,      KEY_RALT },
		{ XK_Alt_R,      KEY_RALT },
		{ XK_minus,      KEY_MINUS },
		{ XK_equal,      KEY_EQUALS },
		{ XK_F1,         KEY_F1 },
		{ XK_F2,         KEY_F2 },
		{ XK_F3,         KEY_F3 },
		{ XK_F4,         KEY_F4 },
		{ XK_F5,         KEY_F5 },
		{ XK_F6,         KEY_F6 },
		{ XK_F7,         KEY_F7 },
		{ XK_F8,         KEY_F8 },
		{ XK_F9,         KEY_F9 },
		{ XK_F10,        KEY_F10 },
		{ XK_F11,        KEY_F11 },
		{ XK_F12,        KEY_F12 },
		{ NoSymbol,      0 }
	};
	unsigned char key;
	unsigned int i;

	for (i = 0; map[i].sym != NoSymbol; i++) {
		if ((KeySym)key_code == map[i].sym) {
			key = map[i].key;
			goto push;
		}
	}

	/* Letters and printable ASCII keep their Doom key codes. */
	if (key_code <= 0xff) {
		key = (unsigned char)tolower((int)key_code);
		goto push;
	}

	return;

push:
	key_queue[key_queue_write] = (unsigned short)((pressed << 8) | key);
	key_queue_write = (key_queue_write + 1) % KEYQUEUE_SIZE;
	if (key_queue_write == key_queue_read)
		key_queue_read = (key_queue_read + 1) % KEYQUEUE_SIZE;
}

static void pump_events(void)
{
	while (XPending(display) > 0) {
		XEvent e;

		XNextEvent(display, &e);

		switch (e.type) {
		case KeyPress:
			queue_key(1, XLookupKeysym(&e.xkey, 0));
			break;
		case KeyRelease:
			/*
			 * Auto-repeat: the server sends release immediately
			 * followed by a press of the same key at the same
			 * timestamp.  Drop both so the key stays "held".
			 */
			if (XPending(display) > 0) {
				XEvent next;

				XPeekEvent(display, &next);
				if (next.type == KeyPress &&
				    next.xkey.keycode == e.xkey.keycode &&
				    next.xkey.time == e.xkey.time) {
					XNextEvent(display, &next);
					break;
				}
			}
			queue_key(0, XLookupKeysym(&e.xkey, 0));
			break;
		case Expose:
			if (e.xexpose.count == 0)
				XPutImage(display, window, gc, image, 0, 0, 0, 0,
				    DOOMGENERIC_RESX, DOOMGENERIC_RESY);
			break;
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] == wm_delete)
				exit(0);
			break;
		case DestroyNotify:
			exit(0);
		default:
			break;
		}
	}
}

void DG_Init(void)
{
	XSetWindowAttributes attrs;
	XSizeHints *hints;
	XClassHint class_hint;
	unsigned int depth;
	int black;

	memset(key_queue, 0, sizeof(key_queue));
	key_queue_write = 0;
	key_queue_read = 0;

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "doom: cannot open X display\n");
		exit(1);
	}

	screen = DefaultScreen(display);
	depth = (unsigned int)DefaultDepth(display, screen);
	black = BlackPixel(display, screen);

	memset(&attrs, 0, sizeof(attrs));
	attrs.background_pixel = (unsigned long)black;
	attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

	window = XCreateWindow(display, DefaultRootWindow(display), 0, 0,
	    DOOMGENERIC_RESX, DOOMGENERIC_RESY, 0, depth, InputOutput,
	    DefaultVisual(display, screen), CWBackPixel | CWEventMask, &attrs);

	XStoreName(display, window, "DOOM");

	class_hint.res_name = "doom";
	class_hint.res_class = "Doom";
	XSetClassHint(display, window, &class_hint);

	/* Keep the window at the native resolution; Doom cannot rescale. */
	hints = XAllocSizeHints();
	if (hints != NULL) {
		hints->flags = PMinSize | PMaxSize;
		hints->min_width = hints->max_width = DOOMGENERIC_RESX;
		hints->min_height = hints->max_height = DOOMGENERIC_RESY;
		XSetWMNormalHints(display, window, hints);
		XFree(hints);
	}

	wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete, 1);

	XSelectInput(display, window,
	    ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask);

	gc = XCreateGC(display, window, 0, NULL);

	XMapWindow(display, window);

	/* Wait until the window manager has mapped us. */
	for (;;) {
		XEvent e;

		XNextEvent(display, &e);
		if (e.type == MapNotify)
			break;
	}

	image = XCreateImage(display, DefaultVisual(display, screen), depth,
	    ZPixmap, 0, (char *)DG_ScreenBuffer, DOOMGENERIC_RESX,
	    DOOMGENERIC_RESY, 32, 0);
	if (image == NULL) {
		fprintf(stderr, "doom: cannot create XImage\n");
		exit(1);
	}

	if (image->bits_per_pixel != 32 || image->byte_order != LSBFirst) {
		fprintf(stderr, "doom: unsupported framebuffer: %d bpp, order %d\n",
		    image->bits_per_pixel, image->byte_order);
		exit(1);
	}

	XSync(display, False);
}

void DG_DrawFrame(void)
{
	if (display == NULL)
		return;

	pump_events();

	XPutImage(display, window, gc, image, 0, 0, 0, 0,
	    DOOMGENERIC_RESX, DOOMGENERIC_RESY);
	XFlush(display);
}

void DG_SleepMs(uint32_t ms)
{
	if (ms > 0)
		usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
	struct timeval tp;

	gettimeofday(&tp, NULL);

	return (uint32_t)(tp.tv_sec * 1000 + tp.tv_usec / 1000);
}

int DG_GetKey(int *pressed, unsigned char *doom_key)
{
	unsigned short data;

	if (key_queue_read == key_queue_write)
		return 0;

	data = key_queue[key_queue_read];
	key_queue_read = (key_queue_read + 1) % KEYQUEUE_SIZE;

	*pressed = data >> 8;
	*doom_key = (unsigned char)(data & 0xff);

	return 1;
}

void DG_SetWindowTitle(const char *title)
{
	if (display != NULL && window != 0)
		XStoreName(display, window, title);
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);

	for (;;)
		doomgeneric_Tick();

	return 0;
}
