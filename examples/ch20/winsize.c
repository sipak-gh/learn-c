/* winsize.c - print the terminal size, and again whenever it changes.
 * Ctrl-D quits. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>          /* ioctl, TIOCGWINSZ: not POSIX, but everywhere */
#include <unistd.h>

static volatile sig_atomic_t resized = 0;

static void on_winch(int signo)
{
    (void)signo;
    resized = 1;
}

static void print_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        perror("TIOCGWINSZ");
        return;
    }
    printf("%d rows, %d columns\n", ws.ws_row, ws.ws_col);
}

int main(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;            /* no SA_RESTART: a signal interrupts read() */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    print_size();
    char buf[256];
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            if (resized) {
                resized = 0;
                print_size();
            }
            continue;
        }
        if (n <= 0)
            break;              /* end of input (Ctrl-D) or an error */
    }
    return 0;
}
