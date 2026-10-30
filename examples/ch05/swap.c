/* swap.c - pointer parameters let a function change the caller's variables. */
#include <stdio.h>

static void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Two results: the quotient and remainder go out through pointers. */
static void divide(int num, int den, int *quot, int *rem)
{
    *quot = num / den;
    *rem = num % den;
}

int main(void)
{
    int x = 1, y = 2;
    swap(&x, &y);
    printf("x = %d, y = %d\n", x, y);

    int q, r;
    divide(17, 5, &q, &r);
    printf("17 / 5 = %d remainder %d\n", q, r);
    return 0;
}
