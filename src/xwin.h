#pragma once
#include <xcb/xcb.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <wchar.h>
#include "wstr.h"

#define CT_FONT_SIZE 16
#define CT_PAD_X     2
#define CT_PAD_Y     2
#define CT_CURSOR    1

struct xwin_font_ctx {
    FT_Library          f_ft_library;
    FT_Face             f_ft_face;
    hb_font_t          *f_hb_font;
    hb_buffer_t        *f_hb_buffer;
    cairo_font_face_t  *f_cairo_face;
    double              f_char_width;
};

struct xwin_graph_ctx {
    cairo_surface_t    *g_surface;
    xcb_visualtype_t   *g_visualtype;
};

struct xwin_input_ctx {
    XIM                 i_xim;
    XIC                 i_xic;
};

struct xwin_tbuf {
    wchar_t * *t_lines;
    int **t_vis_attrs;
    int *t_dirty;
    int t_rows, t_cols;
    int t_cx, t_cy;
};

struct xwin {
    Display                    *w_xdisplay;
    xcb_connection_t           *w_conn;
    xcb_window_t                w_id;
    const xcb_screen_t         *w_screen;
    int                         w_width, w_height;
    int                         w_closed;
    int                         w_width_chars, w_height_chars;
    struct xwin_font_ctx        w_font;
    struct xwin_graph_ctx       w_graph;
    struct xwin_tbuf            w_tbuf;
    struct xwin_input_ctx       w_input;
};

int xwin_font_ctx_create(struct xwin_font_ctx *f);
void xwin_font_ctx_destroy(struct xwin_font_ctx *f);
int xwin_font_ctx_load_glyph(struct xwin_font_ctx *f);

int xwin_tbuf_create(struct xwin_tbuf *t, int rows, int cols);
int xwin_tbuf_resize(struct xwin_tbuf *t, int rows, int cols);
void xwin_tbuf_dirty(struct xwin_tbuf *t, int row);
void xwin_tbuf_dirty_all(struct xwin_tbuf *t);
void xwin_tbuf_scrollup(struct xwin_tbuf *t);
void xwin_tbuf_move(struct xwin_tbuf *t, int t_cy, int t_cx);
void xwin_tbuf_putc(struct xwin_tbuf *t, wchar_t c, int a);

int xwin_input_ctx_create(struct xwin_input_ctx *i, struct xwin *w);

int xwin_create(struct xwin *w, const char *title, int width, int height);
void xwin_destroy(struct xwin *w);

void xwin_draw_text(struct xwin *w, cairo_t *cr, const wchar_t * text);
void xwin_paint_region(struct xwin *w, int r0, int c0, int r1, int c1);
void xwin_repaint(struct xwin *w);

void xwin_poll_events(struct xwin *w);
void xwin_event_configure_notify(struct xwin *w, const XConfigureEvent *e);
void xwin_event_key_press(struct xwin *w, XKeyPressedEvent *e);
