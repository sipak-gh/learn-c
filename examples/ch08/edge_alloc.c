/* edge_alloc.c - malloc(0), realloc(p, 0) and a failed malloc.
 * C99 lets each libc choose what the first two do. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    void *a = malloc(0);
    printf("malloc(0)        -> %s\n", a != NULL ? "a pointer (must be freed)" : "NULL");
    free(a);

    void *p = malloc(16);
    void *r = realloc(p, 0);         /* p is invalid after this, either way */
    printf("realloc(p, 0)    -> %s\n", r != NULL ? "a new pointer (must be freed)" : "NULL");
    free(r);

    volatile size_t huge = SIZE_MAX; /* volatile: stop gcc warning at compile time */
    errno = 0;
    void *big = malloc(huge);
    printf("malloc(SIZE_MAX) -> %s, errno = %s\n",
           big != NULL ? "memory?!" : "NULL", strerror(errno));
    free(big);
    return 0;
}
