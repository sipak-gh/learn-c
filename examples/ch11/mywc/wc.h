/* wc.h - count lines, words and bytes, like wc(1). */
#ifndef WC_H
#define WC_H

#include <stdbool.h>
#include <stddef.h>

struct wc {
    long lines, words, bytes;
    bool in_word;        /* was the last byte part of a word? */
};

/* Start counting from zero. */
void wc_init(struct wc *w);

/* Count len more bytes. The input may arrive in pieces of any size: the
 * counts after all the pieces are the same as for one big piece. */
void wc_feed(struct wc *w, const char *buf, size_t len);

#endif
