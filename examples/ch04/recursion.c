/* recursion.c - a function that calls itself, and the call stack. */
#include <stdio.h>

static unsigned long long factorial(unsigned n)
{
    if (n <= 1)
        return 1;                       /* base case: stops the recursion */
    return n * factorial(n - 1);        /* recursive case: a smaller problem */
}

/* Each call gets its own 'n' and 'depth' in its own stack frame. */
static void count_down(int n, int depth)
{
    printf("%*senter count_down(%d)\n", depth * 2, "", n);
    if (n > 0)
        count_down(n - 1, depth + 1);
    printf("%*sleave count_down(%d)\n", depth * 2, "", n);
}

int main(void)
{
    for (unsigned i = 0; i <= 20; i += 5)
        printf("%2u! = %llu\n", i, factorial(i));
    count_down(2, 0);
    return 0;
}
