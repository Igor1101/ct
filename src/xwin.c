#include "xwin.h"
#include <cairo/cairo-ft.h>
#include <sys/time.h>
#include <assert.h>
#include <hb-ft.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

static xcb_visualtype_t *s_get_screen_visualtype(const xcb_screen_t *screen) {
    for (xcb_depth_iterator_t it = xcb_screen_allowed_depths_iterator(screen);
         it.rem;
         xcb_depth_next(&it)) {
        size_t len = xcb_depth_visuals_length(it.data);
        xcb_visualtype_t *vis = xcb_depth_visuals(it.data);

        for (size_t i = 0; i < len; ++i) {
            if (vis->visual_id == screen->root_visual) {
                return vis;
            }
        }
    }
    return NULL;
}

static uint64_t s_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int xwin_font_ctx_create(struct xwin_font_ctx *f) {
    FT_Error ft_error;

    if ((ft_error = FT_Init_FreeType(&f->f_ft_library))) {
        fprintf(stderr, "Failed to init freetype2\n");
        return -1;
    }

    if ((ft_error = FT_New_Face(f->f_ft_library, CT_FONT_PATH, 0, &f->f_ft_face))) {
        fprintf(stderr, "Failed to load font face\n");
        return -1;
    }

    if ((ft_error = FT_Set_Char_Size(f->f_ft_face, CT_FONT_SIZE * 64, CT_FONT_SIZE * 64, 0, 0))) {
        fprintf(stderr, "Failed to set font size\n");
        return -1;
    }

    if (!(f->f_hb_font = hb_ft_font_create(f->f_ft_face, NULL))) {
        fprintf(stderr, "Failed to create harfbuzz font\n");
        return -1;
    }

    if (!(f->f_hb_buffer = hb_buffer_create())) {
        fprintf(stderr, "Failed to create harfbuzz buffer\n");
        return -1;
    }

    if (!(f->f_cairo_face = cairo_ft_font_face_create_for_ft_face(f->f_ft_face, 0))) {
        fprintf(stderr, "Failed to create font face for cairo font\n");
        return -1;
    }

    assert(FT_IS_FIXED_WIDTH(f->f_ft_face));


    return 0;
}

void xwin_font_ctx_destroy(struct xwin_font_ctx *f) {
    hb_buffer_destroy(f->f_hb_buffer);
    hb_font_destroy(f->f_hb_font);

    FT_Done_Face(f->f_ft_face);
    FT_Done_FreeType(f->f_ft_library);
}

int xwin_input_ctx_create(struct xwin_input_ctx *i, struct xwin *w) {
    if (!(i->i_xim = XOpenIM(w->w_xdisplay, NULL, NULL, NULL))) {
        return -1;
    }

    XIMStyles *styles;
    XIMStyle xim_req_style;

    if (XGetIMValues(i->i_xim, XNQueryInputStyle, &styles, NULL)) {
        return -1;
    }

    for (int i = 0; i < styles->count_styles; ++i) {
        printf("STYLE\n");
    }



    if (!(i->i_xic = XCreateIC(i->i_xim,
                               XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                               XNClientWindow, w->w_id,
                               XNFocusWindow, w->w_id,
                               NULL))) {
        return -1;
    }

    return 0;
}

int xwin_create(struct xwin *w, const char *title, int width, int height) {
    // Setup IM modifiers
    const char *xmodifiers;
    if ((xmodifiers = getenv("XMODIFIERS")) && XSetLocaleModifiers(xmodifiers) == NULL) {
        return -1;
    }

    if (xwin_tbuf_create(&w->w_tbuf, 25, 80) != 0) {
        return -1;
    }

    if (xwin_font_ctx_create(&w->w_font) != 0) {
        return -1;
    }

    if (!(w->w_xdisplay = XOpenDisplay(NULL))) {
        return -1;
    }
    w->w_conn = XGetXCBConnection(w->w_xdisplay);

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
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
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

    if (xwin_input_ctx_create(&w->w_input, w) != 0) {
        return -1;
    }

    xcb_flush(w->w_conn);

    w->w_graph.g_visualtype = s_get_screen_visualtype(w->w_screen);
    w->w_graph.g_surface = cairo_xcb_surface_create(w->w_conn, w->w_id, w->w_graph.g_visualtype, 1, 1);

    // TODO: perform this in font init using FT. Somehow. This code sucks
    cairo_text_extents_t text_extents;
    cairo_t *cr = cairo_create(w->w_graph.g_surface);
    cairo_set_font_face(cr, w->w_font.f_cairo_face);
    cairo_set_font_size(cr, CT_FONT_SIZE);
    cairo_text_extents(cr, "A", &text_extents);
    cairo_destroy(cr);

    w->w_font.f_char_width = text_extents.width;

    w->w_closed = 0;

    return 0;
}

void xwin_destroy(struct xwin *w) {
    xcb_disconnect(w->w_conn);

    xwin_font_ctx_destroy(&w->w_font);
}

static void s_xwin_draw_text(struct xwin *w, cairo_t *cr, double x, double y, int j, cairo_glyph_t *cairo_glyphs) {
    struct xwin_font_ctx *f = &w->w_font;

    const wchar_t *text = w->w_tbuf.t_lines[j];
    size_t in_len = xwstrlen(text);

    hb_buffer_reset(f->f_hb_buffer);
    hb_buffer_add_utf32(f->f_hb_buffer, text, -1, 0, -1);
    hb_buffer_set_direction(f->f_hb_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(f->f_hb_buffer, HB_SCRIPT_LATIN);

    hb_shape(f->f_hb_font, f->f_hb_buffer, NULL, 0);

    int len = hb_buffer_get_length(f->f_hb_buffer);
    const hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(f->f_hb_buffer, &len);

    assert(len == in_len);

    for (int i = 0; i < len; ++i) {
        if (text[i] == ' ') {
            continue;
        }

        int attr = 0xFF - w->w_tbuf.t_vis_attrs[j][i] & 0xFF;
        if (w->w_tbuf.t_cx == i && w->w_tbuf.t_cy == j) {
            int o = attr & 0xFF;
            attr &= ~0xFF;
            attr |= 0xFF - o;
        }

        cairo_set_source_rgb(cr, attr / 255.0, attr / 255.0, attr / 255.0);

        hb_codepoint_t gid = glyph_info[i].codepoint;
        cairo_glyphs[i].index = gid;
        cairo_glyphs[i].x = x + i * f->f_char_width;
        cairo_glyphs[i].y = y;

        cairo_show_glyphs(cr, &cairo_glyphs[i], 1);
    }
}

static void s_xwin_paint(struct xwin *w, cairo_t *cr) {
    if (!w->w_width_chars || !w->w_height_chars) {
        return;
    }

    uint64_t t0, t1;
    const struct xwin_font_ctx *f = &w->w_font;

    cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate(w->w_width_chars);
    if (!cairo_glyphs) {
        return;
    }
    cairo_save(cr);
    cairo_set_font_face(cr, w->w_font.f_cairo_face);
    cairo_set_font_size(cr, CT_FONT_SIZE);

    t0 = s_millis();
    for (int i = 0; i < w->w_tbuf.t_rows; ++i) {
        if (!w->w_tbuf.t_lines[i]) {
            continue;
        }

        if (w->w_tbuf.t_dirty[i]) {
            for (int j = 0; j < w->w_tbuf.t_cols; ++j) {
                int attr = w->w_tbuf.t_vis_attrs[i][j];
                if (w->w_tbuf.t_cx == j && w->w_tbuf.t_cy == i) {
                    int o = attr & 0xFF;
                    attr &= ~0xFF;
                    attr |= 0xFF - o;
                }

                cairo_set_source_rgb(cr, (attr & 0xFF) / 255.0, (attr & 0xFF) / 255.0, (attr & 0xFF) / 255.0);
                cairo_rectangle(cr, CT_PAD_X + j * f->f_char_width, CT_PAD_Y + i * CT_FONT_SIZE, f->f_char_width, CT_FONT_SIZE);
                cairo_fill(cr);
            }

            s_xwin_draw_text(w, cr, CT_PAD_X, CT_PAD_Y + i * CT_FONT_SIZE + CT_FONT_SIZE, i, cairo_glyphs);
            w->w_tbuf.t_dirty[i] = 0;
        }
    }
    t1 = s_millis();
    cairo_restore(cr);
    cairo_glyph_free(cairo_glyphs);

    if (w->w_tbuf.t_cx >= 0
     && w->w_tbuf.t_cy >= 0
     && w->w_tbuf.t_cx < w->w_tbuf.t_cols
     && w->w_tbuf.t_cy < w->w_tbuf.t_rows
     && (!w->w_tbuf.t_lines[w->w_tbuf.t_cy] ||
         !w->w_tbuf.t_lines[w->w_tbuf.t_cy][w->w_tbuf.t_cx]
         )) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr,
                        CT_PAD_X + w->w_tbuf.t_cx * f->f_char_width,
                        CT_PAD_Y + w->w_tbuf.t_cy * CT_FONT_SIZE,
                        f->f_char_width,
                        CT_FONT_SIZE);
        cairo_fill(cr);
    }

    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_rectangle(cr, 0, 0, w->w_tbuf.t_cols * f->f_char_width, w->w_tbuf.t_rows * CT_FONT_SIZE);
    cairo_stroke(cr);

    printf("%d\n", t1 - t0);
}

void xwin_repaint(struct xwin *w) {
    cairo_t *cr = cairo_create(w->w_graph.g_surface);
    s_xwin_paint(w, cr);
    xcb_flush(w->w_conn);
    cairo_destroy(cr);
}

void xwin_event_configure_notify(struct xwin *w, const XConfigureEvent *e) {
    int res = 0;

    if (e->width && e->width != w->w_width) {
        w->w_width = e->width;
        w->w_width_chars = e->width / w->w_font.f_char_width;
        res = 1;
    }

    if (e->height && e->height != w->w_height) {
        w->w_height = e->height;
        w->w_height_chars = e->height / CT_FONT_SIZE;
        res = 1;
    }

    if (res) {
        cairo_xcb_surface_set_size(w->w_graph.g_surface, w->w_width, w->w_height);
        xwin_tbuf_resize(&w->w_tbuf, w->w_height_chars, w->w_width_chars);
    }
}

static wchar_t s_utf8_to_wchar(const char *u) {
    if (!u || !*u) {
        return 0;
    }

    if (!(u[0] & ~0x7F)) {
        return (wchar_t) u[0];
    }

    if (u[0] < 224) {
        return ((char) ((u[0] & ~0xE0)) << 6) | ((char) (u[1] & ~0xC0));
    }

    return -1;
}

static void xwin_event_key_type(struct xwin *w, wchar_t sym) {
    xwin_tbuf_putc(&w->w_tbuf, sym, 0);
}

static void xwin_event_key_press_gen(struct xwin *w, KeySym keysym) {
    switch (keysym) {
    case XK_BackSpace:
        // Send backspace to buffer
        xwin_tbuf_putc(&w->w_tbuf, 8, 0);
        break;
    case XK_Return:
        // Send return to buffer
        xwin_tbuf_putc(&w->w_tbuf, '\n', 0);
        break;
    case XK_Tab:
        // Tab
        xwin_tbuf_putc(&w->w_tbuf, '\t', 0);
        break;
    default:
        printf("Unhandled keypress: %04x\n", keysym);
        break;
    }
}

void xwin_event_key_press(struct xwin *w, XKeyPressedEvent *e) {
    char buf[16];
    int count = 0;
    Status status = 0;
    KeySym keysym;

    count = Xutf8LookupString(w->w_input.i_xic, e, buf, 16, &keysym, &status);

    if (status == XBufferOverflow) {
        printf("Buffer overflow\n");
        return;
    }

    if (count > 0) {
        if (count == 1 && !isprint(buf[0])) {
            return xwin_event_key_press_gen(w, keysym);
        }

        wchar_t sym = s_utf8_to_wchar(buf);
        xwin_event_key_type(w, sym);
    } else {
        xwin_event_key_press_gen(w, keysym);
    }
}

void xwin_poll_events(struct xwin *w) {
    XEvent event;

    while (XPending(w->w_xdisplay)) {
        XNextEvent(w->w_xdisplay, &event);

        if (XFilterEvent(&event, None)) {
            continue;
        }

        if (event.type == MappingNotify) {
            XRefreshKeyboardMapping(&event.xmapping);
            continue;
        }
        if (event.type == UnmapNotify) {
            w->w_closed = 1;
            return;
        }

        if (event.type == ConfigureNotify) {
            xwin_event_configure_notify(w, (XConfigureEvent *) &event);
            continue;
        }

        if (event.type == FocusIn || event.type == FocusOut) {
            XFocusChangeEvent *e = (XFocusChangeEvent *) &event;

            if (e->mode == NotifyGrab) {
                continue;
            }

            if (event.type == FocusIn) {
                XSetICFocus(w->w_input.i_xic);
            }

            continue;
        }

        if (event.type == Expose) {
            xwin_repaint(w);
            continue;
        }

        if (event.type == KeyPress) {
            xwin_event_key_press(w, (XKeyPressedEvent *) &event);
            xwin_repaint(w);
            continue;
        }
    }
}
