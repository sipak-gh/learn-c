/* main.c - mywc: count lines, words and bytes in files, or in stdin. */
#include <stdio.h>

#include "count.h"

static void print_counts(const struct counts *c, const char *name)
{
    printf("%7ld %7ld %7ld%s%s\n", c->lines, c->words, c->bytes,
           name[0] != '\0' ? " " : "", name);
}

int main(int argc, char *argv[])
{
    struct counts total = { 0, 0, 0 };
    if (argc < 2) {
        count_stream(stdin, &total);
        print_counts(&total, "");
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (f == NULL) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        struct counts c = { 0, 0, 0 };
        count_stream(f, &c);
        fclose(f);
        print_counts(&c, argv[i]);
        count_add(&total, &c);
    }
    if (count_streams > 1)
        print_counts(&total, "total");
    return status;
}
