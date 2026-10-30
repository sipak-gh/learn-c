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

/* The self-pipe trick: create a pipe, and make the SIGINT/SIGTERM handler
 * also write a byte into it. Returns the read end (non-blocking), for the
 * caller to poll(): it becomes readable when a quit signal arrives, so
 * poll() wakes up even if the signal came just before poll() was called.
 * Returns -1 with errno set on failure. Call after sig_init(). */
int sig_wake_fd(void);

/* Set by SIGWINCH (the terminal window changed size) once
 * sig_watch_winch() was called; the handler also wakes the self-pipe. */
extern volatile sig_atomic_t sig_winch;
int sig_watch_winch(void);

/* Read and discard everything in the self-pipe. */
void sig_drain(int fd);

#endif
