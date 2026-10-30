/* strbuf.c - a growable byte buffer. */
#include "strbuf.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sb_reserve(struct strbuf *sb, size_t extra)
{
    if (extra > SIZE_MAX - 1 - sb->len)
        return -1;                      /* len + extra + 1 would overflow */
    size_t need = sb->len + extra + 1;  /* + 1 for the '\0' */
    if (need <= sb->cap)
        return 0;

    size_t cap = sb->cap > 0 ? sb->cap : 64;
    while (cap < need)
        cap = cap <= SIZE_MAX / 2 ? cap * 2 : need;

    /* Never p = realloc(p, n): on failure the old block would leak. */
    char *data = realloc(sb->data, cap);
    if (data == NULL)
        return -1;
    sb->data = data;
    sb->cap = cap;
    return 0;
}

int sb_append(struct strbuf *sb, const void *bytes, size_t n)
{
    if (sb_reserve(sb, n) < 0)
        return -1;
    if (n > 0)
        memcpy(sb->data + sb->len, bytes, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

int sb_appendf(struct strbuf *sb, const char *fmt, ...)
{
    /* First pass: vsnprintf with size 0 only counts the bytes needed. A
     * va_list can be used once, so the second pass needs its own copy. */
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    int result = -1;
    if (n >= 0 && sb_reserve(sb, (size_t)n) == 0) {
        vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
        sb->len += (size_t)n;
        result = 0;
    }
    va_end(ap2);
    return result;
}

void sb_consume(struct strbuf *sb, size_t n)
{
    if (n >= sb->len) {
        sb->len = 0;
    } else {
        memmove(sb->data, sb->data + n, sb->len - n);   /* regions overlap */
        sb->len -= n;
    }
    if (sb->data != NULL)
        sb->data[sb->len] = '\0';
}

void sb_clear(struct strbuf *sb)
{
    sb_consume(sb, sb->len);
}

void sb_free(struct strbuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}
