/* overflow.c - signed overflow is undefined, so the optimiser may assume
 * it never happens. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* "Will x + 1 overflow?" asked the wrong way. */
static int bigger(int x)
{
    int y = x + 1;
    return y > x;
}

/* The right way: compare before doing the arithmetic. */
static int bigger_ok(int x)
{
    return x < INT_MAX;
}

int main(int argc, char **argv)
{
    int x = argc > 1 ? atoi(argv[1]) : INT_MAX;

    printf("x = %d\n", x);
    printf("bigger(x)    = %d\n", bigger(x));
    printf("bigger_ok(x) = %d\n", bigger_ok(x));
    return 0;
}
