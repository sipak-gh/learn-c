/* mywc.c - count lines, words and bytes on standard input, like wc. */
#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    long lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    int c;                               /* int, not char: EOF is not a char */

    while ((c = getchar()) != EOF) {
        bytes++;
        switch (c) {
        case '\n':
            lines++;                     /* a newline also ends a word */
            /* fall through */
        case ' ':
        case '\t':
        case '\r':
        case '\v':
        case '\f':
            in_word = false;
            break;
        default:
            if (!in_word) {
                in_word = true;
                words++;
            }
            break;
        }
    }
    printf("%7ld %7ld %7ld\n", lines, words, bytes);
    return 0;
}
