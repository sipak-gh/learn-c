/* server.h - the server's event loop: one thread, many sockets. */
#ifndef PARLEY_SERVER_H
#define PARLEY_SERVER_H

#include <stddef.h>

struct server_config {
    size_t max_clients;     /* more get "ERROR :Server full" */
    int idle_secs;          /* PING after this long without input; 0: never */
};

/* Serve clients on listen_fd until SIGINT or SIGTERM (sig_init() must have
 * been called). Returns 0, or -1 after logging a fatal error. */
int server_run(int listen_fd, const struct server_config *cfg);

#endif
