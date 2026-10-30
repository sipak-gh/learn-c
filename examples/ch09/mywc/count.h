/* count.h - counting lines, words and bytes: the interface of count.c. */
#ifndef MYWC_COUNT_H
#define MYWC_COUNT_H

#include <stdio.h>

struct counts {
    long lines;
    long words;
    long bytes;
};

/* Read f to the end and add what it contains to *c. */
void count_stream(FILE *f, struct counts *c);

/* Add the counts in *from to *to. */
void count_add(struct counts *to, const struct counts *from);

/* How many streams count_stream has read. Defined in count.c. */
extern int count_streams;

#endif
