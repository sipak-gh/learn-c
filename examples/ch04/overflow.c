/* overflow.c - recursion without a base case runs out of stack. */
#include <stdio.h>

static void dive(long depth)
{
    char frame[64];                     /* make each stack frame bigger */
    frame[0] = (char)depth;
    if (depth % 20000 == 0)
        fprintf(stderr, "depth %ld\n", depth);  /* stderr: printed at once */
    dive(depth + 1);                    /* no base case: never stops */
    (void)frame;
}

int main(void)
{
    dive(1);
    return 0;
}
