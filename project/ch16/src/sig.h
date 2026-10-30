/* sig.h - signal handling shared by server and client. */
#ifndef PARLEY_SIG_H
#define PARLEY_SIG_H

#include <signal.h>

/* The number of the SIGINT or SIGTERM that asked us to quit, or 0. The
 * handler sets it; the main loop checks it when a call fails with EINTR. */
extern volatile sig_atomic_t sig_quit;

/* Ignore SIGPIPE (a write to a closed connection then fails with EPIPE
 * instead of killing the process) and make SIGINT and SIGTERM set sig_quit.
 * Returns 0, or -1 with errno set. */
int sig_init(void);

#endif
