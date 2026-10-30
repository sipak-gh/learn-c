/* precedence.c - expressions that surprise because of precedence. */
#include <stdio.h>

int main(void)
{
    int flags = 6, mask = 2;
    int a[] = { 10, 20, 30 };
    int *p = a;

    /* == binds tighter than &, so "flags & mask == 2" means this: */
    printf("flags & (mask == 2) -> %d\n", flags & (mask == 2));
    printf("(flags & mask) == 2 -> %d\n", (flags & mask) == 2);

    /* + binds tighter than <<, so "1 << 2 + 1" means this: */
    printf("1 << (2 + 1)        -> %d\n", 1 << (2 + 1));

    /* postfix ++ binds tighter than unary *: *(p++) */
    int first = *p++;
    printf("*p++ gave %d, now *p is %d\n", first, *p);

    /* ?: is right associative: a ? b : (c ? d : e) */
    int n = 0;
    printf("n is %s\n", n < 0 ? "negative" : n == 0 ? "zero" : "positive");

    /* = is right associative and returns the value assigned */
    int x, y;
    x = y = 7;
    printf("x=%d y=%d\n", x, y);

    /* integer division truncates toward zero (C99); % keeps the sign of a */
    printf("-7 / 2 = %d, -7 %% 2 = %d\n", -7 / 2, -7 % 2);

    /* the comma operator: evaluate left, then right; value of the right */
    int c = (x++, x + 1);
    printf("c=%d\n", c);
    return 0;
}
