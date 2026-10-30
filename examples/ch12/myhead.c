/* myhead.c - print the first N lines of stdin (default 10), like head -n.
 * getline() reads lines of any length. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main(int argc, char *argv[])
{
    long want = argc > 1 ? strtol(argv[1], NULL, 10) : 10;
    char *line = NULL;                 /* getline allocates and grows it */
    size_t cap = 0;
    ssize_t len;
    long longest = 0;

    for (long i = 0; i < want && (len = getline(&line, &cap, stdin)) != -1; i++) {
        fwrite(line, 1, (size_t)len, stdout);   /* len counts '\0' bytes too */
        if (len > longest)
            longest = len;
    }
    if (ferror(stdin)) {
        perror("myhead");
        free(line);
        return 1;
    }
    fprintf(stderr, "longest line: %ld bytes, buffer: %zu bytes\n", longest, cap);
    free(line);
    return 0;
}
