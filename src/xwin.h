#pragma once
#include <xcb/xcb.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define CT_CHAR_WIDTH   8
#define CT_CHAR_HEIGHT  12

struct xwin {
    xcb_connection_t   *w_conn;
    xcb_window_t        w_id;
    const xcb_screen_t *w_screen;
    int                 w_width, w_height;
    int                 w_closed;
    int                 w_width_chars, w_height_chars;
};

int xwin_create(struct xwin *w, const char *title, int width, int height);
void xwin_destroy(struct xwin *w);

void xwin_paint_region(struct xwin *w, int r0, int c0, int r1, int c1);
void xwin_repaint(struct xwin *w);

void xwin_poll_events(struct xwin *w);
void xwin_event_configure_notify(struct xwin *w, const xcb_configure_notify_event_t *e);
void xwin_event_expose(struct xwin *w, const xcb_expose_event_t *e);
