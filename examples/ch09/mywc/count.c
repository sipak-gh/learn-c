/* count.c - counting lines, words and bytes. */
#include "count.h"

#include <stdbool.h>

int count_streams = 0;                  /* the one definition */

/* Private to this file: no other file can call it or clash with it. */
static bool is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

void count_stream(FILE *f, struct counts *c)
{
    bool in_word = false;
    int ch;
    while ((ch = getc(f)) != EOF) {
        c->bytes++;
        if (ch == '\n')
            c->lines++;
        if (is_space(ch)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            c->words++;
        }
    }
    count_streams++;
}

void count_add(struct counts *to, const struct counts *from)
{
    to->lines += from->lines;
    to->words += from->words;
    to->bytes += from->bytes;
}
