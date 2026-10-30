/* mywc.c - print the lines, words and bytes on standard input. */
#include <stdio.h>

#include "wc.h"

int main(void)
{
    char buf[4096];
    size_t n;
    struct wc w;

    wc_init(&w);
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
        wc_feed(&w, buf, n);
    if (ferror(stdin)) {
        perror("mywc: stdin");
        return 1;
    }
    printf("%ld %ld %ld\n", w.lines, w.words, w.bytes);
    return 0;
}
