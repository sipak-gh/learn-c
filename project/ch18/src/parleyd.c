/* parleyd.c - the parley chat server: options and the main loop.
 * Version 3: one thread, many sockets (see server.c). */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct config {
    const char *addr;
    long port;
    long max_clients;
    long idle;
    bool verbose;
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
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-t IDLE] [-v]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -t IDLE        PING clients idle for IDLE seconds, drop them after\n"
           "                 twice that; 0: never (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS, DEFAULT_IDLE);
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
        if (opt == 'p' || opt == 'b' || opt == 'm' || opt == 't') {
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
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
        .idle = DEFAULT_IDLE,
        .verbose = false,
    };
    parse_args(argc, argv, &cfg);
    log_init("parleyd", cfg.verbose);
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

    struct server_config scfg = {
        .max_clients = (size_t)cfg.max_clients,
        .idle_secs = (int)cfg.idle,
    };
    int status = server_run(listen_fd, &scfg);
    close(listen_fd);
    return status == 0 ? 0 : 1;
}
