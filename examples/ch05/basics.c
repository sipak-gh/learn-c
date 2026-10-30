/* basics.c - addresses, & and *, pointer types, NULL. */
#include <stdio.h>

int main(void)
{
    int n = 42;
    int *p = &n;                    /* p holds the address of n */

    printf("n = %d, *p = %d\n", n, *p);
    *p = 7;                         /* write through the pointer */
    printf("after *p = 7: n = %d\n", n);

    double d = 2.5;
    double *q = &d;                 /* a pointer type names what it points to */
    *q *= 2;
    printf("d = %.1f\n", d);

    printf("sizeof n = %zu, sizeof d = %zu\n", sizeof n, sizeof d);
    printf("sizeof p = %zu, sizeof q = %zu\n", sizeof p, sizeof q);

    int *none = NULL;               /* points to nothing */
    if (none == NULL)
        printf("none is a null pointer\n");

    printf("&n   = %p\n", (void *)&n);
    printf("NULL = %p\n", (void *)none);
    return 0;
}
