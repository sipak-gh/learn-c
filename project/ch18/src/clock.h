/* clock.h - a clock for timeouts. */
#ifndef PARLEY_CLOCK_H
#define PARLEY_CLOCK_H

/* Milliseconds since some fixed point in the past. Unlike the time of day,
 * this clock never jumps (CLOCK_MONOTONIC), so it is right for measuring
 * how long something took or how long to wait. */
long long clock_ms(void);

#endif
