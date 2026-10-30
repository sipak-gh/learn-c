/* rng.c - rand() differs between C libraries; xorshift32 does not. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* George Marsaglia's xorshift32: tiny, fast, the same everywhere.
 * The state must never be 0. */
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

int main(void)
{
    srand(42);
    printf("rand():       ");
    for (int i = 0; i < 4; i++)
        printf(" %d", rand() % 1000);
    printf("\n");

    uint32_t state = 42;
    printf("xorshift32(): ");
    for (int i = 0; i < 4; i++)
        printf(" %lu", (unsigned long)(xorshift32(&state) % 1000));
    printf("\n");
    return 0;
}
