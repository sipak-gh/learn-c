/* convert.c - integer promotions and the usual arithmetic conversions. */
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t a = 200, b = 100;
    uint8_t c = a + b;                 /* a + b is an int (300); c keeps 300 % 256 */
    printf("a + b = %d, sizeof(a + b) = %zu, c = %d\n", a + b, sizeof(a + b), c);

    int minus = -1;
    unsigned int one = 1;
    printf("-1 < 1u is %d\n", minus < one);          /* the classic trap */
    printf("(unsigned)-1 = %u\n", (unsigned)minus);

    printf("7 / 2 = %d, -7 / 2 = %d, 7 %% 2 = %d, -7 %% 2 = %d\n", 7 / 2, -7 / 2, 7 % 2, -7 % 2);
    printf("7 / 2.0 = %f, (double)7 / 2 = %f\n", 7 / 2.0, (double)7 / 2);
    printf("(int)3.99 = %d, (int)-3.99 = %d\n", (int)3.99, (int)-3.99);

    long big = 3000000000L;
    int narrow = (int)big;             /* out of int's range: implementation-defined */
    printf("(int)3000000000L = %d\n", narrow);
    return 0;
}
