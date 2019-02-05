#include "xwin.h"
#include <assert.h>

int xwin_tbuf_create(struct xwin_tbuf *t, int rows, int cols) {
    t->t_rows = rows;
    t->t_cols = cols;
    t->t_lines = calloc(sizeof(wchar_t *), rows);
    t->t_dirty = calloc(sizeof(int), rows);
    t->t_vis_attrs = calloc(sizeof(int *), rows);
    t->t_cx = 0;
    t->t_cy = 0;

    return !t->t_lines;
}

void xwin_tbuf_scrollup(struct xwin_tbuf *t) {
    wchar_t * line0 = t->t_lines[0];
    int *vis0 = t->t_vis_attrs[0];

    for (int i = 0; i < t->t_rows - 1; ++i) {
        t->t_lines[i] = t->t_lines[i + 1];
        t->t_vis_attrs[i] = t->t_vis_attrs[i + 1];
    }
    t->t_lines[t->t_rows - 1] = NULL;
    t->t_vis_attrs[t->t_rows - 1] = NULL;

    xwin_tbuf_dirty_all(t);

    free(line0);
    free(vis0);
}

void xwin_tbuf_move(struct xwin_tbuf *t, int y, int x) {
    assert(x >= 0 && y >= 0 && x < t->t_cols && y < t->t_rows);
    t->t_cx = x;
    t->t_cy = y;
}

static inline void xwin_tbuf_set(struct xwin_tbuf *t, int y, int x, wchar_t c, int attr) {
    if (!t->t_lines[y]) {
        t->t_lines[y] = malloc((t->t_cols + 1) * sizeof(wchar_t));
        t->t_vis_attrs[y] = malloc(sizeof(int) * t->t_cols);
        // Pad with spaces
        memset(t->t_lines[y], ' ', x * sizeof(wchar_t));
        memset(t->t_lines[y] + x + 1, 0, (t->t_cols - x) * sizeof(wchar_t));
        memset(t->t_vis_attrs[y], 0, t->t_cols * sizeof(int));
        // Zero-terminate
    }

    t->t_vis_attrs[y][x] = attr;
    t->t_lines[y][x] = c;
    t->t_dirty[y] = 1;
}

void xwin_tbuf_putc(struct xwin_tbuf *t, wchar_t c, int attr) {
    if (c > '\n') {
        if (t->t_cx == t->t_cols - 2) {
            t->t_cx = 0;
            ++t->t_cy;
        }

        if (t->t_cy == t->t_rows) {
            xwin_tbuf_scrollup(t);
            --t->t_cy;
        }

        xwin_tbuf_set(t, t->t_cy, t->t_cx, c, attr);

        t->t_cx++;
    } else {
        switch (c) {
        case '\n':          // Line feed
            t->t_cx = 0;
            ++t->t_cy;
            break;
        case '\r':          // Carriage return
            t->t_cx = 0;
            break;
        case '\t':
            {
                int oldc = t->t_cx;
                int newc = oldc + (4 - (oldc + 4) % 4);
                if (newc > t->t_cols - 2) {
                    t->t_cx = 0;
                    ++t->t_cy;
                } else {
                    for (int i = oldc; i < newc; ++i) {
                        xwin_tbuf_putc(t, ' ', attr);
                    }
                }
            }
            break;
        case 8:             // Backspace
            if (t->t_cx) {
                if (t->t_lines[t->t_cy]) {
                    if (t->t_lines[t->t_cy][t->t_cx]) {
                        --t->t_cx;
                        /*t->t_lines[t->t_cy][--t->t_cx] = ' ';*/
                    } else {
                        --t->t_cx;
                        /*t->t_lines[t->t_cy][--t->t_cx] = 0;*/
                    }
                    t->t_dirty[t->t_cy] = 1;
                }
            } else {
                if (t->t_cy) {
                    --t->t_cy;
                    t->t_cx = xwstrlen(t->t_lines[t->t_cy]);
                    if (t->t_cx) {
                        /*t->t_lines[t->t_cy][--t->t_cx] = 0;*/
                        t->t_dirty[t->t_cy] = 1;
                    }
                }
            }
            break;
        default:
            xwin_tbuf_putc(t, '?', 0x00FF);
            break;
        }
    }
}

int xwin_tbuf_resize(struct xwin_tbuf *t, int r, int c) {
    t->t_lines = realloc(t->t_lines, sizeof(char *) * r);
    t->t_vis_attrs = realloc(t->t_vis_attrs, sizeof(int *) * r);
    t->t_dirty = realloc(t->t_dirty, sizeof(int) * r);
    for (int i = t->t_rows; i < r; ++i) {
        t->t_lines[i] = NULL;
        t->t_vis_attrs[i] = NULL;
        t->t_dirty[i] = 0;
    }
    t->t_rows = r;
    return !t->t_lines;
}

void xwin_tbuf_dirty_all(struct xwin_tbuf *t) {
    memset(t->t_dirty, 0xFF, t->t_rows * sizeof(int));
}

void xwin_tbuf_dirty(struct xwin_tbuf *t, int l) {
    t->t_dirty[l] = 1;
}

