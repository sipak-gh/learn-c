/* parleyd.c - the parley chat server: options and the main loop.
 * Version 2: one thread per client. */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

/* Everything the client threads share: the room, every struct conn in it
 * (except its in and skipping fields, which only its own thread touches)
 * and the right to write to client sockets. One mutex protects it all:
 * a thread must hold room_lock while it uses any of it. */
static pthread_mutex_t room_lock = PTHREAD_MUTEX_INITIALIZER;
static struct room room;
/* Signalled when the last client thread has left the room. */
static pthread_cond_t room_empty = PTHREAD_COND_INITIALIZER;

/* Our threads only need a few KiB of stack. The default size differs
 * (glibc: the stack limit, often 8 MiB; musl: 128 KiB), so we choose. */
#define THREAD_STACK_SIZE (256 * 1024)

/* The thread that serves one client, until it quits or disconnects or the
 * server shuts down. arg is the client's struct conn, which the thread
 * owns: it frees it before it ends. */
static void *client_thread(void *arg)
{
    struct conn *c = arg;
    char line[MSG_LINE_MAX + 1];
    const char *reason = "Connection closed";
    char error[128];
    bool open = true;

    while (open) {
        /* Block in read() WITHOUT the lock, so other threads can run. */
        ssize_t n = read_into(c->fd, &c->in, 4096);
        int saved = errno;

        pthread_mutex_lock(&room_lock);
        if (n <= 0) {
            if (n < 0) {
                str_copy(error, strerror(saved), sizeof error);
                reason = error;
            }
            open = false;
        }
        enum frame_status st;
        while (c->state == CONN_OPEN
               && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(c, "417", NULL, "Input line too long");
            else
                room_handle_line(&room, c, line);
        }
        if (c->state != CONN_OPEN)
            open = false;
        pthread_mutex_unlock(&room_lock);
    }

    pthread_mutex_lock(&room_lock);
    room_part(&room, c, reason);     /* does nothing after QUIT */
    room_remove(&room, c);
    if (room.count == 0)
        pthread_cond_signal(&room_empty);
    log_msg(LOG_DEBUG, "closing connection from %s", c->addr);
    pthread_mutex_unlock(&room_lock);

    close(c->fd);
    sb_free(&c->in);
    free(c);
    return NULL;
}

/* Start a thread for a new client on socket fd. The thread is detached:
 * nobody waits for it with pthread_join, its resources are freed when it
 * ends. Called with room_lock held. */
static void start_client(int fd, const char *addr, const pthread_attr_t *attr)
{
    struct conn *c = malloc(sizeof *c);
    if (c == NULL || room_add(&room, c) < 0) {
        log_msg(LOG_ERROR, "%s: out of memory", addr);
        free(c);
        close(fd);
        return;
    }
    *c = (struct conn){ .fd = fd, .state = CONN_OPEN };
    str_copy(c->addr, addr, sizeof c->addr);

    pthread_t tid;
    int err = pthread_create(&tid, attr, client_thread, c);
    if (err != 0) {                    /* pthread functions return the error */
        log_msg(LOG_ERROR, "%s: cannot start a thread: %s", addr, strerror(err));
        room_remove(&room, c);
        free(c);
        close(fd);
    }
}

/* Accept clients until SIGINT or SIGTERM; each gets its own thread. */
static void serve(int listen_fd, size_t max_clients)
{
    pthread_attr_t attr;
    size_t stack_size;
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stack_size);
    log_msg(LOG_DEBUG, "default thread stack size: %zu KiB, using %d KiB",
            stack_size / 1024, THREAD_STACK_SIZE / 1024);
    pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* A signal goes to any one thread that does not block it. We want
     * SIGINT and SIGTERM to interrupt accept() in this thread, so client
     * threads block them: a new thread inherits the creator's signal mask,
     * so we block them while we create it. */
    sigset_t quit_signals, old_mask;
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGINT);
    sigaddset(&quit_signals, SIGTERM);

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

        pthread_mutex_lock(&room_lock);
        if (room.count >= max_clients) {
            log_msg(LOG_WARN, "%s: server full", addr);
            struct conn c = { .fd = fd, .state = CONN_OPEN };
            conn_send(&c, &(struct msg){ .command = "ERROR", .trailing = "Server full" });
            close(fd);
        } else {
            pthread_sigmask(SIG_BLOCK, &quit_signals, &old_mask);
            start_client(fd, addr, &attr);
            pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
        }
        pthread_mutex_unlock(&room_lock);
    }
    pthread_attr_destroy(&attr);
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));

    /* Say goodbye to every client, then wake its thread: after
     * shutdown(SHUT_RDWR), its blocked read() returns 0 (end of file). */
    pthread_mutex_lock(&room_lock);
    for (size_t i = 0; i < room.count; i++) {
        struct conn *c = room.conns[i];
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Server shutting down" });
        c->nick[0] = '\0';            /* everyone leaves: no QUIT messages */
        c->state = CONN_CLOSING;
        shutdown(c->fd, SHUT_RDWR);
    }
    /* pthread_cond_wait unlocks room_lock while it sleeps, and locks it
     * again before it returns. Always wait in a loop: re-check the state. */
    while (room.count > 0)
        pthread_cond_wait(&room_empty, &room_lock);
    pthread_mutex_unlock(&room_lock);
    room_free(&room);
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
    log_msg(LOG_INFO, "listening on %s", where);

    serve(listen_fd, (size_t)cfg.max_clients);
    close(listen_fd);
    return 0;
}
