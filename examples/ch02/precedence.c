/* precedence.c - three expressions that don't mean what they look like. */
#include <stdio.h>

enum { FLAG_IPV6 = 0x4 };

int main(void)
{
    unsigned int flags = 0x4;

    /* == binds tighter than &: this is flags & (FLAG_IPV6 == FLAG_IPV6). */
    printf("flags & FLAG_IPV6 == FLAG_IPV6   -> %u\n", flags & FLAG_IPV6 == FLAG_IPV6);
    printf("(flags & FLAG_IPV6) == FLAG_IPV6 -> %d\n", (flags & FLAG_IPV6) == FLAG_IPV6);

    /* + binds tighter than <<: this is 1 << (2 + 1). */
    printf("1 << 2 + 1 -> %d\n", 1 << 2 + 1);

    /* Relational operators go left to right: (3 > 2) > 1 is 1 > 1. */
    printf("3 > 2 > 1 -> %d\n", 3 > 2 > 1);
    return 0;
}
