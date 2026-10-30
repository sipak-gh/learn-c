/* speed.c - time three things a chat server does all day:
 * small malloc/free pairs, snprintf, and searching a buffer for '\n'.
 * Compare libcs on the same machine only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 4000000
#define LIVE 1024

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    static void *slots[LIVE];
    static char buf[512];
    char line[128];
    unsigned long sum = 0;
    double t0;
    int i;

    t0 = now_ms();
    for (i = 0; i < N; i++) {
        int k = i % LIVE;
        free(slots[k]);
        slots[k] = malloc((size_t)(16 + i % 200));   /* message-sized blocks */
    }
    printf("malloc+free  x %d: %4.0f ms\n", N, now_ms() - t0);
    for (i = 0; i < LIVE; i++)
        free(slots[i]);

    t0 = now_ms();
    for (i = 0; i < N; i++)
        sum += (unsigned long)snprintf(line, sizeof line, ":%s MSG :%s %d\r\n",
                                       "alice", "hello", i);
    printf("snprintf     x %d: %4.0f ms\n", N, now_ms() - t0);

    memset(buf, 'x', sizeof buf);
    t0 = now_ms();
    for (i = 0; i < N; i++) {
        buf[i % sizeof buf] = '\n';                   /* move the newline */
        sum += (unsigned long)((char *)memchr(buf, '\n', sizeof buf) - buf);
        buf[i % sizeof buf] = 'x';
    }
    printf("memchr 512 B x %d: %4.0f ms\n", N, now_ms() - t0);
    return sum == 0;                   /* use sum, so no loop is optimized away */
}
