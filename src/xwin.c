#include "xwin.h"
#include <hb-ft.h>
#include <cairo/cairo-ft.h>
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

    return 0;
}

void xwin_font_ctx_destroy(struct xwin_font_ctx *f) {
    hb_buffer_destroy(f->f_hb_buffer);
    hb_font_destroy(f->f_hb_font);

    FT_Done_Face(f->f_ft_face);
    FT_Done_FreeType(f->f_ft_library);
}

int xwin_create(struct xwin *w, const char *title, int width, int height) {
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

    w->w_graph.g_visualtype = s_get_screen_visualtype(w->w_screen);
    w->w_graph.g_surface = cairo_xcb_surface_create(w->w_conn, w->w_id, w->w_graph.g_visualtype, 1, 1);

    w->w_closed = 0;

    return 0;
}

void xwin_destroy(struct xwin *w) {
    xcb_disconnect(w->w_conn);

    xwin_font_ctx_destroy(&w->w_font);
}

void xwin_draw_text(struct xwin *w, cairo_t *cr, const char *text, int x, int y) {
    struct xwin_font_ctx *f = &w->w_font;

    hb_buffer_reset(f->f_hb_buffer);
    hb_buffer_add_utf8(f->f_hb_buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(f->f_hb_buffer);

    hb_shape(f->f_hb_font, f->f_hb_buffer, NULL, 0);

    int len = hb_buffer_get_length(f->f_hb_buffer);
    const hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(f->f_hb_buffer, &len);
    const hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(f->f_hb_buffer, &len);
    cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate(len);
    cairo_translate(cr, x, y);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_font_face(cr, w->w_font.f_cairo_face);
    cairo_set_font_size(cr, CT_FONT_SIZE);
    double px = 0;
    double py = 0;

    for (int i = 0; i < len; ++i) {
        hb_codepoint_t gid = glyph_info[i].codepoint;

        char glyph_name[64];
        hb_font_get_glyph_name(f->f_hb_font, gid, glyph_name, sizeof(glyph_name));
        printf("Render \"%s\"\n", glyph_name);

        cairo_glyphs[i].index = gid;
        cairo_glyphs[i].x = px + glyph_pos[i].x_offset / 64.0;
        cairo_glyphs[i].y = py + glyph_pos[i].y_offset / 64.0;
        px += glyph_pos[i].x_advance / 64.0;
        py += glyph_pos[i].y_advance / 64.0;
    }

    cairo_show_glyphs(cr, cairo_glyphs, len);
    cairo_glyph_free(cairo_glyphs);
}

void xwin_paint_region(struct xwin *w, int r0, int c0, int r1, int c1) {

}

void xwin_repaint(struct xwin *w) {
    /*xwin_paint_region(w, 0, 0, w->w_width_chars, w->w_height_chars);*/
    cairo_t *cr = cairo_create(w->w_graph.g_surface);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    xwin_draw_text(w, cr, "A >>= B", 100, 100);
    cairo_destroy(cr);
    xcb_flush(w->w_conn);
}

void xwin_event_expose(struct xwin *w, const xcb_expose_event_t *e) {
    xwin_repaint(w);
    /*xwin_paint_region(w,*/
            /*e->x / CT_CHAR_WIDTH,*/
            /*e->y / CT_CHAR_HEIGHT,*/
            /*(e->x + e->width) / CT_CHAR_WIDTH,*/
            /*(e->y + e->height) / CT_CHAR_HEIGHT);*/
}

void xwin_event_configure_notify(struct xwin *w, const xcb_configure_notify_event_t *e) {
    int res = 0;

    if (e->width && e->width != w->w_width) {
        w->w_width = e->width;
        /*w->w_width_chars = e->width / CT_CHAR_WIDTH;*/
        res = 1;
    }

    if (e->height && e->height != w->w_height) {
        w->w_height = e->height;
        /*w->w_height_chars = e->height / CT_CHAR_HEIGHT;*/
        res = 1;
    }

    if (res) {
        cairo_xcb_surface_set_size(w->w_graph.g_surface, w->w_width, w->w_height);
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
            free(event);
            return;
        default:
            printf("Event %d\n", event->response_type & ~0x80);
            break;
        }

        free(event);
    }
}
