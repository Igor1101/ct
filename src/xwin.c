#include "xwin.h"
#include <hb-ft.h>
#include <stdlib.h>
#include <stdio.h>

static xcb_render_pictformat_t s_get_visual_render_pictformat(xcb_render_query_pict_formats_reply_t *reply,
                                                              xcb_render_pictforminfo_t *formats,
                                                              xcb_visualid_t visual_id) {
    xcb_render_pictscreen_iterator_t screen_it;
    xcb_render_pictscreen_t         *cscreen;
    xcb_render_pictdepth_iterator_t  depth_it;
    xcb_render_pictdepth_t          *cdepth;
    xcb_render_pictvisual_iterator_t visual_it;
    xcb_render_pictvisual_t         *cvisual;

    for (screen_it = xcb_render_query_pict_formats_screens_iterator(reply);
         screen_it.rem;
         xcb_render_pictscreen_next(&screen_it)) {
        cscreen = screen_it.data;

        for (depth_it = xcb_render_pictscreen_depths_iterator(cscreen);
             depth_it.rem;
             xcb_render_pictdepth_next(&depth_it)) {
            cdepth = depth_it.data;

            for (visual_it = xcb_render_pictdepth_visuals_iterator(cdepth);
                 visual_it.rem;
                 xcb_render_pictvisual_next(&visual_it)) {
                cvisual = visual_it.data;

                if (cvisual->visual == visual_id) {
                    return cvisual->format;
                }
            }
        }
    }

    return 0;
}

static xcb_render_pictforminfo_t *s_get_pictforminfo(xcb_render_query_pict_formats_reply_t *reply,
                                                     xcb_render_pictforminfo_t *query) {
    for (xcb_render_pictforminfo_iterator_t it = xcb_render_query_pict_formats_formats_iterator(reply);
         it.rem;
         xcb_render_pictforminfo_next(&it)) {
        xcb_render_pictforminfo_t *cformat = it.data;

        if (query->type != cformat->type ||
            (query->depth != cformat->depth) ||
            (query->direct.red_mask && query->direct.red_mask != cformat->direct.red_mask) ||
            (query->direct.green_mask && query->direct.green_mask != cformat->direct.green_mask) ||
            (query->direct.blue_mask && query->direct.blue_mask != cformat->direct.blue_mask) ||
            (query->direct.alpha_mask && query->direct.alpha_mask != cformat->direct.alpha_mask)) {
            continue;
        }

        return cformat;
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

    if ((ft_error = FT_Set_Char_Size(f->f_ft_face, 0, 14 * 64, 0, 0))) {
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

    // Check XCB-render version
    xcb_render_query_version_cookie_t version_cookie;
    xcb_render_query_version_reply_t *version_reply;

    version_cookie = xcb_render_query_version(w->w_conn, XCB_RENDER_MAJOR_VERSION, XCB_RENDER_MINOR_VERSION);
    version_reply = xcb_render_query_version_reply(w->w_conn, version_cookie, 0);

    if (!version_reply) {
        return -1;
    }

    printf("XCB render version %d.%d\n", version_reply->major_version, version_reply->minor_version);

    free(version_reply);

    // w->w_render_pictformat
    xcb_render_query_pict_formats_cookie_t pictformats_cookie;
    xcb_render_query_pict_formats_reply_t *pictformats_reply;
    xcb_render_pictforminfo_t *pictformats;

    pictformats_cookie = xcb_render_query_pict_formats(w->w_conn);
    pictformats_reply = xcb_render_query_pict_formats_reply(w->w_conn, pictformats_cookie, NULL);

    if (!pictformats_reply) {
        return -1;
    }

    pictformats = xcb_render_query_pict_formats_formats(pictformats_reply);

    for (int i = 0; i < pictformats_reply->num_formats; ++i) {
        if (pictformats[i].type == XCB_RENDER_PICT_TYPE_DIRECT &&
            pictformats[i].depth == 32) {
            w->w_render_pictformat = pictformats[i].id;
            break;
        }
    }

    w->w_render_win_format = s_get_visual_render_pictformat(pictformats_reply, pictformats, w->w_screen->root_visual);

    // w->w_render_pictforminfo
    xcb_render_pictforminfo_t *alpha_forminfo_ptr, pictforminfo_query;
    xcb_render_pictformat_t    alpha_mask_format;

    pictforminfo_query.id = 0;
    pictforminfo_query.type = XCB_RENDER_PICT_TYPE_DIRECT;
    pictforminfo_query.depth = 8;
    pictforminfo_query.direct.red_mask = 0;
    pictforminfo_query.direct.green_mask = 0;
    pictforminfo_query.direct.blue_mask = 0;
    pictforminfo_query.direct.alpha_mask = 255;

    if (!(alpha_forminfo_ptr = s_get_pictforminfo(pictformats_reply, &pictforminfo_query))) {
        return -1;
    }

    w->w_render_alpha_format = alpha_forminfo_ptr->id;

    free(pictformats_reply);

    w->w_closed = 0;

    return 0;
}

void xwin_destroy(struct xwin *w) {
    xcb_disconnect(w->w_conn);

    xwin_font_ctx_destroy(&w->w_font);
}

void xwin_draw_text(struct xwin *w, const char *text, int x, int y) {
    struct xwin_font_ctx *f = &w->w_font;

    hb_buffer_reset(f->f_hb_buffer);
    hb_buffer_add_utf8(f->f_hb_buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(f->f_hb_buffer);

    hb_shape(f->f_hb_font, f->f_hb_buffer, NULL, 0);

    int len = hb_buffer_get_length(f->f_hb_buffer);
    const hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(f->f_hb_buffer, &len);
    const hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(f->f_hb_buffer, &len);

    for (int i = 0; i < len; ++i) {
        hb_codepoint_t gid = glyph_info[i].codepoint;

        char glyph_name[64];
        hb_font_get_glyph_name(f->f_hb_font, gid, glyph_name, sizeof(glyph_name));
        printf("Render \"%s\"\n", glyph_name);
    }
}

void xwin_paint_region(struct xwin *w, int r0, int c0, int r1, int c1) {

}

void xwin_repaint(struct xwin *w) {
    /*xwin_paint_region(w, 0, 0, w->w_width_chars, w->w_height_chars);*/
    xwin_draw_text(w, "A >>= B", 100, 100);
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
            free(event);
            return;
        default:
            printf("Event %d\n", event->response_type & ~0x80);
            break;
        }

        free(event);
    }
}
