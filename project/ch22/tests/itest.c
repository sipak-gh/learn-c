/* itest.c - the integration test: a real parleyd, real TCP clients.
 *
 *     itest PATH-TO-PARLEYD [NAME...]
 *
 * Each test starts its own server with "-p 0", so the kernel picks a free
 * port (the server logs which one: we read it from the log file), then
 * connects scripted clients and checks every line they receive. Nothing
 * here is mocked: this is the server exactly as users run it.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "clock.h"
#include "io.h"
#include "msg.h"
#include "net.h"
#include "strbuf.h"
#include "test.h"

#define TIMEOUT_MS 3000     /* the longest we wait for any expected line */

static const char *parleyd;  /* path of the server binary */
static int failed_checks;

void test_fail(const char *file, int line, const char *fmt, ...)
{
    printf("%s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    failed_checks++;
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
        continue;
}

/* The server under test. */

struct server {
    pid_t pid;
    int port;
    char log[256];          /* its stderr goes to this file */
};

/* Find "listening on 127.0.0.1:PORT" in the log; 0 if not there yet. */
static int port_from_log(const char *path)
{
    char text[1024];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, text, sizeof text - 1);
    close(fd);
    if (n <= 0)
        return 0;
    text[n] = '\0';
    const char *p = strstr(text, "listening on 127.0.0.1:");
    return p == NULL ? 0 : atoi(p + strlen("listening on 127.0.0.1:"));
}

/* Start parleyd with the given extra options (a NULL-terminated list).
 * Returns false if it doesn't come up. */
static bool start_server(struct server *s, const char *const *options)
{
    const char *argv[16] = { parleyd, "-p", "0", "-T" };
    int argc = 4;
    for (int i = 0; options[i] != NULL && argc < 15; i++)
        argv[argc++] = options[i];
    argv[argc] = NULL;

    snprintf(s->log, sizeof s->log, "%s.itest.log", parleyd);
    int log_fd = open(s->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) {
        printf("itest: %s: %s\n", s->log, strerror(errno));
        return false;
    }
    fflush(stdout);                 /* or the child would print it again */
    s->pid = fork();
    if (s->pid < 0) {
        printf("itest: fork: %s\n", strerror(errno));
        close(log_fd);
        return false;
    }
    if (s->pid == 0) {              /* the child: become the server */
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        /* execv() wants char *const argv[]; it does not change them. */
        execv(parleyd, (char *const *)argv);
        fprintf(stderr, "exec %s: %s\n", parleyd, strerror(errno));
        _exit(127);
    }
    close(log_fd);
    long long deadline = clock_ms() + TIMEOUT_MS;
    while ((s->port = port_from_log(s->log)) == 0 && clock_ms() < deadline)
        sleep_ms(10);
    if (s->port == 0) {
        printf("itest: the server did not start (see %s)\n", s->log);
        kill(s->pid, SIGKILL);
        waitpid(s->pid, NULL, 0);
        return false;
    }
    return true;
}

/* Stop the server with SIGTERM; returns its exit status (-1: crashed). */
static int stop_server(struct server *s)
{
    int status;
    kill(s->pid, SIGTERM);
    if (waitpid(s->pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* A scripted client. */

struct client {
    int fd;
    struct strbuf in;
    bool skipping;
};

static bool connect_client(struct client *c, int port)
{
    char err[256];
    *c = (struct client){ .fd = net_connect("127.0.0.1", port, err, sizeof err) };
    if (c->fd < 0)
        printf("itest: cannot connect to %s\n", err);
    return c->fd >= 0;
}

static void close_client(struct client *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    sb_free(&c->in);
}

static void send_text(struct client *c, const char *text)
{
    if (write_all(c->fd, text, strlen(text)) < 0)
        printf("itest: write: %s\n", strerror(errno));
}

/* The next line from the server into line[]: 1, or 0 at the end of the
 * connection, or -1 if none came within TIMEOUT_MS, or -2 on an error
 * (such as ECONNRESET: the server reset the connection). */
static int next_line(struct client *c, char line[MSG_LINE_MAX + 1])
{
    long long deadline = clock_ms() + TIMEOUT_MS;
    for (;;) {
        enum frame_status st = msg_take_line(&c->in, &c->skipping, line);
        if (st == FRAME_LINE)
            return 1;
        if (st != FRAME_NONE)
            continue;                           /* a bad line: skip it */
        long long left = deadline - clock_ms();
        struct pollfd p = { .fd = c->fd, .events = POLLIN };
        if (left <= 0 || poll(&p, 1, (int)left) <= 0)
            return -1;
        ssize_t n = read_into(c->fd, &c->in, 4096);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -2;
        if (n == 0)
            return 0;
    }
}

/* Check that the next line c receives is want. */
#define EXPECT(c, want) expect_line((c), (want), __FILE__, __LINE__)
static const char *what(int r)
{
    return r == 0 ? "the end of connection"
           : r == -1 ? "nothing (timeout)" : strerror(errno);
}

static void expect_line(struct client *c, const char *want, const char *file, int lineno)
{
    char line[MSG_LINE_MAX + 1];
    int r = next_line(c, line);
    if (r <= 0)
        test_fail(file, lineno, "expected \"%s\", got %s", want, what(r));
    else if (strcmp(line, want) != 0)
        test_fail(file, lineno, "expected \"%s\", got \"%s\"", want, line);
}

/* Check that the server closes c's connection, cleanly (no reset) and
 * with no more lines. */
#define EXPECT_CLOSED(c) expect_closed((c), __FILE__, __LINE__)
static void expect_closed(struct client *c, const char *file, int lineno)
{
    char line[MSG_LINE_MAX + 1];
    int r = next_line(c, line);
    if (r != 0)
        test_fail(file, lineno, "expected the end of connection, got %s",
                  r == 1 ? line : what(r));
}

/* Connect and register as nick; others[] (NULL-terminated) are already in
 * the room and see the JOIN. names is the expected 353 list. */
static void join(struct client *c, int port, const char *nick, const char *names,
                 struct client *others[])
{
    char text[600];
    if (!connect_client(c, port)) {
        failed_checks++;
        return;
    }
    snprintf(text, sizeof text, "NICK %s\r\n", nick);
    send_text(c, text);
    snprintf(text, sizeof text, ":parley 001 %s :Welcome to parley, %s", nick, nick);
    EXPECT(c, text);
    snprintf(text, sizeof text, ":%s JOIN", nick);
    EXPECT(c, text);
    for (int i = 0; others != NULL && others[i] != NULL; i++)
        EXPECT(others[i], text);
    snprintf(text, sizeof text, ":parley 353 %s :%s", nick, names);
    EXPECT(c, text);
    snprintf(text, sizeof text, ":parley 366 %s :End of NAMES", nick);
    EXPECT(c, text);
}

#define START(s, ...)                                                   \
    do {                                                                \
        static const char *const options_[] = { __VA_ARGS__ };          \
        if (!start_server((s), options_)) {                             \
            failed_checks++;                                            \
            return;                                                     \
        }                                                               \
    } while (0)

/* The tests. */

static void test_chat(void)
{
    struct server s;
    struct client alice, bob;
    START(&s, NULL);
    join(&alice, s.port, "alice", "alice", NULL);
    join(&bob, s.port, "bob", "alice bob", (struct client *[]){ &alice, NULL });

    send_text(&alice, "MSG :hi bob\r\n");
    EXPECT(&alice, ":alice MSG :hi bob");
    EXPECT(&bob, ":alice MSG :hi bob");
    send_text(&bob, "ME waves\nNICK Alice\r\nNICK robert\r\n");    /* "\n" is fine too */
    EXPECT(&alice, ":bob ME :waves");
    EXPECT(&bob, ":bob ME :waves");
    EXPECT(&bob, ":parley 433 bob Alice :Nickname is already in use");
    EXPECT(&alice, ":bob NICK robert");
    EXPECT(&bob, ":bob NICK robert");
    send_text(&alice, "QUIT :bye\r\n");
    EXPECT(&alice, "ERROR :Goodbye");
    EXPECT_CLOSED(&alice);
    EXPECT(&bob, ":alice QUIT :bye");

    close_client(&alice);
    close_client(&bob);
    CHECK_INT(stop_server(&s), 0);
}

/* TCP may split lines anywhere. Send every byte in its own segment
 * (TCP_NODELAY turns off Nagle's algorithm, which would combine them). */
static void test_one_byte_at_a_time(void)
{
    struct server s;
    struct client carol;
    START(&s, NULL);
    if (!connect_client(&carol, s.port)) {
        failed_checks++;
        stop_server(&s);
        return;
    }
    int on = 1;
    setsockopt(carol.fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    const char *text = "NICK carol\r\nMSG :one byte at a time\r\n";
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (write_all(carol.fd, text + i, 1) < 0)
            break;
        sleep_ms(2);
    }
    EXPECT(&carol, ":parley 001 carol :Welcome to parley, carol");
    EXPECT(&carol, ":carol JOIN");
    EXPECT(&carol, ":parley 353 carol :carol");
    EXPECT(&carol, ":parley 366 carol :End of NAMES");
    EXPECT(&carol, ":carol MSG :one byte at a time");
    close_client(&carol);
    CHECK_INT(stop_server(&s), 0);
}

static void test_bad_input(void)
{
    struct server s;
    struct client dave;
    char text[1200];
    START(&s, NULL);
    join(&dave, s.port, "dave", "dave", NULL);

    memset(text, 'x', 1000);                       /* a 1000-byte line */
    strcpy(text + 1000, "\r\n");
    send_text(&dave, text);
    EXPECT(&dave, ":parley 417 dave :Input line too long");
    if (write_all(dave.fd, "MSG :a\0b\r\n", 10) < 0)
        failed_checks++;
    EXPECT(&dave, ":parley 400 dave :Line contains a NUL byte");
    send_text(&dave, "JUMP\r\nNICK 9lives\r\n: \r\nPING :still ok\r\n");
    EXPECT(&dave, ":parley 421 dave JUMP :Unknown command");
    EXPECT(&dave, ":parley 432 dave 9lives :Erroneous nickname");
    EXPECT(&dave, ":parley 400 dave :Malformed message");
    EXPECT(&dave, ":parley PONG :still ok");
    close_client(&dave);
    CHECK_INT(stop_server(&s), 0);
}

static void test_flood(void)
{
    struct server s;
    struct client eve, frank;
    char text[30 * 160], echo[200];
    START(&s, "-f", "5", NULL);
    join(&frank, s.port, "frank", "frank", NULL);
    join(&eve, s.port, "eve", "frank eve", (struct client *[]){ &frank, NULL });

    /* eve sends 30 lines of 150 x's at once: more than the server reads
     * in one go (4096 bytes). NICK used one line of her 5; 4 more are
     * fine, the 5th is not. Then the server closes with eve's input still
     * unread: she must get every line up to the ERROR, and a clean end
     * (no reset, see linger() in server.c). */
    size_t len = 0;
    for (int i = 0; i < 30; i++) {
        memcpy(text + len, "MSG :", 5);
        memset(text + len + 5, 'x', 150);
        memcpy(text + len + 155, "\r\n", 2);
        len += 157;
    }
    if (write_all(eve.fd, text, len) < 0)
        failed_checks++;
    snprintf(echo, sizeof echo, ":eve MSG :%.150s", text + 5);
    for (int i = 0; i < 4; i++) {
        EXPECT(&eve, echo);
        EXPECT(&frank, echo);
    }
    EXPECT(&eve, "ERROR :Excess flood");
    EXPECT_CLOSED(&eve);
    EXPECT(&frank, ":eve QUIT :Excess flood");
    close_client(&eve);
    close_client(&frank);
    CHECK_INT(stop_server(&s), 0);
}

/* A client that never reads must not make the server's memory grow
 * without bound: once its output queue is full, it is dropped. */
static void test_slow_reader(void)
{
    struct server s;
    struct client slow, gina;
    char text[600], line[MSG_LINE_MAX + 1];
    START(&s, "-f", "0", NULL);
    join(&slow, s.port, "slow", "slow", NULL);
    join(&gina, s.port, "gina", "slow gina", (struct client *[]){ &slow, NULL });

    /* gina talks, and reads her own lines back; slow reads nothing. */
    strcpy(text, "MSG :");
    memset(text + 5, 'z', 400);
    strcpy(text + 405, "\r\n");
    bool dropped = false;
    for (int sent = 0; !dropped && sent < 20000; sent++) {
        send_text(&gina, text);
        int r = 1;
        while (!dropped && (r = next_line(&gina, line)) == 1)
            if (strcmp(line, ":slow QUIT :Output queue full") == 0)
                dropped = true;
            else if (line[1] == 'g')        /* ":gina MSG ...": our echo */
                break;
        if (r != 1)
            break;
    }
    CHECK(dropped);
    close_client(&slow);
    close_client(&gina);
    CHECK_INT(stop_server(&s), 0);
}

static void test_timeouts(void)
{
    struct server s;
    struct client quiet, idle;
    START(&s, "-t", "1", NULL);
    if (!connect_client(&quiet, s.port)) {            /* never registers */
        failed_checks++;
        stop_server(&s);
        return;
    }
    join(&idle, s.port, "idle", "idle", NULL);        /* never answers */
    EXPECT(&quiet, "ERROR :Registration timeout");
    EXPECT_CLOSED(&quiet);
    EXPECT(&idle, "PING :parley");
    EXPECT(&idle, "ERROR :Ping timeout");
    EXPECT_CLOSED(&idle);
    close_client(&quiet);
    close_client(&idle);
    CHECK_INT(stop_server(&s), 0);
}

static void test_full_and_shutdown(void)
{
    struct server s;
    struct client a, b, c;
    START(&s, "-m", "2", NULL);
    join(&a, s.port, "a", "a", NULL);
    join(&b, s.port, "b", "a b", (struct client *[]){ &a, NULL });
    if (connect_client(&c, s.port)) {
        EXPECT(&c, "ERROR :Server full");
        EXPECT_CLOSED(&c);
        close_client(&c);
    }
    kill(s.pid, SIGTERM);
    EXPECT(&a, "ERROR :Server shutting down");
    EXPECT_CLOSED(&a);
    EXPECT(&b, "ERROR :Server shutting down");
    EXPECT_CLOSED(&b);
    close_client(&a);
    close_client(&b);
    CHECK_INT(stop_server(&s), 0);
}

static const struct {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "chat", test_chat },
    { "one_byte_at_a_time", test_one_byte_at_a_time },
    { "bad_input", test_bad_input },
    { "flood", test_flood },
    { "slow_reader", test_slow_reader },
    { "timeouts", test_timeouts },
    { "full_and_shutdown", test_full_and_shutdown },
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: itest PATH-TO-PARLEYD [NAME...]\n");
        return 2;
    }
    parleyd = argv[1];
    /* A server that dies would make our writes raise SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        bool selected = argc == 2;
        for (int j = 2; j < argc; j++)
            selected = selected || strcmp(argv[j], tests[i].name) == 0;
        if (!selected)
            continue;
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
        } else {
            failed++;
            printf("FAIL %s (server log: %s.itest.log)\n", tests[i].name, parleyd);
        }
        fflush(stdout);
    }
    printf("itest: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
