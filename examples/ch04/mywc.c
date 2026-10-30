/* mywc.c - count lines, words and bytes on stdin, like wc. */
#include <stdbool.h>
#include <stdio.h>

/* File-scope counters. 'static' keeps them private to this file. */
static long lines, words, bytes;

static bool is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/* Read stdin to the end, updating the counters. */
static void count_input(void)
{
    bool in_word = false;
    int c;                              /* int, not char: EOF is not a char */
    while ((c = getchar()) != EOF) {
        bytes++;
        if (c == '\n')
            lines++;
        if (is_space(c)) {
            in_word = false;
        } else if (!in_word) {          /* first letter of a new word */
            in_word = true;
            words++;
        }
    }
}

static void print_counts(void)
{
    printf("%7ld %7ld %7ld\n", lines, words, bytes);
}

int main(void)
{
    count_input();
    print_counts();
    return 0;
}
