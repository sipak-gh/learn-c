/* search.c - goto: leave two nested loops at once. */
#include <stdio.h>

int main(void)
{
    /* Find two numbers from 1..20 whose sum is 13 and product is 42. */
    for (int a = 1; a <= 20; a++) {
        for (int b = a; b <= 20; b++) {
            if (a + b == 13 && a * b == 42) {
                printf("found %d and %d\n", a, b);
                goto done;               /* break would leave only the inner loop */
            }
        }
    }
    printf("not found\n");
done:
    printf("search finished\n");
    return 0;
}
