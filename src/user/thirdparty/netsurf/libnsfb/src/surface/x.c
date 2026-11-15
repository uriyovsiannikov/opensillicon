/*
 * Copyright 2009 Vincent Sanders <vince@simtec.co.uk>
 *
 * This file is part of libnsfb, http://www.netsurf-browser.org/
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#define _XOPEN_SOURCE 500

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <string.h>

#ifndef OPEN_SILLICON
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_keysyms.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "libnsfb_plot.h"
#include "libnsfb_plot_util.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"
#include "cursor.h"

#if defined(NSFB_NEED_HINTS_ALLOC)
static xcb_size_hints_t *
xcb_alloc_size_hints(void)
{
    return calloc(1, sizeof(xcb_size_hints_t));
}

static void
xcb_free_size_hints(xcb_size_hints_t *hints)
{
    free(hints);
}
#endif

#if defined(NSFB_NEED_ICCCM_API_PREFIX)
#define xcb_size_hints_set_max_size xcb_icccm_size_hints_set_max_size
#define xcb_size_hints_set_min_size xcb_icccm_size_hints_set_min_size
#define xcb_set_wm_size_hints       xcb_icccm_set_wm_size_hints
#endif

#if (NSFB_XCBPROTO_MAJOR_VERSION > 1) || \
    (NSFB_XCBPROTO_MAJOR_VERSION == 1 && NSFB_XCBPROTO_MINOR_VERSION >= 6)
#define WM_NORMAL_HINTS XCB_ATOM_WM_NORMAL_HINTS
#endif

#define X_BUTTON_LEFT 1
#define X_BUTTON_MIDDLE 2
#define X_BUTTON_RIGHT 3
#define X_BUTTON_WHEELUP 4
#define X_BUTTON_WHEELDOWN 5

typedef struct xstate_s {
    xcb_connection_t *connection; /* The x server connection */
    xcb_screen_t *screen; /* The screen to put the window on */
    xcb_key_symbols_t *keysymbols; /* keysym mappings */ 

    xcb_shm_segment_info_t shminfo;

    xcb_image_t *image; /* The X image buffer */

    xcb_cursor_t cursor;
    const nsfb_colour_t *cursor_pixels;
    int cursor_width, cursor_height, cursor_stride;
    int cursor_hotspot_x, cursor_hotspot_y;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_window_t window; /* The handle to the window */
    xcb_pixmap_t pmap; /* The handle to the backing pixmap */
    xcb_gcontext_t gc; /* The handle to the pixmap plotting graphics context */
    xcb_shm_seg_t segment; /* The handle to the image shared memory */
} xstate_t;

/* X keyboard codepage to nsfb mapping */
static enum nsfb_key_code_e XCSKeyboardMap[256] = {
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_BACKSPACE, /* XK_BackSpace */
    NSFB_KEY_TAB, /* XK_Tab */
    NSFB_KEY_UNKNOWN, /* XK_Linefeed */
    NSFB_KEY_CLEAR, /* XK_Clear */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_RETURN, /* XK_Return */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x10 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_PAUSE, /* XK_Pause */
    NSFB_KEY_UNKNOWN, /* XK_Scroll_Lock */
    NSFB_KEY_UNKNOWN, /* XK_Sys_Req */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_ESCAPE, /* XK_Escape */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x20 */
    NSFB_KEY_UNKNOWN, /* XK_Multi_key */
    NSFB_KEY_UNKNOWN, /* XK_Kanji */
    NSFB_KEY_UNKNOWN, /* XK_Muhenkan */
    NSFB_KEY_UNKNOWN, /* XK_Henkan_Mode */
    NSFB_KEY_UNKNOWN, /* XK_Henkan */
    NSFB_KEY_UNKNOWN, /* XK_Romaji */
    NSFB_KEY_UNKNOWN, /* XK_Hiragana*/
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x30 */
    NSFB_KEY_UNKNOWN, /* XK_Eisu_toggle */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_Codeinput */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_SingleCandidate */
    NSFB_KEY_UNKNOWN, /* XK_MultipleCandidate */
    NSFB_KEY_UNKNOWN, /* XK_PreviousCandidate */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x40 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x50 */
    NSFB_KEY_HOME, /* XK_Home */
    NSFB_KEY_LEFT, /* XK_Left */
    NSFB_KEY_UP, /* XK_Up */
    NSFB_KEY_RIGHT, /* XK_Right */
    NSFB_KEY_DOWN, /* XK_Down */
    NSFB_KEY_PAGEUP, /* XK_Page_Up */
    NSFB_KEY_PAGEDOWN, /* XK_Page_Down */
    NSFB_KEY_END, /* XK_End */
    NSFB_KEY_UNKNOWN, /* XK_Begin */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x60 */
    NSFB_KEY_UNKNOWN, /* XK_Select */
    NSFB_KEY_UNKNOWN, /* XK_Print*/
    NSFB_KEY_UNKNOWN, /* XK_Execute*/
    NSFB_KEY_UNKNOWN, /* XK_Insert*/
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_Undo*/
    NSFB_KEY_UNKNOWN, /* XK_Redo */
    NSFB_KEY_UNKNOWN, /* XK_Menu */
    NSFB_KEY_UNKNOWN, /* XK_Find */
    NSFB_KEY_UNKNOWN, /* XK_Cancel */
    NSFB_KEY_UNKNOWN, /* XK_Help */
    NSFB_KEY_UNKNOWN, /* XK_Break*/
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x70 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_Mode_switch */
    NSFB_KEY_UNKNOWN, /* XK_Num_Lock */
    /* 0x80 */
    NSFB_KEY_UNKNOWN, /* XK_KP_Space */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_KP_Tab */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_KP_Enter */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0x90 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_KP_F1*/
    NSFB_KEY_UNKNOWN, /* XK_KP_F2*/
    NSFB_KEY_UNKNOWN, /* XK_KP_F3*/
    NSFB_KEY_UNKNOWN, /* XK_KP_F4*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Home*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Left*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Up*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Right*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Down*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Page_Up*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Page_Down*/
    NSFB_KEY_UNKNOWN, /* XK_KP_End*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Begin*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Insert*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Delete*/
    /* 0xa0 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_KP_Multiply*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Add*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Separator*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Subtract*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Decimal*/
    NSFB_KEY_UNKNOWN, /* XK_KP_Divide*/
    /* 0xb0 */
    NSFB_KEY_UNKNOWN, /* XK_KP_0 */
    NSFB_KEY_UNKNOWN, /* XK_KP_1 */
    NSFB_KEY_UNKNOWN, /* XK_KP_2 */
    NSFB_KEY_UNKNOWN, /* XK_KP_3 */
    NSFB_KEY_UNKNOWN, /* XK_KP_4 */
    NSFB_KEY_UNKNOWN, /* XK_KP_5 */
    NSFB_KEY_UNKNOWN, /* XK_KP_6 */
    NSFB_KEY_UNKNOWN, /* XK_KP_7 */
    NSFB_KEY_UNKNOWN, /* XK_KP_8 */
    NSFB_KEY_UNKNOWN, /* XK_KP_9 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_F1, /* XK_F1 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* XK_KP_Equal */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_F2, /* XK_F2 */
    /* 0xc0 */
    NSFB_KEY_F3, /* XK_F3*/
    NSFB_KEY_F4, /* XK_F4*/
    NSFB_KEY_F5, /* XK_F5*/
    NSFB_KEY_F6, /* XK_F6*/
    NSFB_KEY_F7, /* XK_F7*/
    NSFB_KEY_F8, /* XK_F8*/
    NSFB_KEY_F9, /* XK_F9*/
    NSFB_KEY_F10, /* XK_F10*/
    NSFB_KEY_F11, /* XK_F11*/
    NSFB_KEY_F12, /* XK_F12*/
    NSFB_KEY_F13, /* XK_F13 */
    NSFB_KEY_F14, /* XK_F14 */
    NSFB_KEY_F15, /* XK_F15 */
    NSFB_KEY_UNKNOWN, /* XK_F16 */
    NSFB_KEY_UNKNOWN, /* XK_F17 */
    NSFB_KEY_UNKNOWN, /* XK_F18*/
    /* 0xd0 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    /* 0xe0 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_LSHIFT, /* XK_Shift_L*/
    NSFB_KEY_RSHIFT, /* XK_Shift_R*/
    NSFB_KEY_UNKNOWN, /* XK_Control_L*/
    NSFB_KEY_UNKNOWN, /* XK_Control_R*/
    NSFB_KEY_UNKNOWN, /* XK_Caps_Lock*/
    NSFB_KEY_UNKNOWN, /* XK_Shift_Lock*/
    NSFB_KEY_UNKNOWN, /* XK_Meta_L*/
    NSFB_KEY_UNKNOWN, /* XK_Meta_R*/
    NSFB_KEY_UNKNOWN, /* XK_Alt_L */
    NSFB_KEY_UNKNOWN, /* XK_Alt_R*/
    NSFB_KEY_UNKNOWN, /* XK_Super_L*/
    NSFB_KEY_UNKNOWN, /* XK_Super_R*/
    NSFB_KEY_UNKNOWN, /* XK_Hyper_L*/
    NSFB_KEY_UNKNOWN, /* XK_Hyper_R*/
    NSFB_KEY_UNKNOWN, /* */
    /* 0xf0 */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
    NSFB_KEY_UNKNOWN, /* */
};

/* Convert an X keysym into a nsfb key code.
 *
 * Our approach is primarily to assume both codings are roughly ascii like.
 *
 * The Keysyms are defined in X11/keysymdef.h and our mapping for the
 * "keyboard" keys i.e. those from code set 255 is from there
 */
static enum nsfb_key_code_e
xkeysym_to_nsfbkeycode(xcb_keysym_t ks)
{
    enum nsfb_key_code_e nsfb_key = NSFB_KEY_UNKNOWN;
    uint8_t codeset = (ks & 0xFF00) >> 8;
    uint8_t chrcode = ks & 0xFF;

    if (ks != XCB_NO_SYMBOL) {
        
        switch (codeset) {
        case 0x00: /* Latin 1 */
        case 0x01: /* Latin 2 */
        case 0x02: /* Latin 3 */
        case 0x03: /* Latin 4 */
        case 0x04: /* Katakana */
        case 0x05: /* Arabic */
        case 0x06: /* Cyrillic */
        case 0x07: /* Greek */
        case 0x08: /* Technical */
        case 0x0A: /* Publishing */
        case 0x0C: /* Hebrew */
        case 0x0D: /* Thai */
            /* this is somewhat incomplete, but the nsfb codes are lined up on
             * the ascii codes and x seems to have done similar 
             */
            nsfb_key = (enum nsfb_key_code_e)chrcode;
            break;

        case 0xFF: /* Keyboard */
            nsfb_key = XCSKeyboardMap[chrcode];
            break;
        }
    }

    return nsfb_key;
}
/*
  static void
  set_palette(nsfb_t *nsfb)
  {
  X_Surface *x_screen = nsfb->surface_priv;
  X_Color palette[256];
  int rloop, gloop, bloop;
  int loop = 0;

  // build a linear R:3 G:3 B:2 colour cube palette.
  for (rloop = 0; rloop < 8; rloop++) {
  for (gloop = 0; gloop < 8; gloop++) {
  for (bloop = 0; bloop < 4; bloop++) {
  palette[loop].r = (rloop << 5) | (rloop << 2) | (rloop >> 1);
  palette[loop].g = (gloop << 5) | (gloop << 2) | (gloop >> 1);
  palette[loop].b = (bloop << 6) | (bloop << 4) | (bloop << 2) | (bloop);
  nsfb->palette[loop] = palette[loop].r |
  palette[loop].g << 8 |
  palette[loop].b << 16;
  loop++;
  }
  }
  }

  // Set palette
  //X_SetColors(x_screen, palette, 0, 256);

  }
*/
static int
update_pixmap(xstate_t *xstate, int x, int y, int width, int height)
{
    if (xstate->shminfo.shmseg == 0) {
        /* not using shared memory */
        /* Bound PutImage payloads by the negotiated XCB request limit.
         * A full browser window can exceed a core X11 request. */
        size_t maximum = (size_t)xcb_get_maximum_request_length(xstate->connection) * 4;
        size_t overhead = sizeof(xcb_put_image_request_t) + 4;
        size_t stride = xstate->image->stride;
        if (stride == 0 || maximum <= overhead || maximum - overhead < stride)
            return -1;
        size_t rows = (maximum - overhead) / stride;
        while (height > 0) {
            int batch = (size_t)height < rows ? height : (int)rows;
            xcb_put_image(xstate->connection, xstate->image->format,
                          xstate->pmap, xstate->gc, xstate->image->width,
                          batch, 0, y, 0, xstate->image->depth,
                          (uint32_t)(batch * stride),
                          xstate->image->data + y * stride);
            y += batch;
            height -= batch;
        }
    } else {
        /* shared memory */
        xcb_image_shm_put(xstate->connection, 
                          xstate->pmap, 
                          xstate->gc, 
                          xstate->image, 
                          xstate->shminfo, 
                          x,y,
                          x,y,
                          width,height,0);
    }

    return 0;
}

static int
update_and_redraw_pixmap(xstate_t *xstate, int x, int y, int width, int height)
{
    update_pixmap(xstate, x, y, width, height);

    xcb_copy_area(xstate->connection,
                  xstate->pmap,
                  xstate->window,
                  xstate->gc,
                  x, y,
                  x, y,
                  width, height);

    xcb_flush(xstate->connection);

    return 0;
}


static bool
xcopy(nsfb_t *nsfb, nsfb_bbox_t *srcbox, nsfb_bbox_t *dstbox)
{
    xstate_t *xstate = nsfb->surface_priv;
    uint8_t *srcptr;
    uint8_t *dstptr;
    int srcx = srcbox->x0;
    int srcy = srcbox->y0;
    int dstx = dstbox->x0;
    int dsty = dstbox->y0;
    int width = dstbox->x1 - dstbox->x0;
    int height = dstbox->y1 - dstbox->y0;
    int hloop;

    /* copy the area on the server */
    xcb_copy_area(xstate->connection,
                  xstate->pmap,
                  xstate->pmap,
                  xstate->gc,
                  srcbox->x0, 
                  srcbox->y0,
                  dstbox->x0, 
                  dstbox->y0,
                  srcbox->x1 - srcbox->x0, 
                  srcbox->y1 - srcbox->y0);

    /* do the copy in the local memory too */
    srcptr = (nsfb->ptr +
              (srcy * nsfb->linelen) +
              ((srcx * nsfb->bpp) / 8));

    dstptr = (nsfb->ptr +
              (dsty * nsfb->linelen) +
              ((dstx * nsfb->bpp) / 8));

    if (width == nsfb->width) {
        /* take shortcut and use memmove */
        memmove(dstptr, srcptr, (width * height * nsfb->bpp) / 8);
    } else {
        if (srcy > dsty) {
            for (hloop = height; hloop > 0; hloop--) {
                memmove(dstptr, srcptr, (width * nsfb->bpp) / 8);
                srcptr += nsfb->linelen;
                dstptr += nsfb->linelen;
            }
        } else {
            srcptr += height * nsfb->linelen;
            dstptr += height * nsfb->linelen;
            for (hloop = height; hloop > 0; hloop--) {
                srcptr -= nsfb->linelen;
                dstptr -= nsfb->linelen;
                memmove(dstptr, srcptr, (width * nsfb->bpp) / 8);
            }
        }
    }

    /* update the x window */
    xcb_copy_area(xstate->connection,
                  xstate->pmap,
                  xstate->window,
                  xstate->gc,
                  dstx, dsty,
                  dstx, dsty,
                  width, height);

    return true;

}


static int 
x_set_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
    if (nsfb->surface_priv != NULL)
        return -1; /* if were already initialised fail */

    nsfb->width = width;
    nsfb->height = height;
    nsfb->format = format;

    /* select default sw plotters for format */
    select_plotters(nsfb);

    nsfb->plotter_fns->copy = xcopy;

    return 0;
}


static xcb_format_t *
find_format(xcb_connection_t * c, uint8_t depth, uint8_t bpp)
{
    const xcb_setup_t *setup = xcb_get_setup(c);
    xcb_format_t *fmt = xcb_setup_pixmap_formats(setup);
    xcb_format_t *fmtend = fmt + xcb_setup_pixmap_formats_length(setup);

    for(; fmt != fmtend; ++fmt) {
        if((fmt->depth == depth) && (fmt->bits_per_pixel == bpp)) {
            return fmt;
        }
    }
    return 0;
}

static xcb_image_t *
create_shm_image(xstate_t *xstate, int width, int height, int bpp)
{
#ifdef OPEN_SILLICON
    /* Native shared mappings do not expose the SysV SHM protocol. */
    (void)xstate; (void)width; (void)height; (void)bpp;
    return NULL;
#else
    const xcb_setup_t *setup = xcb_get_setup(xstate->connection);
    unsigned char *image_data;
    xcb_format_t *fmt;
    int depth = bpp;
    uint32_t image_size;
    int shmid;

    xcb_shm_query_version_reply_t *rep;
    xcb_shm_query_version_cookie_t ck;
    xcb_void_cookie_t shm_attach_cookie;
    xcb_generic_error_t *generic_error;

    ck = xcb_shm_query_version(xstate->connection);
    rep = xcb_shm_query_version_reply(xstate->connection, ck , NULL);
    if (!rep) {
        fprintf (stderr, "Server has no shm support.\n");
        return NULL;
    }

    if ((rep->major_version < 1) ||
        (rep->major_version == 1 && rep->minor_version == 0)) {
        fprintf(stderr, "server SHM support is insufficient.\n");
        free(rep);
        return NULL;
    }
    free(rep);

    if (bpp == 32)
        depth = 24;

    fmt = find_format(xstate->connection, depth, bpp);
    if (fmt == NULL)
        return NULL;

    /* doing it this way ensures we deal with bpp smaller than 8 */
    image_size = (bpp * width * height) >> 3;

    /* get the shared memory segment */
    shmid = shmget(IPC_PRIVATE, image_size, IPC_CREAT|0777);
    if (shmid == -1)
        return NULL;

    xstate->shminfo.shmid = shmid;

    xstate->shminfo.shmaddr = shmat(xstate->shminfo.shmid, 0, 0);
    image_data = xstate->shminfo.shmaddr;

    xstate->shminfo.shmseg = xcb_generate_id(xstate->connection);
    shm_attach_cookie = xcb_shm_attach_checked(xstate->connection,
					       xstate->shminfo.shmseg,
					       xstate->shminfo.shmid, 
					       0);
    generic_error = xcb_request_check(xstate->connection, shm_attach_cookie);

    /* either there is an error and the shm us no longer needed, or it now
     * belongs to the x server - regardless release local reference to shared
     * memory segment
     */
    shmctl(xstate->shminfo.shmid, IPC_RMID, 0);

    if (generic_error != NULL) {
        /* unable to attach shm */
        xstate->shminfo.shmseg = 0;

        free(generic_error);
        return NULL;
    }


    return xcb_image_create(width,
                            height,
                            XCB_IMAGE_FORMAT_Z_PIXMAP,
                            fmt->scanline_pad,
                            fmt->depth,
                            fmt->bits_per_pixel,
                            0,
                            setup->image_byte_order,
                            XCB_IMAGE_ORDER_LSB_FIRST,
                            image_data,
                            image_size,
                            image_data);
#endif
}


static xcb_image_t *
create_image(xcb_connection_t *c, int width, int height, int bpp)
{
    const xcb_setup_t *setup = xcb_get_setup(c);
    unsigned char *image_data;
    xcb_format_t *fmt;
    int depth = bpp;
    uint32_t image_size;

    if (bpp == 32)
        depth = 24;

    fmt = find_format(c, depth, bpp);
    if (fmt == NULL)
        return NULL;

    /* doing it this way ensures we deal with bpp smaller than 8 */
    image_size = (bpp * width * height) >> 3;

    image_data = calloc(1, image_size);
    if (image_data == NULL)
        return NULL;

    return xcb_image_create(width,
                            height,
                            XCB_IMAGE_FORMAT_Z_PIXMAP,
                            fmt->scanline_pad,
                            fmt->depth,
                            fmt->bits_per_pixel,
                            0,
                            setup->image_byte_order,
                            XCB_IMAGE_ORDER_LSB_FIRST,
                            image_data,
                            image_size,
                            image_data);
}

static int x_initialise(nsfb_t *nsfb)
{
    uint32_t mask;
    uint32_t values[3];
    xcb_size_hints_t *hints;
    xstate_t *xstate = nsfb->surface_priv;

    if (xstate != NULL)
        return -1; /* already initialised */

    /* sanity check bpp. */
    if ((nsfb->bpp != 32) && (nsfb->bpp != 16) && (nsfb->bpp != 8))
        return -1;

    xstate = calloc(1, sizeof(xstate_t));
    if (xstate == NULL)
        return -1; /* no memory */

    /* open connection with the server */
    xstate->connection = xcb_connect(NULL, NULL);
    if (xstate->connection == NULL) {
        fprintf(stderr, "Memory error opening display\n");
        free(xstate);
        return -1; /* no memory */	
    }

    if (xcb_connection_has_error(xstate->connection) != 0) {
        fprintf(stderr, "Error opening display\n");
        free(xstate);
        return -1; /* no memory */	
    }

    /* get screen */
    xstate->screen = xcb_setup_roots_iterator(xcb_get_setup(xstate->connection)).data;

    /* create image */
    xstate->image = create_shm_image(xstate, nsfb->width, nsfb->height, nsfb->bpp);

    if (xstate->image == NULL)
        xstate->image = create_image(xstate->connection, nsfb->width, nsfb->height, nsfb->bpp);

    if (xstate->image == NULL) {
        fprintf(stderr, "Unable to create image\n");
        xcb_disconnect(xstate->connection);
        free(xstate);
        return -1;
    }

    /* ensure plotting information is stored */
    nsfb->surface_priv = xstate;
    nsfb->ptr = xstate->image->data;
    nsfb->linelen = xstate->image->stride;

    /* get keysymbol maps */
    xstate->keysymbols = xcb_key_symbols_alloc(xstate->connection);

    /* create window */
    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = xstate->screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE |
                XCB_EVENT_MASK_POINTER_MOTION;

    xstate->window = xcb_generate_id(xstate->connection);
    xcb_create_window (xstate->connection,
                       XCB_COPY_FROM_PARENT,
                       xstate->window,
                       xstate->screen->root,
                       0, 0, xstate->image->width, xstate->image->height, 1,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT,
                       xstate->screen->root_visual,
                       mask, values);
    static const char title[] = "NetSurf";
    static const char wm_class[] = "netsurf\0NetSurf";
    xcb_change_property(xstate->connection, XCB_PROP_MODE_REPLACE,
                        xstate->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                        8, sizeof(title) - 1, title);
    xcb_change_property(xstate->connection, XCB_PROP_MODE_REPLACE,
                        xstate->window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING,
                        8, sizeof(wm_class), wm_class);
    xcb_intern_atom_cookie_t protocols = xcb_intern_atom(xstate->connection,
                                                       0, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t deletion = xcb_intern_atom(xstate->connection,
                                                      0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xstate->connection,
                                                         protocols, NULL);
    if (reply != NULL) {
        xstate->wm_protocols = reply->atom;
        free(reply);
    }
    reply = xcb_intern_atom_reply(xstate->connection, deletion, NULL);
    if (reply != NULL) {
        xstate->wm_delete_window = reply->atom;
        free(reply);
    }
    if (xstate->wm_protocols != XCB_ATOM_NONE &&
        xstate->wm_delete_window != XCB_ATOM_NONE)
        xcb_change_property(xstate->connection, XCB_PROP_MODE_REPLACE,
                            xstate->window, xstate->wm_protocols, XCB_ATOM_ATOM,
                            32, 1, &xstate->wm_delete_window);

    /* set size hits on window */
    hints = xcb_alloc_size_hints();
    if (hints != NULL) {
        xcb_size_hints_set_max_size(hints,
                                    xstate->image->width,
                                    xstate->image->height);
        xcb_size_hints_set_min_size(hints,
                                    xstate->image->width,
                                    xstate->image->height);
        xcb_set_wm_size_hints(xstate->connection,
                              xstate->window,
                              WM_NORMAL_HINTS,
                              hints);
        xcb_free_size_hints(hints);
    }

    /* create backing pixmap */
    xstate->pmap = xcb_generate_id(xstate->connection);
    xcb_create_pixmap(xstate->connection, 24, xstate->pmap, xstate->window, xstate->image->width, xstate->image->height);

    /* create pixmap plot gc */
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
    values[0] = xstate->screen->black_pixel;
    values[1] = 0xffffff;

    xstate->gc = xcb_generate_id (xstate->connection);
    xcb_create_gc(xstate->connection, xstate->gc, xstate->pmap, mask, values);

    /*    if (nsfb->bpp == 8)
          set_palette(nsfb);
    */

    /* put the image into the pixmap */
    update_and_redraw_pixmap(xstate, 0, 0, xstate->image->width, xstate->image->height);


    /* show the window */
    xcb_map_window (xstate->connection, xstate->window);
    xcb_flush(xstate->connection);

    return 0;
}

static int x_finalise(nsfb_t *nsfb)
{
    xstate_t *xstate = nsfb->surface_priv;
    if (xstate == NULL)
        return 0;

    xcb_key_symbols_free(xstate->keysymbols);

    /* free pixmap */
    xcb_free_pixmap(xstate->connection, xstate->pmap);

    /* close connection to server */
    xcb_disconnect(xstate->connection);

    return 0;
}

static bool x_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    xcb_generic_event_t *e;
    xcb_expose_event_t *ee;
    xcb_motion_notify_event_t *emn;
    xcb_button_press_event_t *ebp;
    xcb_key_press_event_t *ekp;
    xcb_key_press_event_t *ekr;
    xstate_t *xstate = nsfb->surface_priv;

    if (xstate == NULL)
        return false;

    xcb_flush(xstate->connection);

    /* try and retrive an event immediately */
    e = xcb_poll_for_event(xstate->connection);

    if ((e == NULL) && (timeout != 0)) {
        if (timeout > 0) {
            int confd;
            fd_set rfds;
            struct timeval tv;
            int retval;

            confd = xcb_get_file_descriptor(xstate->connection);
            FD_ZERO(&rfds);
            FD_SET(confd, &rfds);

            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;

            retval = select(confd + 1, &rfds, NULL, NULL, &tv);
            if (retval < 0)
                return false;
            if (retval == 0) {
		    /* timeout, nothing happened */
		    event->type = NSFB_EVENT_CONTROL;
		    event->value.controlcode = NSFB_CONTROL_TIMEOUT;
		    return true;
            }
        }
        e = xcb_wait_for_event(xstate->connection);
    }

    if (e == NULL) {
        if (xcb_connection_has_error(xstate->connection) != 0) {
            /* connection closed quiting time */
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_QUIT;
            return true;
        } else {
            return false; /* no event */
        }
    }

    event->type = NSFB_EVENT_NONE;

    switch (e->response_type & 0x7f) {
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *message = (xcb_client_message_event_t *)e;
        if (message->type == xstate->wm_protocols && message->format == 32 &&
            message->data.data32[0] == xstate->wm_delete_window) {
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_QUIT;
        }
        break;
    }
    case XCB_EXPOSE:
        ee = (xcb_expose_event_t *)e;
        xcb_copy_area(xstate->connection,
                      xstate->pmap,
                      xstate->window,
                      xstate->gc,
                      ee->x, ee->y,
                      ee->x, ee->y,
                      ee->width, ee->height);
        xcb_flush (xstate->connection);
        break;

    case XCB_MOTION_NOTIFY:
        emn = (xcb_motion_notify_event_t *)e;
        event->type = NSFB_EVENT_MOVE_ABSOLUTE;
        event->value.vector.x = emn->event_x;
        event->value.vector.y = emn->event_y;
        event->value.vector.z = 0;
        break;


    case XCB_BUTTON_PRESS:
        ebp = (xcb_button_press_event_t *)e;
        event->type = NSFB_EVENT_KEY_DOWN;

        switch (ebp->detail) {

        case X_BUTTON_LEFT:
            event->value.keycode = NSFB_KEY_MOUSE_1;
            break;

        case X_BUTTON_MIDDLE:
            event->value.keycode = NSFB_KEY_MOUSE_2;
            break;

        case X_BUTTON_RIGHT:
            event->value.keycode = NSFB_KEY_MOUSE_3;
            break;

        case X_BUTTON_WHEELUP:
            event->value.keycode = NSFB_KEY_MOUSE_4;
            break;

        case X_BUTTON_WHEELDOWN:
            event->value.keycode = NSFB_KEY_MOUSE_5;
            break;
        }
        break;

    case XCB_BUTTON_RELEASE:
        ebp = (xcb_button_press_event_t *)e;
        event->type = NSFB_EVENT_KEY_UP;

        switch (ebp->detail) {

        case X_BUTTON_LEFT:
            event->value.keycode = NSFB_KEY_MOUSE_1;
            break;

        case X_BUTTON_MIDDLE:
            event->value.keycode = NSFB_KEY_MOUSE_2;
            break;

        case X_BUTTON_RIGHT:
            event->value.keycode = NSFB_KEY_MOUSE_3;
            break;

        case X_BUTTON_WHEELUP:
            event->value.keycode = NSFB_KEY_MOUSE_4;
            break;

        case X_BUTTON_WHEELDOWN:
            event->value.keycode = NSFB_KEY_MOUSE_5;
            break;
        }
        break;


    case XCB_KEY_PRESS:
        ekp = (xcb_key_press_event_t *)e;
        event->type = NSFB_EVENT_KEY_DOWN;
        event->value.keycode = xkeysym_to_nsfbkeycode(xcb_key_symbols_get_keysym(xstate->keysymbols, ekp->detail, 0));
        break;

    case XCB_KEY_RELEASE:
        ekr = (xcb_key_release_event_t *)e;
        event->type = NSFB_EVENT_KEY_UP;
        event->value.keycode = xkeysym_to_nsfbkeycode(xcb_key_symbols_get_keysym(xstate->keysymbols, ekr->detail, 0));
        break;

    }

    free(e);

    return true;
}

static int x_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    (void)nsfb;
    (void)box;
    return 0;
}

/* X owns pointer movement. Only a shape change sends a cursor request;
 * moving the mouse never uploads or redraws framebuffer pixels. */
static int
x_cursor(nsfb_t *nsfb, struct nsfb_cursor_s *cursor)
{
    xstate_t *xstate = nsfb->surface_priv;
    if (cursor == NULL || cursor->pixel == NULL || xstate == NULL)
        return false;
    if (xstate->cursor_pixels == cursor->pixel &&
        xstate->cursor_width == cursor->bmp_width &&
        xstate->cursor_height == cursor->bmp_height &&
        xstate->cursor_stride == cursor->bmp_stride &&
        xstate->cursor_hotspot_x == cursor->hotspot_x &&
        xstate->cursor_hotspot_y == cursor->hotspot_y)
        return true;
    if (cursor->bmp_width <= 0 || cursor->bmp_height <= 0 ||
        cursor->bmp_stride < cursor->bmp_width ||
        cursor->hotspot_x < 0 || cursor->hotspot_x >= cursor->bmp_width ||
        cursor->hotspot_y < 0 || cursor->hotspot_y >= cursor->bmp_height)
        return false;

    xcb_connection_t *conn = xstate->connection;
    xcb_image_t *source = xcb_image_create_native(conn, cursor->bmp_width,
        cursor->bmp_height, XCB_IMAGE_FORMAT_Z_PIXMAP, 1, NULL, 0, NULL);
    xcb_image_t *mask = xcb_image_create_native(conn, cursor->bmp_width,
        cursor->bmp_height, XCB_IMAGE_FORMAT_Z_PIXMAP, 1, NULL, 0, NULL);
    if (source == NULL || mask == NULL) {
        if (source != NULL) xcb_image_destroy(source);
        if (mask != NULL) xcb_image_destroy(mask);
        return false;
    }
    memset(source->data, 0, source->size);
    memset(mask->data, 0, mask->size);
    for (int y = 0; y < cursor->bmp_height; y++) {
        for (int x = 0; x < cursor->bmp_width; x++) {
            nsfb_colour_t pixel = cursor->pixel[y * cursor->bmp_stride + x];
            unsigned brightness = (pixel & 255) + ((pixel >> 8) & 255) +
                                  ((pixel >> 16) & 255);
            xcb_image_put_pixel(mask, x, y, (pixel >> 24) >= 128);
            xcb_image_put_pixel(source, x, y, brightness < 384);
        }
    }
    xcb_pixmap_t source_pix = xcb_generate_id(conn);
    xcb_pixmap_t mask_pix = xcb_generate_id(conn);
    xcb_gcontext_t gc = xcb_generate_id(conn);
    xcb_create_pixmap(conn, 1, source_pix, xstate->window,
                      cursor->bmp_width, cursor->bmp_height);
    xcb_create_pixmap(conn, 1, mask_pix, xstate->window,
                      cursor->bmp_width, cursor->bmp_height);
    xcb_create_gc(conn, gc, source_pix, 0, NULL);
    xcb_image_put(conn, source_pix, gc, source, 0, 0, 0);
    xcb_image_put(conn, mask_pix, gc, mask, 0, 0, 0);
    xcb_cursor_t native = xcb_generate_id(conn);
    xcb_create_cursor(conn, native, source_pix, mask_pix,
                      0, 0, 0, 65535, 65535, 65535,
                      cursor->hotspot_x, cursor->hotspot_y);
    xcb_change_window_attributes(conn, xstate->window, XCB_CW_CURSOR, &native);
    if (xstate->cursor != XCB_CURSOR_NONE)
        xcb_free_cursor(conn, xstate->cursor);
    xcb_free_gc(conn, gc);
    xcb_free_pixmap(conn, source_pix);
    xcb_free_pixmap(conn, mask_pix);
    xcb_image_destroy(source);
    xcb_image_destroy(mask);
    xstate->cursor = native;
    xstate->cursor_pixels = cursor->pixel;
    xstate->cursor_width = cursor->bmp_width;
    xstate->cursor_height = cursor->bmp_height;
    xstate->cursor_stride = cursor->bmp_stride;
    xstate->cursor_hotspot_x = cursor->hotspot_x;
    xstate->cursor_hotspot_y = cursor->hotspot_y;
    xcb_flush(conn);
    return true;
}

static int x_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    xstate_t *xstate = nsfb->surface_priv;
    return update_and_redraw_pixmap(xstate, box->x0, box->y0,
                                   box->x1 - box->x0, box->y1 - box->y0);
}

const nsfb_surface_rtns_t x_rtns = {
    .initialise = x_initialise,
    .finalise = x_finalise,
    .input = x_input,
    .claim = x_claim,
    .update = x_update,
    .cursor = x_cursor,
    .geometry = x_set_geometry,
};

NSFB_SURFACE_DEF(x, NSFB_SURFACE_X, &x_rtns)

/*
 * Local variables:
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */
