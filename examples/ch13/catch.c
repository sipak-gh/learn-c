/* catch.c - catch SIGINT and SIGTERM with sigaction() and stop cleanly. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* volatile: the value may change "behind the compiler's back".
 * sig_atomic_t: an integer the CPU reads and writes in one step. */
static volatile sig_atomic_t got_signal = 0;

static void on_signal(int signo)
{
    got_signal = signo;                 /* nothing else: see the text */
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;          /* or SIG_IGN, or SIG_DFL */
    sigemptyset(&sa.sa_mask);           /* block no extra signals in the handler */
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    printf("pid %d: working; press Ctrl-C or send SIGTERM\n", (int)getpid());
    fflush(stdout);
    int ticks = 0;
    while (got_signal == 0) {
        sleep(1);                       /* a signal cuts the sleep short */
        if (got_signal == 0)
            printf("tick %d\n", ++ticks);
        fflush(stdout);
    }
    printf("got signal %d (%s): saving work and exiting\n",
           (int)got_signal, strsignal(got_signal));
    return 0;
}
