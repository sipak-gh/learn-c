/* str.h - small, safe string helpers. */
#ifndef PARLEY_STR_H
#define PARLEY_STR_H

#include <stdbool.h>
#include <stddef.h>

/* Copy src into dst[size], always NUL-terminated (if size > 0), like BSD
 * strlcpy. Returns strlen(src); a result >= size means it was truncated. */
size_t str_copy(char *dst, const char *src, size_t size);

/* Remove any trailing '\r' and '\n' characters. */
void str_chomp(char *s);

/* ASCII case-insensitive equality. */
bool str_ieq(const char *a, const char *b);

/* Parse a whole decimal number in [min, max] into *out. Returns false (and
 * leaves *out alone) if s is not such a number. */
bool str_to_long(const char *s, long min, long max, long *out);

#endif
