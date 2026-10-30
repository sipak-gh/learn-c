/* pollbug.c - why poll() and stdio input don't mix. */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static double seconds(void)              /* time since the first call */
{
    static struct timespec start;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (start.tv_sec == 0 && start.tv_nsec == 0)
        start = now;
    return (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void)
{
    char line[256];
    seconds();
    for (;;) {
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&pfd, 1, -1) < 0)
            return 1;
        /* poll() said "the fd is readable", so we read ONE line... */
        if (fgets(line, sizeof line, stdin) == NULL)
            break;
        printf("%.1f s: %s", seconds(), line);
        fflush(stdout);
        /* ...but fgets() read everything that was there into stdin's
         * buffer. The fd is empty now, so the next poll() sleeps, while
         * the other lines wait in a buffer poll() knows nothing about. */
    }
    printf("%.1f s: end of input\n", seconds());
    return 0;
}
