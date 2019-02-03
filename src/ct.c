#include "xwin.h"
#include <unistd.h>

static struct xwin s_window;

int main() {
    if (xwin_create(&s_window, "Hello", 1024, 768) != 0) {
        return -1;
    }

    while (!s_window.w_closed) {
        xwin_poll_events(&s_window);
    }

    xwin_destroy(&s_window);

    return 0;
}
