/* inline.c - small helpers the compiler may paste into the caller. */
#include <stdio.h>

/* 'static inline': private to this file, and a hint to inline it.
 * If the compiler doesn't inline a call, it emits a private copy. */
static inline int max_int(int a, int b)
{
    return a > b ? a : b;
}

int main(void)
{
    printf("max_int(3, 7) = %d\n", max_int(3, 7));
    return 0;
}
