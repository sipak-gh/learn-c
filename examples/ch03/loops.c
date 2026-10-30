/* loops.c - while, do-while and for; break and continue. */
#include <stdio.h>

int main(void)
{
    unsigned int n = 6667, digits = 0;
    while (n > 0) {                      /* test first: may run zero times */
        n /= 10;
        digits++;
    }
    printf("6667 has %u digits\n", digits);

    int tries = 0;
    do {                                 /* test last: runs at least once */
        tries++;
    } while (tries < 0);
    printf("do-while ran %d time(s)\n", tries);

    for (int i = 1; i <= 5; i++)         /* i exists only inside the loop */
        printf("%d squared is %d\n", i, i * i);

    for (int i = 1; i <= 20; i++) {
        if (i % 2 == 0)
            continue;                    /* skip the rest of this round */
        if (i > 9)
            break;                       /* leave the loop */
        printf("%d ", i);
    }
    printf("\n");

    for (;;) {                           /* no condition: loop forever */
        printf("once\n");
        break;
    }
    return 0;
}
