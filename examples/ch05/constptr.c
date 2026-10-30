/* constptr.c - the four combinations of const and pointers. */
#include <stdio.h>

int main(void)
{
    int a = 1, b = 2;

    int *p1 = &a;                   /* change the pointer and the int */
    *p1 = 10;
    p1 = &b;

    const int *p2 = &a;             /* read-only int: *p2 = 3 is an error */
    p2 = &b;                        /* ...but p2 itself can move */

    int *const p3 = &a;             /* read-only pointer: p3 = &b is an error */
    *p3 = 20;                       /* ...but the int can change */

    const int *const p4 = &a;       /* neither can change */

    printf("a = %d, b = %d, *p1 = %d, *p2 = %d, *p3 = %d, *p4 = %d\n",
           a, b, *p1, *p2, *p3, *p4);
    return 0;
}
