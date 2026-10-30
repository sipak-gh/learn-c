/* parley.c - the parley chat client.
 * Version 3: on a terminal, a split-screen user interface (ui.c); when
 * stdin or stdout is not a terminal (or with -L), plain lines as before.
 * poll() waits for the keyboard (stdin) and the server at the same time. */
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <sys/socket.h>
#include <unistd.h>

#include "io.h"
#include "log.h"
#include "msg.h"
#include "net.h"
#include "sig.h"
#include "str.h"
#include "ui.h"
#include "version.h"

#define DEFAULT_PORT 6667
#define PORT_MAX 65535
/* Longest line we send as a message: the server adds ":nick " to it, and
 * that must still fit in MSG_LINE_MAX. */
#define INPUT_MAX 400

/* Settings from the command line. */
struct config {
    const char *host;
    long port;
    const char *nick;
    bool line_mode;
};

static void usage_error(const char *fmt, ...)
{
    fprintf(stderr, "parley: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\nTry 'parley --help' for more information.\n");
    exit(2);
}

static void print_help(void)
{
    printf("usage: parley [-h HOST] [-p PORT] [-n NICK] [-L]\n"
           "  -h HOST    server to connect to (default localhost)\n"
           "  -p PORT    server port (default %d)\n"
           "  -n NICK    your nickname (default guest)\n"
           "  -L         line mode: no full-screen interface\n"
           "  --help     show this help and exit\n"
           "  --version  show the version and exit\n",
           DEFAULT_PORT);
}

static void parse_args(int argc, char *argv[], struct config *cfg)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_help();
            exit(0);
        }
        if (strcmp(arg, "--version") == 0) {
            printf("parley " PARLEY_VERSION "\n");
            exit(0);
        }
        if (arg[0] != '-' || arg[1] == '\0' || arg[2] != '\0')
            usage_error("unexpected argument '%s'", arg);
        if (arg[1] == 'L') {
            cfg->line_mode = true;
            continue;
        }

        char opt = arg[1];
        if (opt != 'h' && opt != 'p' && opt != 'n')
            usage_error("unknown option '%s'", arg);
        if (i + 1 >= argc)
            usage_error("option -%c needs a value", opt);
        const char *value = argv[++i];

        switch (opt) {
        case 'h':
            cfg->host = value;
            break;
        case 'n':
            cfg->nick = value;
            break;
        case 'p':
            if (!str_to_long(value, 1, PORT_MAX, &cfg->port))
                usage_error("invalid port '%s' (expected 1-%d)", value, PORT_MAX);
            break;
        }
    }
}

/* The connection to the server. */
static int server_fd = -1;
static struct strbuf server_in;     /* received, not yet handled */
static bool server_skipping;        /* skipping a too-long line */
static char server_error[128];      /* the reason in the server's ERROR */

/* What the user typed, not yet handled. We read stdin with read(), not
 * with stdio: stdio might read ahead into its own buffer, and poll()
 * knows nothing about that buffer. */
static struct strbuf kbd;
static bool kbd_skipping;

static bool quitting = false;       /* we sent QUIT */
static bool show_time = false;      /* timestamps in front of chat lines */
static char my_nick[32] = "";       /* our nick, once the server has said so */
static char server_name[NET_ADDR_MAX + 8];   /* "host:port", for the status bar */
static bool ui_on = false;          /* the full-screen interface is running */

/* Print one line for the user, with the time in front if stamp is true
 * and show_time is on. */
static void vprint_line(bool stamp, const char *fmt, va_list ap)
{
    char line[UI_LINE_MAX];
    size_t len = 0;
    if (stamp && show_time) {
        time_t now = time(NULL);
        struct tm tm;
        if (localtime_r(&now, &tm) != NULL)
            len = strftime(line, sizeof line, "[%H:%M] ", &tm);
    }
    vsnprintf(line + len, sizeof line - len, fmt, ap);
    if (ui_on)
        ui_print(line);
    else
        printf("%s\n", line);
}

/* The status bar: who we are, where. */
static void update_status(void)
{
    char text[128];
    snprintf(text, sizeof text, " parley | %s @ %s",
             my_nick[0] != '\0' ? my_nick : "(not registered)", server_name);
    if (ui_on)
        ui_set_status(text);
}

/* Something that happened in the chat: gets a timestamp. */
static void show(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint_line(true, fmt, ap);
    va_end(ap);
}

/* A message from the client itself (help, usage hints): no timestamp. */
static void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint_line(false, fmt, ap);
    va_end(ap);
}

/* Send one protocol message to the server. */
static void send_msg(const struct msg *m)
{
    char line[MSG_LINE_MAX + 1];
    int n = msg_format(line, sizeof line, m);
    if (n > 0 && write_all(server_fd, line, (size_t)n) < 0)
        die("sending to server: %s", strerror(errno));
}

/* The last parameter: the text of most messages. */
static const char *last_param(const struct msg *m)
{
    if (m->trailing != NULL)
        return m->trailing;
    return m->nparams > 0 ? m->params[m->nparams - 1] : "";
}

/* Show one line from the server the way a human wants to read it. */
static void handle_server_line(char *line)
{
    char raw[MSG_LINE_MAX + 1];
    str_copy(raw, line, sizeof raw);      /* msg_parse cuts line up */
    struct msg m;
    if (msg_parse(line, &m) != MSG_OK)
        return;
    const char *from = m.prefix != NULL ? m.prefix : "?";
    const char *cmd = m.command;
    const char *text = last_param(&m);
    const char *p0 = msg_param(&m, 0);

    if (strcmp(cmd, "MSG") == 0) {
        show("<%s> %s", from, text);
    } else if (strcmp(cmd, "ME") == 0) {
        show("* %s %s", from, text);
    } else if (strcmp(cmd, "JOIN") == 0) {
        show("--> %s joined", from);
    } else if (strcmp(cmd, "QUIT") == 0) {
        show("<-- %s left (%s)", from, text);
    } else if (strcmp(cmd, "NICK") == 0 && p0 != NULL) {
        if (strcmp(from, my_nick) == 0) {
            str_copy(my_nick, p0, sizeof my_nick);
            update_status();
            show("-- You are now known as %s", p0);
        } else {
            show("-- %s is now known as %s", from, p0);
        }
    } else if (strcmp(cmd, "PING") == 0) {
        send_msg(&(struct msg){ .command = "PONG", .trailing = text });
    } else if (strcmp(cmd, "PONG") == 0 || strcmp(cmd, "366") == 0) {
        /* nothing to show */
    } else if (strcmp(cmd, "ERROR") == 0) {
        str_copy(server_error, text, sizeof server_error);
    } else if (strcmp(cmd, "001") == 0 && p0 != NULL) {
        str_copy(my_nick, p0, sizeof my_nick);
        update_status();
        show("-- %s", text);
    } else if (strcmp(cmd, "353") == 0) {
        show("-- Users: %s", text);
    } else if (cmd[0] == '4') {
        /* An error reply: ":parley 433 * bob :Nickname is already in use" */
        if (m.nparams >= 2)
            show("!! %s: %s", m.params[1], text);
        else
            show("!! %s", text);
    } else {
        show("?? %s", raw);
    }
}

static void cmd_nick(const char *arg)
{
    if (*arg == '\0') {
        note("-- usage: /nick NEWNICK");
        return;
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { arg } });
}

static void cmd_me(const char *arg)
{
    if (*arg == '\0') {
        note("-- usage: /me ACTION");
        return;
    }
    send_msg(&(struct msg){ .command = "ME", .trailing = arg });
}

static void cmd_who(const char *arg)
{
    (void)arg;   /* unused: every handler must have the same type */
    send_msg(&(struct msg){ .command = "NAMES" });
}

static void cmd_quit(const char *arg)
{
    /* "QUIT" alone, or "QUIT :reason" */
    send_msg(&(struct msg){ .command = "QUIT", .trailing = *arg != '\0' ? arg : NULL });
    quitting = true;
}

static void cmd_help(const char *arg);

/* The dispatch table: one struct per slash command. The table also holds
 * the help text, so /help can never get out of date. */
typedef void command_fn(const char *arg);

struct command {
    const char *name;
    const char *args;      /* argument summary for /help */
    const char *help;
    command_fn *run;
};

static const struct command commands[] = {
    { "nick", "NEWNICK",  "change your nickname",    cmd_nick },
    { "me",   "ACTION",   "describe an action",      cmd_me   },
    { "who",  "",         "list who is in the room", cmd_who  },
    { "quit", "[REASON]", "leave",                   cmd_quit },
    { "help", "",         "show this help",          cmd_help },
};
enum { NCOMMANDS = sizeof commands / sizeof commands[0] };

static void cmd_help(const char *arg)
{
    (void)arg;
    note("-- commands:");
    for (int i = 0; i < NCOMMANDS; i++) {
        char usage[32];
        snprintf(usage, sizeof usage, "/%s %s", commands[i].name, commands[i].args);
        note("--   %-15s %s", usage, commands[i].help);
    }
    note("-- anything else is sent as a message");
}

/* "/nick bob" -> run cmd_nick("bob"). Modifies line in place. */
static void run_command(char *line)
{
    char *name = line + 1;   /* skip the '/' */
    char *arg = name;
    while (*arg != '\0' && *arg != ' ')
        arg++;
    if (*arg == ' ')
        *arg++ = '\0';      /* end the name; arg now points after the space */

    for (int i = 0; i < NCOMMANDS; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].run(arg);     /* call through the function pointer */
            return;
        }
    }
    note("-- unknown command /%s (try /help)", name);
}

/* One line the user typed (without the newline). */
static void handle_input_line(char *line)
{
    if (strlen(line) > INPUT_MAX) {
        note("-- line too long (max %d bytes), not sent", INPUT_MAX);
        return;
    }
    if (line[0] == '\0')
        return;                    /* ignore empty lines */
    if (line[0] == '/')
        run_command(line);
    else
        send_msg(&(struct msg){ .command = "MSG", .trailing = line });
}

/* Leave: send QUIT (unless /quit did), then tell the server that we will
 * send nothing more. We keep reading until it closes the connection. */
static void leave(void)
{
    if (!quitting)
        send_msg(&(struct msg){ .command = "QUIT" });
    quitting = true;
    shutdown(server_fd, SHUT_WR);
}

/* Read what is waiting on stdin. Returns false at end of input. */
static bool read_keyboard(void)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(STDIN_FILENO, &kbd, 4096);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        die("reading input: %s", strerror(errno));
    }
    if (n == 0 && kbd.len > 0)
        sb_append(&kbd, "\n", 1);     /* a last line without a newline */

    enum frame_status st;
    while (!quitting && (st = msg_take_line(&kbd, &kbd_skipping, line)) != FRAME_NONE) {
        if (st == FRAME_TOO_LONG)
            note("-- line too long (max %d bytes), not sent", INPUT_MAX);
        else if (st == FRAME_BAD)
            note("-- line contains a NUL byte, not sent");
        else
            handle_input_line(line);
    }
    return n > 0;
}

/* Read keys typed in the full-screen interface. Returns false when the
 * user wants to leave. */
static bool read_keys(void)
{
    char keys[256];
    ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        die("reading input: %s", strerror(errno));
    }
    return n > 0 && ui_keys(keys, (size_t)n, handle_input_line);
}

/* Read what the server sent. Returns false when it closed the connection. */
static bool read_server(void)
{
    char line[MSG_LINE_MAX + 1];
    ssize_t n = read_into(server_fd, &server_in, 4096);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;
        str_copy(server_error, strerror(errno), sizeof server_error);
        return false;
    }
    enum frame_status st;
    while ((st = msg_take_line(&server_in, &server_skipping, line)) != FRAME_NONE) {
        if (st == FRAME_LINE)
            handle_server_line(line);
        else
            show("!! the server sent a bad line (ignored)");
    }
    return n > 0;
}

int main(int argc, char *argv[])
{
    struct config cfg = { .host = "localhost", .port = DEFAULT_PORT, .nick = "guest" };
    parse_args(argc, argv, &cfg);
    log_init("parley", false);
    if (sig_init() < 0)
        die("setting up signals: %s", strerror(errno));
    int wake_fd = sig_wake_fd();
    if (wake_fd < 0)
        die("setting up signals: %s", strerror(errno));
    /* Timestamps help a human reading a terminal, and get in the way when
     * the output goes to a file or another program. */
    show_time = isatty(STDOUT_FILENO);

    char err[256];
    server_fd = net_connect(cfg.host, (int)cfg.port, err, sizeof err);
    if (server_fd < 0)
        die("cannot connect to %s", err);
    snprintf(server_name, sizeof server_name, "%s:%ld", cfg.host, cfg.port);

    /* The full-screen interface needs a terminal on both sides. */
    if (!cfg.line_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (sig_watch_winch() < 0 || ui_start("", INPUT_MAX) < 0)
            die("setting up the terminal: %s", strerror(errno));
        ui_on = true;
        /* However we exit, give the user their terminal back: atexit()
         * runs ui_stop at exit(), die() runs it before printing. */
        atexit(ui_stop);
        log_on_die(ui_stop);
        update_status();
        note("-- type /help for help, /quit or Ctrl-D to leave");
    }
    send_msg(&(struct msg){ .command = "NICK", .nparams = 1, .params = { cfg.nick } });

    bool kbd_open = true;
    for (;;) {
        /* stdout is a pipe or file in scripts: then stdio buffers it, so
         * flush before we sleep in poll(). */
        if (ui_on)
            ui_flush();
        else
            fflush(stdout);
        struct pollfd fds[3] = {
            { .fd = server_fd, .events = POLLIN },
            { .fd = kbd_open && !quitting ? STDIN_FILENO : -1, .events = POLLIN },
            { .fd = wake_fd, .events = POLLIN },
        };
        if (poll(fds, 3, -1) < 0) {
            if (errno == EINTR)
                continue;
            die("poll: %s", strerror(errno));
        }
        if (fds[2].revents & POLLIN) {      /* a signal arrived */
            sig_drain(wake_fd);
            if (sig_winch) {                  /* the window changed size */
                sig_winch = 0;
                ui_resize();
            }
            if (sig_quit) {                   /* Ctrl-C (line mode) or SIGTERM */
                sig_quit = 0;
                if (quitting)
                    die("interrupted");       /* the second time: give up */
                leave();
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            if (!(ui_on ? read_keys() : read_keyboard())) {
                kbd_open = false;
                leave();                  /* end of input, Ctrl-D or Ctrl-C */
            }
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (!read_server())
                break;
        }
    }

    ui_stop();                            /* back to the normal screen */
    ui_on = false;
    if (!quitting) {                      /* the server hung up on us */
        show("-- Disconnected: %s",
             server_error[0] != '\0' ? server_error : "Connection closed");
    }
    close(server_fd);
    close(wake_fd);
    sb_free(&server_in);
    sb_free(&kbd);
    if (fflush(stdout) == EOF)
        die("writing: %s", strerror(errno));
    return quitting ? 0 : 1;
}
