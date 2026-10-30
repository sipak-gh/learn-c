/* bitfields.c - packing small fields into one unsigned int. */
#include <stdio.h>

struct style {
    unsigned int bold : 1;      /* 1 bit: 0 or 1 */
    unsigned int underline : 1;
    unsigned int color : 4;     /* 4 bits: 0..15 */
};

int main(void)
{
    struct style s = { .bold = 1, .color = 9 };
    printf("bold=%u underline=%u color=%u sizeof=%zu\n",
           s.bold, s.underline, s.color, sizeof s);

    unsigned int big = 17;
    s.color = big;              /* too big: only the low 4 bits are kept */
    printf("after color = %u: color=%u\n", big, s.color);
    return 0;
}
