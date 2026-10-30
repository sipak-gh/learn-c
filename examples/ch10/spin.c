/* spin.c - a loop that never ends, for Ctrl-C in gdb. */
#include <stdio.h>

static size_t count_spaces(const char *s)
{
    size_t n = 0;

    while (*s != '\0') {
        if (*s == ' ') {
            n++;
            s++;                      /* BUG: s only moves on at a space */
        }
    }
    return n;
}

int main(void)
{
    printf("%zu\n", count_spaces(" JOIN #c"));
    printf("%zu\n", count_spaces("PRIVMSG #c :hi there"));
    return 0;
}
