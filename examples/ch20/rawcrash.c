/* rawcrash.c - switch off echo and line editing, then leave without
 * putting them back. "rawcrash" just returns; "rawcrash abort" crashes.
 * With -s ("safe"), atexit() and signal handlers restore the terminal. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;

static void restore(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

/* tcsetattr() is async-signal-safe: we may call it in a handler. */
static void on_fatal(int signo)
{
    restore();
    raise(signo);       /* SA_RESETHAND made the action SIG_DFL: die of it */
}

int main(int argc, char *argv[])
{
    int safe = argc > 1 && strcmp(argv[1], "-s") == 0;
    int crash = argc > 1 + safe && strcmp(argv[1 + safe], "abort") == 0;

    if (tcgetattr(STDIN_FILENO, &saved) < 0) {
        perror("rawcrash: tcgetattr");
        return 1;
    }
    if (safe) {
        atexit(restore);                /* covers return from main, exit() */
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;     /* back to the default action on entry */
        sa.sa_handler = on_fatal;
        int fatal[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGTERM, SIGHUP, SIGQUIT };
        for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
            sigaction(fatal[i], &sa, NULL);
    }
    struct termios t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    printf("echo is off. %s now...\n", crash ? "Crashing" : "Leaving");
    fflush(stdout);
    if (crash)
        abort();
    return 0;
}
