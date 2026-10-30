/* bench.c - how fast does a parleyd deliver messages?
 *
 *     bench PORT [CLIENTS [ROUNDS]]
 *
 * Connects CLIENTS clients (default 50) to the parleyd on 127.0.0.1:PORT.
 * In each of ROUNDS rounds (default 200) every client sends one message,
 * and we wait until every client has received all of them (the server
 * sends each message to everyone, the sender too). So CLIENTS * ROUNDS
 * messages go in, and CLIENTS * CLIENTS * ROUNDS lines come out.
 * Start the server with "-f 0" (no flood limit) and "-m" at least CLIENTS.
 * The numbers depend on the machine: compare runs on the same machine.
 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "io.h"
#include "msg.h"
#include "net.h"
#include "str.h"
#include "strbuf.h"

#define TIMEOUT_MS 5000     /* give up if the server is silent this long */

struct client {
    int fd;
    struct strbuf in;
    bool skipping;
    bool registered;        /* saw the end of the NAMES list */
    long got;               /* MSG lines received */
};

/* Is c done: registered (target < 0), or got target messages? */
static bool done(const struct client *c, long target)
{
    return target < 0 ? c->registered : c->got >= target;
}

/* Read from the clients until every one is done(target). False if the
 * server closed a connection or went quiet. */
static bool wait_all(struct client *cs, struct pollfd *pfds, int n, long target)
{
    char line[MSG_LINE_MAX + 1];
    for (;;) {
        int waiting = 0;
        for (int i = 0; i < n; i++)
            if (!done(&cs[i], target))
                pfds[waiting++] = (struct pollfd){ .fd = cs[i].fd, .events = POLLIN };
        if (waiting == 0)
            return true;
        int ready = poll(pfds, (nfds_t)waiting, TIMEOUT_MS);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0) {
            fprintf(stderr, "bench: %s\n", ready == 0 ? "timed out" : strerror(errno));
            return false;
        }
        for (int i = 0; i < n; i++) {
            struct client *c = &cs[i];
            if (done(c, target))
                continue;
            int k = 0;              /* c's entry in pfds (same order) */
            while (pfds[k].fd != c->fd)
                k++;
            if (pfds[k].revents == 0)
                continue;
            ssize_t got = read_into(c->fd, &c->in, 4096);
            if (got <= 0) {
                fprintf(stderr, "bench: client %d: %s\n", i,
                        got == 0 ? "the server closed the connection" : strerror(errno));
                return false;
            }
            enum frame_status st;
            while ((st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
                if (st != FRAME_LINE)
                    continue;
                if (strstr(line, " MSG :") != NULL)
                    c->got++;
                else if (strncmp(line, ":parley 366 ", 12) == 0)
                    c->registered = true;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    long port, n = 50, rounds = 200;
    if (argc < 2 || argc > 4 || !str_to_long(argv[1], 1, 65535, &port)
        || (argc > 2 && !str_to_long(argv[2], 1, 1000, &n))
        || (argc > 3 && !str_to_long(argv[3], 1, 100000, &rounds))) {
        fprintf(stderr, "usage: bench PORT [CLIENTS [ROUNDS]]\n");
        return 2;
    }

    struct client *cs = calloc((size_t)n, sizeof *cs);
    struct pollfd *pfds = calloc((size_t)n, sizeof *pfds);
    if (cs == NULL || pfds == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        return 1;
    }
    char text[64], err[256];
    for (int i = 0; i < n; i++) {
        cs[i].fd = net_connect("127.0.0.1", (int)port, err, sizeof err);
        if (cs[i].fd < 0) {
            fprintf(stderr, "bench: cannot connect to %s\n", err);
            return 1;
        }
        int len = snprintf(text, sizeof text, "NICK b%d\r\n", i);
        if (write_all(cs[i].fd, text, (size_t)len) < 0) {
            fprintf(stderr, "bench: write: %s\n", strerror(errno));
            return 1;
        }
    }
    if (!wait_all(cs, pfds, (int)n, -1))
        return 1;

    long long start = clock_ms();
    for (long r = 0; r < rounds; r++) {
        for (int i = 0; i < n; i++) {
            int len = snprintf(text, sizeof text, "MSG :message %ld from b%d\r\n", r, i);
            if (write_all(cs[i].fd, text, (size_t)len) < 0) {
                fprintf(stderr, "bench: write: %s\n", strerror(errno));
                return 1;
            }
        }
        if (!wait_all(cs, pfds, (int)n, n * (r + 1)))
            return 1;
    }
    long long ms = clock_ms() - start;
    if (ms == 0)
        ms = 1;

    long long lines = (long long)n * n * rounds;
    printf("bench: %ld clients x %ld rounds: %lld lines delivered in %lld ms"
           " (%lld lines/s)\n", n, rounds, lines, ms, lines * 1000 / ms);
    for (int i = 0; i < n; i++) {
        close(cs[i].fd);
        sb_free(&cs[i].in);
    }
    free(cs);
    free(pfds);
    return 0;
}
