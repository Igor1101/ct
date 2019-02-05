#include "wstr.h"

size_t xwstrlen(const wchar_t *s) {
    size_t i = 0;
    while (*s++) {
        ++i;
    }
    return i;
}
