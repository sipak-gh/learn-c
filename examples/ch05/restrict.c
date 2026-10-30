/* restrict.c - a promise to the compiler: "these pointers don't overlap". */
#include <stdio.h>

/* With restrict, the compiler may assume that storing to *b cannot change
 * *a, so it can return 1 without reading *a again. */
static int set_both(int *restrict a, int *restrict b)
{
    *a = 1;
    *b = 2;
    return *a;
}

int main(void)
{
    int x = 0, y = 0;
    int r = set_both(&x, &y);           /* two different objects: OK */
    printf("set_both(&x, &y) = %d, x = %d, y = %d\n", r, x, y);
    return 0;
}
