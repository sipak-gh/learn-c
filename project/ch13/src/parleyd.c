/* parleyd.c - the parley chat server: options and the main loop.
 * No network yet (Chapters 6-14): the server talks to one client on
 * stdin/stdout. */
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
           "  -p PORT        TCP port to listen on (default %d)\n"
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
            if (!str_to_long(value, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
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

/* Run one client session: protocol lines come in on file descriptor 0
 * (stdin) and replies go out on 1 (stdout). Returns 0 when the session
 * ended normally, -1 on an error (already logged). */
static int serve_stdin(void)
{
    int result = -1;
    struct conn client = { .fd = STDOUT_FILENO, .state = CONN_OPEN };
    char line[MSG_LINE_MAX + 1];

    if (isatty(STDIN_FILENO))
        log_msg(LOG_INFO, "type protocol lines, e.g. NICK alice (Ctrl-D ends)");

    while (client.state == CONN_OPEN) {
        ssize_t n = read_into(STDIN_FILENO, &client.in, 4096);
        if (n < 0) {
            if (errno == EINTR && sig_quit != 0) {
                log_msg(LOG_INFO, "got signal %d (%s), shutting down",
                        (int)sig_quit, strsignal(sig_quit));
                conn_send(&client, &(struct msg){
                    .command = "ERROR", .trailing = "Server shutting down" });
                break;
            }
            if (errno == EINTR)
                continue;              /* some other signal: just retry */
            log_msg(LOG_ERROR, "reading from client: %s", strerror(errno));
            goto out;
        }
        if (n == 0)
            break;                     /* end of file: the client is gone */

        enum frame_status st;
        while (client.state == CONN_OPEN
               && (st = msg_take_line(&client.in, &client.skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(&client, "417", NULL, "Input line too long");
            else
                room_handle_line(&client, line);
        }
    }
    if (client.state != CONN_DEAD)
        result = 0;
out:
    sb_free(&client.in);           /* one cleanup path for every exit */
    return result;
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
    log_msg(LOG_INFO, "reading one client session from stdin");

    if (serve_stdin() < 0)
        return 1;
    log_msg(LOG_INFO, "session over");
    return 0;
}
