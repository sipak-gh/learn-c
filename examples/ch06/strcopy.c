/* strcopy.c - safe copy and append that always terminate. */
#include <stdio.h>
#include <string.h>

/* Like BSD strlcpy: copy at most size - 1 bytes, always add '\0'.
 * Returns strlen(src); a result >= size means "truncated". */
static size_t str_copy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* Like BSD strlcat: append src to the string in dst.
 * Returns the length the result wanted; >= size means "truncated". */
static size_t str_append(char *dst, const char *src, size_t size)
{
    const char *end = memchr(dst, '\0', size);
    if (end == NULL)                  /* dst is not terminated: give up */
        return size + strlen(src);
    size_t used = (size_t)(end - dst);
    return used + str_copy(dst + used, src, size - used);
}

int main(void)
{
    char nick[8];
    size_t n = str_copy(nick, "bob", sizeof nick);
    printf("\"%s\" (%zu, %s)\n", nick, n, n >= sizeof nick ? "truncated" : "fits");
    n = str_copy(nick, "a-very-long-nick", sizeof nick);
    printf("\"%s\" (%zu, %s)\n", nick, n, n >= sizeof nick ? "truncated" : "fits");

    char msg[16] = "hello";
    n = str_append(msg, ", ", sizeof msg);
    n = str_append(msg, "world", sizeof msg);
    printf("\"%s\" (%zu, %s)\n", msg, n, n >= sizeof msg ? "truncated" : "fits");
    n = str_append(msg, " and everyone", sizeof msg);
    printf("\"%s\" (%zu, %s)\n", msg, n, n >= sizeof msg ? "truncated" : "fits");
    return 0;
}
