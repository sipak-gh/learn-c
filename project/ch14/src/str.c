/* str.c - small, safe string helpers. */
#include "str.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

size_t str_copy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

void str_chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

bool str_ieq(const char *a, const char *b)
{
    /* <ctype.h> functions need an unsigned char value (or EOF). */
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

bool str_to_long(const char *s, long min, long max, long *out)
{
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || n < min || n > max)
        return false;
    *out = n;
    return true;
}
