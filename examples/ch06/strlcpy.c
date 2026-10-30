/* strlcpy.c - BSD strlcpy: in musl, and in glibc only since 2.38. */
#define _DEFAULT_SOURCE               /* musl declares strlcpy under this */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char nick[8];
    size_t n = strlcpy(nick, "a-very-long-nick", sizeof nick);
    printf("nick = \"%s\", strlcpy returned %zu (%s)\n", nick, n,
           n >= sizeof nick ? "truncated" : "fits");
    return 0;
}
