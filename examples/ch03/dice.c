/* dice.c - do-while: roll a die until it shows a six. */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned int seed;

    if (scanf("%u", &seed) != 1) {
        fprintf(stderr, "dice: expected a seed\n");
        return 1;
    }
    srand(seed);                         /* the same seed gives the same rolls */

    int roll, rolls = 0;
    do {
        roll = rand() % 6 + 1;           /* 1..6 */
        rolls++;
        printf("%d ", roll);
    } while (roll != 6);
    printf("\nsix after %d rolls\n", rolls);
    return 0;
}
