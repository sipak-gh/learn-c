/* uidemo.c - try the client's ui module on its own, without a server.
 * Put it in the project directory (next to the Makefile) and build it
 * with the module's sources:
 *   gcc -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -Isrc \
 *       -o uidemo uidemo.c src/ui.c src/term.c src/strbuf.c src/str.c src/io.c
 * Type lines and press Enter; resize the window; Ctrl-D quits. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ui.h"

static volatile sig_atomic_t resized = 0;
static int count = 0;

static void on_winch(int signo)
{
    (void)signo;
    resized = 1;
}

/* Called by ui_keys() for each line the user finishes with Enter. */
static void on_line(char *line)
{
    char text[UI_LINE_MAX];
    snprintf(text, sizeof text, "<you> %s", line);
    ui_print(text);
    snprintf(text, sizeof text, " uidemo | %d lines | Ctrl-D quits", ++count);
    ui_set_status(text);
}

int main(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;               /* no SA_RESTART: read() fails with EINTR */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    if (ui_start(" uidemo | 0 lines | Ctrl-D quits", 100) < 0) {
        perror("uidemo");
        return 1;
    }
    atexit(ui_stop);
    ui_print("-- type something and press Enter");
    ui_flush();
    for (;;) {
        char keys[64];
        ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
        if (n < 0 && errno == EINTR) {
            if (resized) {
                resized = 0;
                ui_resize();
                ui_flush();
            }
            continue;
        }
        if (n <= 0 || !ui_keys(keys, (size_t)n, on_line))
            break;
        ui_flush();
    }
    return 0;                      /* exit() runs ui_stop() */
}
