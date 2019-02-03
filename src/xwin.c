#include "xwin.h"
#include <stdlib.h>
#include <stdio.h>

int xwin_create(struct xwin *w, const char *title, int width, int height) {
    w->w_conn = xcb_connect(NULL, NULL);

    if (!w->w_conn) {
        return -1;
    }

    const xcb_setup_t *setup = xcb_get_setup(w->w_conn);
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);

    for (; screens.rem; xcb_screen_next(&screens)) {
        w->w_screen = screens.data;
        break;
    }

    if (!w->w_screen) {
        xcb_disconnect(w->w_conn);
        w->w_conn = NULL;
        return -1;
    }

    w->w_width = width;
    w->w_height = height;

    w->w_id = xcb_generate_id(w->w_conn);

    uint32_t window_hint_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const uint32_t window_hints[] = {
        w->w_screen->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };

    xcb_create_window(w->w_conn,
                      w->w_screen->root_depth,
                      w->w_id,
                      w->w_screen->root,
                      0,
                      0,
                      width,
                      height,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      w->w_screen->root_visual,
                      window_hint_mask,
                      window_hints);

    xcb_map_window(w->w_conn, w->w_id);

    xcb_flush(w->w_conn);

    w->w_closed = 0;

    return 0;
}

void xwin_destroy(struct xwin *w) {
    xcb_disconnect(w->w_conn);
}

void xwin_paint_region(struct xwin *w, int r0, int c0, int r1, int c1) {

}

void xwin_repaint(struct xwin *w) {
    xwin_paint_region(w, 0, 0, w->w_width_chars, w->w_height_chars);
}

void xwin_event_expose(struct xwin *w, const xcb_expose_event_t *e) {
    xwin_paint_region(w,
            e->x / CT_CHAR_WIDTH,
            e->y / CT_CHAR_HEIGHT,
            (e->x + e->width) / CT_CHAR_WIDTH,
            (e->y + e->height) / CT_CHAR_HEIGHT);
}

void xwin_event_configure_notify(struct xwin *w, const xcb_configure_notify_event_t *e) {
    int res = 0;

    if (e->width && e->width != w->w_width) {
        w->w_width = e->width;
        w->w_width_chars = e->width / CT_CHAR_WIDTH;
        res = 1;
    }

    if (e->height && e->height != w->w_height) {
        w->w_height = e->height;
        w->w_height_chars = e->height / CT_CHAR_HEIGHT;
        res = 1;
    }

    if (res) {
        // TODO: resize display buffers here
    }
}

void xwin_poll_events(struct xwin *w) {
    xcb_generic_event_t *event;

    while ((event = xcb_wait_for_event(w->w_conn))) {

        switch (event->response_type & ~0x80) {
        case XCB_EXPOSE:
            xwin_event_expose(w, (xcb_expose_event_t *) event);
            break;
        case XCB_CONFIGURE_NOTIFY:
            xwin_event_configure_notify(w, (xcb_configure_notify_event_t *) event);
            break;
        case XCB_UNMAP_NOTIFY:
            w->w_closed = 1;
            return;
        default:
            printf("Event %d\n", event->response_type & ~0x80);
            break;
        }

        free(event);
    }
}
