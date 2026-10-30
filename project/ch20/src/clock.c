/* clock.c - a clock for timeouts. */
#include "clock.h"

#include <time.h>

long long clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   /* cannot fail with these arguments */
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
