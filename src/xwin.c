#include "xwin.h"
#include <cairo/cairo-ft.h>
#include <sys/time.h>
#include <assert.h>
#include <hb-ft.h>
#include <stdlib.h>
#include <stdio.h>

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

    if ((ft_error = FT_New_Face(f->f_ft_library, "/home/alnyan/.local/share/fonts/font.ttf", 0, &f->f_ft_face))) {
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

int xwin_tbuf_create(struct xwin_tbuf *t, int rows, int cols) {
    t->t_rows = rows;
    t->t_cols = cols;
    t->t_lines = malloc(sizeof(char *) * rows);
    t->t_dirty = malloc(sizeof(int) * rows);

    return !t->t_lines;
}

void xwin_tbuf_scrollup(struct xwin_tbuf *t) {
    char *line0 = t->t_lines[0];

    for (int i = 0; i < t->t_rows - 1; ++i) {
        t->t_lines[i] = t->t_lines[i + 1];
    }
    t->t_lines[t->t_rows - 1] = NULL;

    xwin_tbuf_dirty_all(t);

    free(line0);
}

void xwin_tbuf_mvaddstr(struct xwin_tbuf *t, int y, int x, const char *text) {
    assert(y >= 0 && y < t->t_rows && x >= 0);
    size_t len = strlen(text);
    assert(x + len < t->t_cols); // TODO: implement line wrapping

    if (!t->t_lines[y]) {
        t->t_lines[y] = malloc(t->t_cols + 1);
        // Pad with spaces before the line, like this:
        // ' ', ' ', ' ', 'T', 'e', 'x', 't'
        memset(t->t_lines[y], ' ', x);
        // Make sure we're null-terminated
        t->t_lines[y][t->t_cols] = 0;
    }

    strncpy(t->t_lines[y], text, len);
    t->t_dirty[y] = 1;
}

void xwin_tbuf_dirty_all(struct xwin_tbuf *t) {
    memset(t->t_dirty, 0xFF, t->t_rows * sizeof(int));
}

void xwin_tbuf_dirty(struct xwin_tbuf *t, int l) {
    t->t_dirty[l] = 1;
}

int xwin_create(struct xwin *w, const char *title, int width, int height) {
    if (xwin_tbuf_create(&w->w_tbuf, 25, 80) != 0) {
        return -1;
    }

    if (xwin_font_ctx_create(&w->w_font) != 0) {
        return -1;
    }

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
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS
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

static void s_xwin_draw_text(struct xwin *w, cairo_t *cr, double x, double y, const char *text, cairo_glyph_t *cairo_glyphs) {
    struct xwin_font_ctx *f = &w->w_font;

    hb_buffer_reset(f->f_hb_buffer);
    hb_buffer_add_utf8(f->f_hb_buffer, text, -1, 0, -1);
    hb_buffer_set_direction(f->f_hb_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(f->f_hb_buffer, HB_SCRIPT_LATIN);

    hb_shape(f->f_hb_font, f->f_hb_buffer, NULL, 0);

    int len = hb_buffer_get_length(f->f_hb_buffer);
    const hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(f->f_hb_buffer, &len);

    assert(len == strlen(text));

    for (int i = 0; i < len; ++i) {
        if (text[i] == ' ') {
            continue;
        }

        cairo_set_source_rgb(cr, 0, 1, 0);
        cairo_rectangle(cr, x + i * f->f_char_width, y - CT_FONT_SIZE, f->f_char_width, CT_FONT_SIZE);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1, (float) i / len, 0);

        hb_codepoint_t gid = glyph_info[i].codepoint;
        cairo_glyphs[i].index = gid;
        cairo_glyphs[i].x = x + i * f->f_char_width;
        cairo_glyphs[i].y = y;

        cairo_show_glyphs(cr, &cairo_glyphs[i], 1);
    }
}

static void s_xwin_paint(struct xwin *w, cairo_t *cr) {
    uint64_t t0, t1;
    const struct xwin_font_ctx *f = &w->w_font;

    cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate(w->w_width_chars);
    cairo_save(cr);
    cairo_set_font_face(cr, w->w_font.f_cairo_face);
    cairo_set_font_size(cr, CT_FONT_SIZE);

    t0 = s_millis();
    for (int i = 0; i < w->w_tbuf.t_rows; ++i) {
        if (!w->w_tbuf.t_lines[i]) {
            continue;
        }

        if (w->w_tbuf.t_dirty[i]) {
            s_xwin_draw_text(w, cr, CT_PAD_X, CT_PAD_Y + i * CT_FONT_SIZE + CT_FONT_SIZE, w->w_tbuf.t_lines[i], cairo_glyphs);
            w->w_tbuf.t_dirty[i] = 0;
        }
    }
    t1 = s_millis();
    cairo_restore(cr);
    cairo_glyph_free(cairo_glyphs);

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

void xwin_event_expose(struct xwin *w, const xcb_expose_event_t *e) {
    int r0 = e->x / w->w_font.f_char_width;
    int r1 = r0 + 1 + e->width / w->w_font.f_char_width;
    for (int i = r0; i < r1 && i < w->w_tbuf.t_rows; ++i) {
        xwin_tbuf_dirty(&w->w_tbuf, i);
    }

    xwin_repaint(w);
}

void xwin_event_configure_notify(struct xwin *w, const xcb_configure_notify_event_t *e) {
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
        xwin_tbuf_dirty_all(&w->w_tbuf);
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
        case XCB_KEY_PRESS:
            {
                static int counter = 0;
                static int y = 0;
                char buf[128];
                snprintf(buf, sizeof(buf), "Line %d", counter++);
                if (y == w->w_tbuf.t_rows) {
                    xwin_tbuf_scrollup(&w->w_tbuf);
                    --y;
                }
                xwin_tbuf_mvaddstr(&w->w_tbuf, y++, 0, buf);
            }
            xwin_repaint(w);
            break;
        case XCB_UNMAP_NOTIFY:
            w->w_closed = 1;
            free(event);
            return;
        default:
            printf("Event %d\n", event->response_type & ~0x80);
            break;
        }

        free(event);
    }
}
