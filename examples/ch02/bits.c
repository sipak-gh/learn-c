/* bits.c - bitwise operators: flags packed into one unsigned int. */
#include <stdio.h>

enum {
    FLAG_VERBOSE = 1u << 0,          /* 0x1 */
    FLAG_DAEMON  = 1u << 1,          /* 0x2 */
    FLAG_IPV6    = 1u << 2           /* 0x4 */
};

int main(void)
{
    unsigned int flags = 0;

    flags |= FLAG_VERBOSE | FLAG_IPV6;           /* set two bits */
    printf("set:    %#04x\n", flags);
    printf("verbose? %d, daemon? %d\n",
           (flags & FLAG_VERBOSE) != 0, (flags & FLAG_DAEMON) != 0);

    flags &= ~FLAG_VERBOSE;                      /* clear a bit */
    printf("clear:  %#04x\n", flags);
    flags ^= FLAG_DAEMON;                        /* toggle a bit */
    printf("toggle: %#04x\n", flags);

    printf("0xF0 >> 4 = %#x, 1u << 31 = %#x, ~0u = %#x\n", 0xF0 >> 4, 1u << 31, ~0u);
    printf("0xC & 0xA = %#x, 0xC | 0xA = %#x, 0xC ^ 0xA = %#x\n", 0xC & 0xA, 0xC | 0xA, 0xC ^ 0xA);
    return 0;
}
