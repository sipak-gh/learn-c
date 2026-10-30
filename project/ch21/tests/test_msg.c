/* test_msg.c - tests for msg.c: parsing, formatting and framing. */
#include "msg.h"
#include "test.h"
#include "tests.h"

void test_msg_parse_table(void)
{
    static const struct {
        const char *line;
        const char *prefix, *command;
        int nparams;
        const char *param0, *param1, *trailing;
    } cases[] = {
        { "NICK alice",              NULL,     "NICK", 1, "alice", NULL, NULL },
        { "MSG :hello there",        NULL,     "MSG",  0, NULL, NULL, "hello there" },
        { ":alice MSG :hi",          "alice",  "MSG",  0, NULL, NULL, "hi" },
        { ":parley 001 al :Welcome", "parley", "001",  1, "al", NULL, "Welcome" },
        { "  PING   a  b ",          NULL,     "PING", 2, "a",  "b",  NULL },
        { "MSG :",                   NULL,     "MSG",  0, NULL, NULL, "" },
        { "MSG ::-)",                NULL,     "MSG",  0, NULL, NULL, ":-)" },
        { "MSG :a  :b",              NULL,     "MSG",  0, NULL, NULL, "a  :b" },
        { "QUIT",                    NULL,     "QUIT", 0, NULL, NULL, NULL },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char line[MSG_LINE_MAX + 1];
        struct msg m;
        strcpy(line, cases[i].line);       /* msg_parse modifies its input */
        CHECK_INT(msg_parse(line, &m), MSG_OK);
        CHECK_STR(m.prefix, cases[i].prefix);
        CHECK_STR(m.command, cases[i].command);
        CHECK_INT(m.nparams, cases[i].nparams);
        CHECK_STR(msg_param(&m, 0), cases[i].nparams > 0 ? cases[i].param0 : cases[i].trailing);
        if (m.nparams > 1)
            CHECK_STR(m.params[1], cases[i].param1);
        CHECK_STR(m.trailing, cases[i].trailing);
    }
}

void test_msg_parse_errors(void)
{
    char empty[] = "", spaces[] = "   ", prefix_only[] = ":alice", colon[] = ": MSG";
    char many[] = "X 1 2 3 4 5 6 7 8 9";
    char eight[] = "X 1 2 3 4 5 6 7 8 :trailing is fine";
    struct msg m;
    CHECK_INT(msg_parse(empty, &m), MSG_EMPTY);
    CHECK_INT(msg_parse(spaces, &m), MSG_EMPTY);
    CHECK_INT(msg_parse(prefix_only, &m), MSG_NO_COMMAND);
    CHECK_INT(msg_parse(colon, &m), MSG_NO_COMMAND);
    CHECK_INT(msg_parse(many, &m), MSG_TOO_MANY_PARAMS);
    CHECK_INT(msg_parse(eight, &m), MSG_OK);
    CHECK_INT(m.nparams, MSG_PARAMS_MAX);
}

void test_msg_param(void)
{
    char a[] = "NICK bob", b[] = "NICK :bob", c[] = "PING x :y";
    struct msg m;
    msg_parse(a, &m);
    CHECK_STR(msg_param(&m, 0), "bob");
    CHECK_STR(msg_param(&m, 1), NULL);
    msg_parse(b, &m);
    CHECK_STR(msg_param(&m, 0), "bob");    /* trailing counts as a param */
    msg_parse(c, &m);
    CHECK_STR(msg_param(&m, 0), "x");
    CHECK_STR(msg_param(&m, 1), "y");
    CHECK_STR(msg_param(&m, 2), NULL);
}

void test_msg_format(void)
{
    char out[MSG_LINE_MAX + 1];
    struct msg m = { .prefix = "parley", .command = "001", .nparams = 1,
                     .params = { "al" }, .trailing = "Welcome to parley, al" };
    CHECK_INT(msg_format(out, sizeof out, &m), 39);
    CHECK_STR(out, ":parley 001 al :Welcome to parley, al\r\n");

    struct msg quit = { .command = "QUIT" };
    CHECK_INT(msg_format(out, sizeof out, &quit), 6);
    CHECK_STR(out, "QUIT\r\n");

    struct msg empty = { .command = "MSG", .trailing = "" };
    msg_format(out, sizeof out, &empty);
    CHECK_STR(out, "MSG :\r\n");
}

void test_msg_format_truncates(void)
{
    char text[600], out[MSG_LINE_MAX + 1];
    memset(text, 'x', sizeof text - 1);
    text[sizeof text - 1] = '\0';

    /* A long trailing parameter is cut so the line is exactly 512 bytes. */
    struct msg m = { .prefix = "al", .command = "MSG", .trailing = text };
    CHECK_INT(msg_format(out, sizeof out, &m), MSG_LINE_MAX);
    CHECK_INT(strlen(out), MSG_LINE_MAX);
    CHECK_STR(out + MSG_LINE_MAX - 2, "\r\n");

    /* A small buffer cuts it further, still ending in "\r\n". */
    char small[16];
    CHECK_INT(msg_format(small, sizeof small, &m), 15);
    CHECK_STR(small, ":al MSG :xxxx\r\n");

    /* If even the command doesn't fit, that is an error. */
    struct msg cmd = { .command = text };
    CHECK_INT(msg_format(out, sizeof out, &cmd), -1);
}

void test_msg_roundtrip(void)
{
    char out[MSG_LINE_MAX + 1];
    struct msg m = { .prefix = "bob", .command = "NICK", .nparams = 1, .params = { "robert" } };
    int n = msg_format(out, sizeof out, &m);
    CHECK_INT(n, 18);
    out[n - 2] = '\0';                     /* drop "\r\n" before parsing */

    struct msg back;
    CHECK_INT(msg_parse(out, &back), MSG_OK);
    CHECK_STR(back.prefix, "bob");
    CHECK_STR(back.command, "NICK");
    CHECK_STR(msg_param(&back, 0), "robert");
}

/* Feed text into a framer in pieces of size step, collecting what comes out
 * as "line|line|" ("!" for a too-long line, "?" for a bad one) in got[size]. */
static void frame_all(const char *text, size_t step, char *got, size_t size)
{
    struct strbuf in = { 0 };
    bool skipping = false;
    char line[MSG_LINE_MAX + 1];
    size_t len = strlen(text);
    got[0] = '\0';

    for (size_t pos = 0; pos < len; pos += step) {
        size_t n = len - pos < step ? len - pos : step;
        sb_append(&in, text + pos, n);
        enum frame_status st;
        while ((st = msg_take_line(&in, &skipping, line)) != FRAME_NONE) {
            if (strlen(got) + strlen(line) + 2 >= size)
                break;
            strcat(got, st == FRAME_LINE ? line : st == FRAME_TOO_LONG ? "!" : "?");
            strcat(got, "|");
        }
    }
    sb_free(&in);
}

void test_frame_lines(void)
{
    char got[256];
    frame_all("NICK al\r\nMSG :hi\nQUIT\r\n", 1000, got, sizeof got);
    CHECK_STR(got, "NICK al|MSG :hi|QUIT|");
    frame_all("\r\n\nPING x\r\npartial", 1000, got, sizeof got);
    CHECK_STR(got, "||PING x|");              /* "partial" waits for more */
}

/* TCP may deliver a line in any number of pieces, even one byte at a time.
 * The framer must give exactly the same lines for every split. */
void test_frame_one_byte_at_a_time(void)
{
    const char *text = "NICK al\r\nMSG :one byte\r\nQUIT :bye\r\n";
    char whole[256], got[256];
    frame_all(text, 1000, whole, sizeof whole);
    CHECK_STR(whole, "NICK al|MSG :one byte|QUIT :bye|");
    for (size_t step = 1; step <= 10; step++) {
        frame_all(text, step, got, sizeof got);
        CHECK_STR(got, whole);
    }
}

void test_frame_too_long(void)
{
    static char text[2000];
    char got[MSG_LINE_MAX + 100];

    /* 600 x's: too long. The framer reports it once, skips the rest of it
     * (even if it arrives later), and then works normally again. */
    strcpy(text, "MSG :");
    memset(text + 5, 'x', 600);
    strcpy(text + 605, "\r\nMSG :ok\r\n");
    for (size_t step = 1; step <= 700; step += 233) {
        frame_all(text, step, got, sizeof got);
        CHECK_STR(got, "!|MSG :ok|");
    }

    /* Exactly 512 bytes with "\r\n" is allowed; 513 is not. */
    memset(text, 'y', 510);
    strcpy(text + 510, "\r\n");
    frame_all(text, 64, got, sizeof got);
    CHECK_INT(strlen(got), 511);                 /* 510 y's and "|" */
    memset(text, 'y', 511);
    strcpy(text + 511, "\r\nPING z\n");
    frame_all(text, 64, got, sizeof got);
    CHECK_STR(got, "!|PING z|");
}

/* A string literal can't hold a NUL for frame_all() (it uses strlen), so
 * this test feeds the buffer itself. */
void test_frame_nul(void)
{
    static const char text[] = "NICK al\r\nMSG :a\0b\r\nQUIT\r\n";
    struct strbuf in = { 0 };
    bool skipping = false;
    char line[MSG_LINE_MAX + 1];

    sb_append(&in, text, sizeof text - 1);
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_LINE);
    CHECK_STR(line, "NICK al");
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_BAD);
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_LINE);
    CHECK_STR(line, "QUIT");
    CHECK_INT(msg_take_line(&in, &skipping, line), FRAME_NONE);
    sb_free(&in);
}
