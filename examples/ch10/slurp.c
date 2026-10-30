/* slurp.c - read a whole file into a heap buffer; one cleanup path. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read all of path into a new NUL-terminated buffer. On success return 0
 * and set *out (the caller frees it) and *len. On failure return -1 with
 * errno set. */
static int slurp(const char *path, char **out, size_t *len)
{
    int result = -1;
    char *buf = NULL;
    size_t used = 0, cap = 0;

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;                        /* nothing to clean up yet */

    for (;;) {
        if (cap - used < 4096) {
            size_t ncap = cap == 0 ? 4096 : cap * 2;
            char *nbuf = realloc(buf, ncap + 1);    /* +1 for the '\0' */
            if (nbuf == NULL)
                goto cleanup;             /* buf is still valid: freed below */
            buf = nbuf;
            cap = ncap;
        }
        size_t n = fread(buf + used, 1, cap - used, f);
        used += n;
        if (n == 0)
            break;
    }
    if (ferror(f))
        goto cleanup;                     /* fread set errno */
    buf[used] = '\0';
    *out = buf;
    *len = used;
    buf = NULL;                           /* now owned by the caller */
    result = 0;

cleanup:
    {
        int saved = errno;                /* fclose/free must not hide it */
        free(buf);
        fclose(f);
        errno = saved;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s FILE\n", argv[0]);
        return 2;
    }
    char *text;
    size_t len;
    if (slurp(argv[1], &text, &len) < 0) {
        fprintf(stderr, "slurp: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    size_t lines = 0;
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n')
            lines++;
    printf("%s: %zu bytes, %zu lines\n", argv[1], len, lines);
    free(text);
    return 0;
}
