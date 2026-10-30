/* split.c - a scroll region: lines scroll above a bar that stays put. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct winsize ws;
    int rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 2)
        rows = ws.ws_row;

    printf("\x1b[2J");                                    /* clear the screen */
    printf("\x1b[%d;1H\x1b[7m status bar: I stay put \x1b[0m", rows);
    printf("\x1b[1;%dr", rows - 1);                       /* rows 1..rows-1 scroll */
    for (int i = 1; i <= 2 * rows; i++) {
        /* Go to the bottom row of the region; '\n' there scrolls the
         * region (and only the region) up by one line. */
        printf("\x1b[%d;1H\nline %d", rows - 1, i);
        fflush(stdout);
        nanosleep(&(struct timespec){ .tv_sec = 0, .tv_nsec = 100000000 }, NULL);
    }
    printf("\x1b[r");                                     /* the whole screen again */
    printf("\x1b[%d;1H\n", rows);                         /* below the bar */
    return 0;
}
