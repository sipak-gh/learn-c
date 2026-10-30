/* overlap.c - memcpy must not be used when source and destination overlap.
 * memmove handles overlap. Here: shift a string right to make room. */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    char a[64] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char b[64] = "0123456789abcdefghijklmnopqrstuvwxyz";
    size_t n = (size_t)argc + 29;    /* 30, computed at run time so gcc
                                        cannot see the overlap and warn */
    (void)argv;

    memmove(a + 1, a, n);            /* correct */
    memcpy(b + 1, b, n);             /* undefined behaviour: the ranges overlap */
    printf("memmove: %s\n", a);
    printf("memcpy:  %s\n", b);
    return 0;
}
