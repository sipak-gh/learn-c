/* strbuf.h - a growable byte buffer.
 *
 * A strbuf owns data, a heap block of cap bytes holding len bytes of
 * content. data[len] is always '\0' once data is allocated, so text
 * content can be used as a C string. A zeroed strbuf is a valid empty
 * buffer:
 *     struct strbuf sb = { 0 };
 *     sb_append(&sb, "hi", 2);
 *     sb_free(&sb);
 */
#ifndef PARLEY_STRBUF_H
#define PARLEY_STRBUF_H

#include <stddef.h>

struct strbuf {
    char *data;
    size_t len;
    size_t cap;
};

/* Make room for extra more bytes. Returns 0, or -1 if the size would
 * overflow or memory is exhausted (the buffer is then unchanged). */
int sb_reserve(struct strbuf *sb, size_t extra);

/* Append n bytes (they may include '\0' bytes). Returns 0 or -1. */
int sb_append(struct strbuf *sb, const void *bytes, size_t n);

/* Append printf-style formatted text. Returns 0 or -1. */
int sb_appendf(struct strbuf *sb, const char *fmt, ...);

/* Drop the first n bytes (all if n >= len); the rest moves to the front. */
void sb_consume(struct strbuf *sb, size_t n);

/* Empty the buffer but keep its memory for reuse. */
void sb_clear(struct strbuf *sb);

/* Free the memory; the strbuf is empty (and reusable) afterwards. */
void sb_free(struct strbuf *sb);

#endif
