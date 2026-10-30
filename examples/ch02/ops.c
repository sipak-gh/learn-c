/* ops.c - arithmetic, increment, assignment, logical and conditional operators. */
#include <stdio.h>

int main(void)
{
    int i = 5;
    int a = i++;                     /* a gets the old value: 5; i is now 6 */
    int b = ++i;                     /* i becomes 7 first; b gets 7 */
    printf("a %d, b %d, i %d\n", a, b, i);

    i += 3;                          /* i = i + 3 -> 10 */
    i *= 2;                          /* 20 */
    i %= 7;                          /* 6 */
    printf("i %d\n", i);

    int zero = 0;
    /* && stops at the first false operand, so 10 / zero never runs. */
    printf("zero != 0 && 10 / zero > 1 is %d\n", zero != 0 && 10 / zero > 1);
    printf("zero == 0 || 10 / zero > 1 is %d\n", zero == 0 || 10 / zero > 1);

    int bigger = a > b ? a : b;      /* condition ? if-true : if-false */
    printf("bigger %d, %s\n", bigger, i % 2 == 0 ? "i is even" : "i is odd");
    return 0;
}
