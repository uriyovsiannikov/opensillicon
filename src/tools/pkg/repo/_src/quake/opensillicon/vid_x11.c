/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "quakedef.h"
#include "d_local.h"

enum { WIDTH = 480, HEIGHT = 360 };
viddef_t vid;
unsigned short d_8to16table[256];
unsigned d_8to24table[256];
static byte pixels[WIDTH * HEIGHT];
static Display *display;
static Window window;
static GC gc;
static XImage *image;
static Atom wm_protocols, wm_delete;
static qboolean mouse_enabled, focused, grabbed;
static Cursor hidden_cursor;
static int dx, dy;

static unsigned long channel(unsigned value, unsigned long mask)
{
	unsigned shift = 0;
	if (mask == 0)
		return 0;
	while (!(mask & 1)) {
		shift++;
		mask >>= 1;
	}
	return ((mask * value + 127) / 255) << shift;
}

void VID_SetPalette(unsigned char *palette)
{
	if (!image)
		return;
	for (unsigned i = 0; i < 256; i++)
		d_8to24table[i] = channel(palette[3*i], image->red_mask)
		    | channel(palette[3*i+1], image->green_mask)
		    | channel(palette[3*i+2], image->blue_mask);
}

void VID_ShiftPalette(unsigned char *palette) { VID_SetPalette(palette); }

void VID_Init(unsigned char *palette)
{
	display = XOpenDisplay(NULL);
	if (!display)
		Sys_Error("cannot open X11 display; run quake from startx");
	int screen = DefaultScreen(display);
	Visual *visual = DefaultVisual(display, screen);
	if (visual->class != TrueColor)
		Sys_Error("the X11 display must use TrueColor");
	window = XCreateSimpleWindow(display, RootWindow(display, screen),
	    80, 80, WIDTH, HEIGHT, 0, 0, 0);
	gc = XCreateGC(display, window, 0, NULL);
	image = XCreateImage(display, visual, DefaultDepth(display, screen),
	    ZPixmap, 0, NULL, WIDTH, HEIGHT, 32, 0);
	if (!image || !(image->data = calloc(HEIGHT, image->bytes_per_line)))
		Sys_Error("could not allocate X11 frame image");

	XSizeHints hints = {0};
	hints.flags = PPosition | PMinSize | PMaxSize;
	hints.x = hints.y = 80;
	hints.min_width = hints.max_width = WIDTH;
	hints.min_height = hints.max_height = HEIGHT;
	XSetWMNormalHints(display, window, &hints);
	XStoreName(display, window, "Quake");
	wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
	wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete, 1);
	XSelectInput(display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask |
	    PointerMotionMask | ButtonPressMask | ButtonReleaseMask);
	XMapWindow(display, window);

	static const char blank[] = {0};
	Pixmap bitmap = XCreateBitmapFromData(display, window, blank, 1, 1);
	XColor color = {0};
	hidden_cursor = XCreatePixmapCursor(display, bitmap, bitmap, &color, &color, 0, 0);
	XFreePixmap(display, bitmap);

	vid.width = vid.conwidth = WIDTH;
	vid.height = vid.conheight = HEIGHT;
	vid.rowbytes = vid.conrowbytes = WIDTH;
	vid.buffer = vid.conbuffer = pixels;
	vid.aspect = ((float)HEIGHT / WIDTH) * (320.0f / 240.0f);
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong(((int *)vid.colormap)[2048]);
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;

	int cache_size = D_SurfaceCacheForRes(WIDTH, HEIGHT);
	d_pzbuffer = Hunk_HighAllocName(WIDTH * HEIGHT * sizeof(*d_pzbuffer) + cache_size, "video");
	if (!d_pzbuffer)
		Sys_Error("could not allocate software renderer buffers");
	D_InitCaches(d_pzbuffer + WIDTH * HEIGHT, cache_size);
	VID_SetPalette(palette);
}

void VID_Shutdown(void)
{
	if (!display)
		return;
	if (image)
		XDestroyImage(image);
	if (hidden_cursor)
		XFreeCursor(display, hidden_cursor);
	if (gc)
		XFreeGC(display, gc);
	XCloseDisplay(display);
	display = NULL;
}

void VID_Update(vrect_t *rects)
{
	(void)rects;
	if (image->bits_per_pixel == 32 && image->byte_order == LSBFirst) {
		for (int y = 0; y < HEIGHT; y++) {
			uint32_t *row = (uint32_t *)(image->data + y * image->bytes_per_line);
			for (int x = 0; x < WIDTH; x++)
				row[x] = d_8to24table[pixels[y * WIDTH + x]];
		}
	} else {
		for (int y = 0; y < HEIGHT; y++)
			for (int x = 0; x < WIDTH; x++)
				XPutPixel(image, x, y, d_8to24table[pixels[y * WIDTH + x]]);
	}
	XPutImage(display, window, gc, image, 0, 0, 0, 0, WIDTH, HEIGHT);
	XFlush(display);
}

void D_BeginDirectRect(int x, int y, byte *bitmap, int width, int height)
{
	/* The disk icon is optional and the next frame repaints the image. */
	(void)x; (void)y; (void)bitmap; (void)width; (void)height;
}

void D_EndDirectRect(int x, int y, int width, int height)
{
	(void)x; (void)y; (void)width; (void)height;
}

static int quake_key(XKeyEvent *event)
{
	KeySym key = XLookupKeysym(event, 0);
	if (key >= XK_F1 && key <= XK_F12)
		return K_F1 + key - XK_F1;
	if (key >= XK_space && key <= XK_asciitilde)
		return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;

	switch (key) {
	case XK_Left: return K_LEFTARROW;
	case XK_Right: return K_RIGHTARROW;
	case XK_Up: return K_UPARROW;
	case XK_Down: return K_DOWNARROW;
	case XK_Escape: return K_ESCAPE;
	case XK_Return: case XK_KP_Enter: return K_ENTER;
	case XK_Tab: return K_TAB;
	case XK_BackSpace: return K_BACKSPACE;
	case XK_Shift_L: case XK_Shift_R: return K_SHIFT;
	case XK_Control_L: case XK_Control_R: return K_CTRL;
	case XK_Alt_L: case XK_Alt_R: return K_ALT;
	case XK_Insert: return K_INS;
	case XK_Delete: return K_DEL;
	case XK_Page_Up: return K_PGUP;
	case XK_Page_Down: return K_PGDN;
	case XK_Home: return K_HOME;
	case XK_End: return K_END;
	case XK_Pause: return K_PAUSE;
	default: return 0;
	}
}

static void update_grab(void)
{
	qboolean want = focused && mouse_enabled && key_dest == key_game && !cls.demoplayback;
	if (want && !grabbed) {
		grabbed = XGrabPointer(display, window, False,
		    PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		    GrabModeAsync, GrabModeAsync, window, hidden_cursor, CurrentTime) == GrabSuccess;
		if (grabbed)
			XWarpPointer(display, None, window, 0, 0, 0, 0, WIDTH/2, HEIGHT/2);
	} else if (!want && grabbed) {
		XUngrabPointer(display, CurrentTime);
		grabbed = false;
		dx = dy = 0;
	}
}

void Sys_SendKeyEvents(void)
{
	if (!display)
		return;
	int motion_x = WIDTH/2, motion_y = HEIGHT/2;
	while (XPending(display)) {
		XEvent event;
		XNextEvent(display, &event);
		switch (event.type) {
		case ClientMessage:
			if (event.xclient.message_type == wm_protocols &&
			    event.xclient.format == 32 && (Atom)event.xclient.data.l[0] == wm_delete)
				Sys_Quit();
			break;
		case FocusIn: focused = true; break;
		case FocusOut:
			focused = false;
			Key_ClearStates();
			update_grab();
			break;
		case KeyPress: case KeyRelease: {
			int key = quake_key(&event.xkey);
			if (key)
				Key_Event(key, event.type == KeyPress);
			break;
		}
		case ButtonPress: case ButtonRelease:
			if (grabbed && event.xbutton.button >= Button1 && event.xbutton.button <= Button3) {
				int key = event.xbutton.button == Button1 ? K_MOUSE1 :
				    event.xbutton.button == Button3 ? K_MOUSE2 : K_MOUSE3;
				Key_Event(key, event.type == ButtonPress);
			}
			break;
		case MotionNotify:
			if (grabbed) {
				motion_x = event.xmotion.x;
				motion_y = event.xmotion.y;
			}
			break;
		}
	}
	if (grabbed && (motion_x != WIDTH/2 || motion_y != HEIGHT/2)) {
		dx += motion_x - WIDTH/2;
		dy += motion_y - HEIGHT/2;
		XWarpPointer(display, None, window, 0, 0, 0, 0, WIDTH/2, HEIGHT/2);
	}
	update_grab();
}

void IN_Init(void) { mouse_enabled = !COM_CheckParm("-nomouse"); }
void IN_Shutdown(void) { mouse_enabled = false; }
void IN_Commands(void) {}
void IN_ClearStates(void) { dx = dy = 0; }

void IN_Move(usercmd_t *cmd)
{
	extern kbutton_t in_strafe, in_mlook;
	float x = dx * sensitivity.value, y = dy * sensitivity.value;
	dx = dy = 0;

	if (in_strafe.state & 1 || (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * x;
	else
		cl.viewangles[YAW] -= m_yaw.value * x;
	if (in_mlook.state & 1)
		V_StopPitchDrift();
	if ((in_mlook.state & 1) && !(in_strafe.state & 1)) {
		cl.viewangles[PITCH] += m_pitch.value * y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	} else {
		cmd->forwardmove -= m_forward.value * y;
	}
}
