/* penalty.c - rate limiting with a penalty clock. */
#include <stdbool.h>
#include <stdio.h>

#define COST_MS 200   /* each line costs 200 ms: 5 lines/s on average */
#define BURST 3       /* the clock may run at most 3 lines ahead */

static long long penalty;   /* the client's clock, in ms */

/* May a line that arrives at time now (ms) go through? */
static bool allow(long long now)
{
    if (penalty < now)
        penalty = now;               /* never lag behind real time */
    if (penalty + COST_MS - now > BURST * COST_MS)
        return false;                /* too far ahead: refuse */
    penalty += COST_MS;
    return true;
}

int main(void)
{
    /* When lines arrive (ms): a burst of 5, a pause, then 1 per 100 ms. */
    static const long long at[] = { 0, 0, 0, 0, 0, 700, 800, 900, 1000, 1100, 1200 };
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
        bool ok = allow(at[i]);
        printf("t=%4lld  clock=%4lld  ahead=%3lld  %s\n",
               at[i], penalty, penalty - at[i], ok ? "ok" : "refused");
    }
    return 0;
}
