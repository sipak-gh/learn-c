/* voidptr.c - void * points to "some object of unknown type". */
#include <stdio.h>

/* Swap any two objects of the same size, one byte at a time. */
static void swap_any(void *a, void *b, size_t size)
{
    unsigned char *p = a;               /* void * converts without a cast */
    unsigned char *q = b;
    for (size_t i = 0; i < size; i++) {
        unsigned char tmp = p[i];
        p[i] = q[i];
        q[i] = tmp;
    }
}

int main(void)
{
    int i1 = 1, i2 = 2;
    swap_any(&i1, &i2, sizeof i1);
    printf("ints: %d %d\n", i1, i2);

    double d1 = 1.5, d2 = -0.25;
    swap_any(&d1, &d2, sizeof d1);
    printf("doubles: %.2f %.2f\n", d1, d2);

    void *v = &i1;
    int *ip = v;                        /* back to the real type */
    printf("through void *: %d\n", *ip);
    return 0;
}
