/* parleyd.c - the parley chat server: options and the main loop.
 * Version 3: one thread, many sockets (see server.c). */

/* getrlimit() is part of the X/Open System Interfaces (XSI) option of
 * POSIX, so ask for those too; 700 includes all of POSIX.1-2008. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "log.h"
#include "net.h"
#include "server.h"
#include "sig.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
#define DEFAULT_MAX_CLIENTS 64
#define MAX_CLIENTS_MAX 1000
#define DEFAULT_IDLE 120
#define IDLE_MAX 86400
#define DEFAULT_FLOOD 20
#define FLOOD_MAX 1000

/* File descriptors the server needs besides one per client: stdin, stdout,
 * stderr, the listening socket, the self-pipe (2) and the spare (1). */
#define FDS_RESERVED 7

struct config {
    const char *addr;
    long port;
    long max_clients;
    long idle;
    long flood;
    bool verbose;
    bool timestamps;
};

static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parleyd: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parleyd --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-f LINES]\n"
           "               [-v] [-T]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -t IDLE        PING clients idle for IDLE seconds, drop them after\n"
           "                 twice that; 0: never (default %d)\n"
           "  -f LINES       drop clients sending more than LINES lines at once\n"
           "                 (5 lines/s on average); 0: no limit (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -T             no timestamps in the log\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS, DEFAULT_IDLE,
           DEFAULT_FLOOD);
}

static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parleyd " PARLEY_VERSION "\n");   /* "a" "b" is "ab" */
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);

        char opt = arg[1];
        const char *value = NULL;
        if (opt == 'p' || opt == 'b' || opt == 'm' || opt == 't' || opt == 'f') {
            if (i + 1 >= argc)
                usage_error("option -%c needs a value", opt);
            value = argv[++i];
        }

        switch (opt) {
        case 'b':
            cfg->addr = value;
            break;
        case 'p':
            if (!str_to_long(value, 0, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 0-%d)", value, PORT_MAX);
            break;
        case 'm':
            if (!str_to_long(value, 1, MAX_CLIENTS_MAX, &cfg->max_clients))
                usage_error("invalid client limit '%s' (expected 1-%d)",
                            value, MAX_CLIENTS_MAX);
            break;
        case 't':
            if (!str_to_long(value, 0, IDLE_MAX, &cfg->idle))
                usage_error("invalid idle time '%s' (expected 0-%d)", value, IDLE_MAX);
            break;
        case 'f':
            if (!str_to_long(value, 0, FLOOD_MAX, &cfg->flood))
                usage_error("invalid flood limit '%s' (expected 0-%d)", value, FLOOD_MAX);
            break;
        case 'v':
            cfg->verbose = true;
            break;
        case 'T':
            cfg->timestamps = false;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

/* Each client needs a file descriptor, and a process may only have
 * RLIMIT_NOFILE of them open (see "ulimit -n"). Lower *max_clients to
 * what fits, so clients get "Server full" rather than accept() failing. */
static void limit_clients(long *max_clients)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        die("getrlimit: %s", strerror(errno));
    log_msg(LOG_DEBUG, "open file limit: %llu", (unsigned long long)rl.rlim_cur);
    if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= (rlim_t)(*max_clients + FDS_RESERVED))
        return;
    long fit = rl.rlim_cur > FDS_RESERVED ? (long)rl.rlim_cur - FDS_RESERVED : 0;
    if (fit == 0)
        die("the open file limit (%llu) leaves no room for clients",
            (unsigned long long)rl.rlim_cur);
    log_msg(LOG_WARN, "the open file limit (%llu) allows only %ld clients",
            (unsigned long long)rl.rlim_cur, fit);
    *max_clients = fit;
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .idle = DEFAULT_IDLE,
        .flood = DEFAULT_FLOOD,
        .verbose = false,
        .timestamps = true,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
    log_timestamps(cfg.timestamps);
    log_msg(LOG_DEBUG, "verbose logging is on");
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));

    char err[256], where[NET_ADDR_MAX];
    int listen_fd = net_listen(cfg.addr, (int)cfg.port, err, sizeof err);
    if (listen_fd < 0)
        die("cannot listen on %s", err);
    if (net_local_addr(listen_fd, where, sizeof where) < 0)
        die("getsockname: %s", strerror(errno));
    log_msg(LOG_INFO, "listening on %s", where);
    limit_clients(&cfg.max_clients);

    struct server_config scfg = {
        .max_clients = (size_t)cfg.max_clients,
        .idle_secs = (int)cfg.idle,
        .flood_lines = (int)cfg.flood,
    };
    int status = server_run(listen_fd, &scfg);
    close(listen_fd);
    return status == 0 ? 0 : 1;
}
