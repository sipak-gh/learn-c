/* sum.c - read two integers with scanf and always check what it matched. */
#include <stdio.h>

int main(void)
{
    int a, b;

    /* scanf needs the address of each variable (&a): Chapter 5 explains why. */
    int matched = scanf("%d %d", &a, &b);
    printf("scanf matched %d\n", matched);
    if (matched != 2) {                          /* if: Chapter 3 */
        fprintf(stderr, "sum: expected two integers\n");
        return 1;
    }
    printf("%d + %d = %d\n", a, b, a + b);
    return 0;
}
