/* errno-rules.c - errno is only meaningful right after a call FAILED. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    /* 1. A failing call sets errno. */
    FILE *f = fopen("no-such-file.txt", "r");
    if (f == NULL)
        printf("fopen failed: errno = %d (%s)\n", errno, strerror(errno));

    /* 2. A successful call may leave the old value in place. */
    char *p = malloc(16);
    printf("malloc %s, errno is still %d\n", p != NULL ? "worked" : "failed", errno);
    free(p);

    /* 3. So check the RETURN VALUE first, and look at errno only on failure.
     *    Functions that have no "failed" return value, like strtol, need
     *    errno = 0 before the call. */
    errno = 0;
    long n = strtol("99999999999999999999", NULL, 10);
    if (errno == ERANGE)
        printf("strtol: out of range, returned %ld\n", n);

    /* 4. Any library call may change errno, even printf. Save it first if
     *    you need it after other calls. */
    errno = 0;
    f = fopen("no-such-file.txt", "r");
    int saved = errno;
    printf("saved errno = %d\n", saved);
    return 0;
}
