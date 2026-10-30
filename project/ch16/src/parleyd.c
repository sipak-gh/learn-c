/* parleyd.c - the parley chat server: options and the main loop.
 * Version 1: a blocking server that talks to one client at a time. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conn.h"
#include "io.h"
#include "log.h"
#include "msg.h"
#include "net.h"
#include "room.h"
#include "sig.h"
#include "str.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
#define DEFAULT_MAX_CLIENTS 64
#define MAX_CLIENTS_MAX 1000

struct config {
    const char *addr;
    long port;
    long max_clients;
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
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n"
           "  -p PORT        TCP port to listen on, 0: any free port (default %d)\n"
           "  -b ADDR        address to listen on (default 127.0.0.1)\n"
           "  -m MAXCLIENTS  most clients at once, 1-%d (default %d)\n"
           "  -v             verbose (debug) logging\n"
           "  -h, --help     show this help and exit\n"
           "  --version      show the version and exit\n",
           DEFAULT_PORT, MAX_CLIENTS_MAX, DEFAULT_MAX_CLIENTS);
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
        if (opt == 'p' || opt == 'b' || opt == 'm') {
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
        case 'v':
            cfg->verbose = true;
            break;
        default:
            usage_error("unknown option '%s'", arg);
        }
    }
}

/* Serve one client on socket fd until it quits or disconnects, or a
 * signal asks us to stop. Closes fd. */
static void serve_client(int fd, const char *addr)
{
    struct conn client = { .fd = fd, .state = CONN_OPEN };
    str_copy(client.addr, addr, sizeof client.addr);
    char line[MSG_LINE_MAX + 1];

    while (client.state == CONN_OPEN) {
        ssize_t n = read_into(fd, &client.in, 4096);
        if (n < 0 && errno == EINTR) {
            if (sig_quit == 0)
                continue;              /* some other signal: just retry */
            conn_send(&client, &(struct msg){
                .command = "ERROR", .trailing = "Server shutting down" });
            break;
        }
        if (n < 0) {
            log_msg(LOG_WARN, "%s: reading: %s", addr, strerror(errno));
            break;
        }
        if (n == 0) {                  /* end of file: the client hung up */
            if (client.nick[0] != '\0')
                log_msg(LOG_INFO, "%s left (Connection closed)", client.nick);
            break;
        }

        enum frame_status st;
        while (client.state == CONN_OPEN
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
    }
    log_msg(LOG_DEBUG, "closing connection from %s", addr);
    sb_free(&client.in);
    close(fd);
}

/* Accept clients one after the other until SIGINT or SIGTERM. */
static void serve(int listen_fd)
{
    while (sig_quit == 0) {
        struct sockaddr_storage ss;       /* room for any kind of address */
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            continue;                  /* the loop condition checks sig_quit */
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);
        serve_client(fd, addr);
    }
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));
}

int main(int argc, char *argv[])
{
    struct config cfg = {
        .addr = "127.0.0.1",
        .port = DEFAULT_PORT,
        .max_clients = DEFAULT_MAX_CLIENTS,
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
    log_msg(LOG_INFO, "listening on %s (one client at a time)", where);

    serve(listen_fd);
    close(listen_fd);
    return 0;
}
