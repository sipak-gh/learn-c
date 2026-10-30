/* overflow.c - unsigned arithmetic wraps; signed overflow is undefined. */
#include <limits.h>
#include <stdio.h>

int main(void)
{
    unsigned int u = UINT_MAX;
    printf("UINT_MAX + 1 = %u\n", u + 1);       /* defined: wraps to 0 */
    printf("0u - 1       = %u\n", 0u - 1);      /* defined: wraps to UINT_MAX */

    int x = INT_MAX;
    int y = x + 1;                              /* undefined behaviour! */
    printf("y = %d\n", y);
    printf("x + 1 > x is %d\n", x + 1 > x);
    return 0;
}
