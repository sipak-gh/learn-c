/* sig.c - signal handling shared by server and client. */
#include "sig.h"

#include <stddef.h>

volatile sig_atomic_t sig_quit = 0;

/* A signal handler may run between any two instructions of the program,
 * so it must do almost nothing: here, set a flag. Only async-signal-safe
 * functions may be called in a handler (no printf, no malloc). */
static void on_quit(int signo)
{
    sig_quit = signo;
}

int sig_init(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) < 0)
        return -1;

    /* No SA_RESTART in sa_flags: a blocking read() interrupted by the
     * signal fails with EINTR, so the main loop wakes up and sees sig_quit. */
    sa.sa_handler = on_quit;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        return -1;
    return 0;
}
