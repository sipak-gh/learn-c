/* comma.c - the comma operator in a for loop: two variables, two steps. */
#include <stdio.h>

int main(void)
{
    /* Print 1..9 from both ends towards the middle. */
    for (int lo = 1, hi = 9; lo < hi; lo++, hi--)
        printf("%d-%d ", lo, hi);
    printf("\n");

    int a, b;
    a = (b = 3, b * 2);                  /* evaluate b = 3, then b * 2 */
    printf("a = %d, b = %d\n", a, b);
    return 0;
}
