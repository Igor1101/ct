#pragma once
#include <xcb/xcb.h>

struct xwin {
    xcb_connection_t   *w_conn;
    xcb_window_t        w_id;
    const xcb_screen_t *w_screen;
    int                 w_width, w_height;
    int                 w_closed;
};

int xwin_create(struct xwin *w, const char *title, int width, int height);
void xwin_destroy(struct xwin *w);

void xwin_poll_events(struct xwin *w);
