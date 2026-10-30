/* wc.c - count lines, words and bytes. */
#include "wc.h"

#include <ctype.h>

void wc_init(struct wc *w)
{
    w->lines = w->words = w->bytes = 0;
    w->in_word = false;
}

void wc_feed(struct wc *w, const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        w->bytes++;
        if (c == '\n')
            w->lines++;
        if (isspace(c)) {
            w->in_word = false;
        } else if (!w->in_word) {
            w->in_word = true;       /* the first byte of a new word */
            w->words++;
        }
    }
}
