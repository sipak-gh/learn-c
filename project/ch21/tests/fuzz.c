/* fuzz.c - throw random input at the protocol code.
 *
 *     fuzz [ROUNDS [SEED]]      (default: 20000 rounds, seed 1)
 *
 * Unit tests check the cases we thought of; a fuzzer finds the ones we
 * didn't. Each round makes up a random line and checks properties that
 * must hold for every input:
 *   - msg_parse() returns a valid status, and on MSG_OK every part is
 *     sane and points into the line;
 *   - formatting a parsed message and parsing it again gives the same parts;
 *   - framing a random stream gives the same lines however it is split.
 * rand() with a fixed seed makes every run the same: a failure can be
 * reproduced with the seed it prints. Run it under ASan (make SANITIZE=1
 * test) so that out-of-bounds reads are caught too.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msg.h"
#include "str.h"
#include "strbuf.h"

static unsigned long failures;

#define FAIL(...)                                                       \
    do {                                                                \
        failures++;                                                     \
        printf("fuzz: ");                                               \
        printf(__VA_ARGS__);                                            \
        putchar('\n');                                                  \
    } while (0)

static int random_below(int n)
{
    return rand() % n;
}

/* A random byte, mostly ones that mean something to the parser. NUL,
 * '\r' and '\n' never appear: lines come out of the framer without them. */
static char random_byte(void)
{
    static const char interesting[] = "  ::abcNICKMSG01";
    if (random_below(4) > 0)
        return interesting[random_below((int)sizeof interesting - 1)];
    int c;
    do
        c = 1 + random_below(255);
    while (c == '\r' || c == '\n');
    return (char)c;
}

static void random_line(char *line, size_t len)
{
    for (size_t i = 0; i < len; i++)
        line[i] = random_byte();
    line[len] = '\0';
}

static bool points_into(const char *p, const char *buf, size_t size)
{
    return p >= buf && p < buf + size;
}

/* Check the parts of a parsed message. */
static void check_parts(const struct msg *m, const char *buf, size_t size, const char *input)
{
    if (m->command == NULL || m->command[0] == '\0' || !points_into(m->command, buf, size))
        FAIL("bad command for \"%s\"", input);
    if (m->prefix != NULL && !points_into(m->prefix, buf, size))
        FAIL("bad prefix for \"%s\"", input);
    if (m->trailing != NULL && !points_into(m->trailing, buf, size))
        FAIL("bad trailing for \"%s\"", input);
    if (m->nparams < 0 || m->nparams > MSG_PARAMS_MAX) {
        FAIL("nparams %d for \"%s\"", m->nparams, input);
        return;
    }
    for (int i = 0; i < m->nparams; i++) {
        const char *p = m->params[i];
        if (!points_into(p, buf, size) || p[0] == '\0' || p[0] == ':' || strchr(p, ' ') != NULL)
            FAIL("bad parameter %d for \"%s\"", i, input);
    }
}

static bool same(const char *a, const char *b)
{
    return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0;
}

/* Parse a random line; if that works, format and parse it again. */
static void fuzz_parse(void)
{
    char input[MSG_LINE_MAX], line[MSG_LINE_MAX], out[MSG_LINE_MAX + 1];
    random_line(input, (size_t)random_below(400));
    str_copy(line, input, sizeof line);

    struct msg m;
    enum msg_status st = msg_parse(line, &m);
    if (st != MSG_OK) {
        if (st != MSG_EMPTY && st != MSG_NO_COMMAND && st != MSG_TOO_MANY_PARAMS)
            FAIL("status %d for \"%s\"", (int)st, input);
        return;
    }
    check_parts(&m, line, sizeof line, input);

    int len = msg_format(out, sizeof out, &m);
    if (len < 2 || out[len - 2] != '\r' || out[len - 1] != '\n') {
        FAIL("cannot format the parts of \"%s\"", input);
        return;
    }
    out[len - 2] = '\0';
    struct msg again;
    if (msg_parse(out, &again) != MSG_OK) {
        FAIL("\"%s\" became \"%s\", which does not parse", input, out);
        return;
    }
    /* Compare the parts. "NICK bob" and "NICK :bob" mean the same, so
     * compare with msg_param(), which counts the trailing one too. */
    int total = m.nparams + (m.trailing != NULL);
    int total_again = again.nparams + (again.trailing != NULL);
    bool ok = same(m.prefix, again.prefix) && same(m.command, again.command)
              && total == total_again;
    for (int i = 0; ok && i < total; i++)
        ok = same(msg_param(&m, i), msg_param(&again, i));
    if (!ok)
        FAIL("\"%s\" became \"%s\", which parses differently", input, out);
}

/* What the framer must do with one line of the given length and ending,
 * and whether it contains a NUL. */
static enum frame_status expected(size_t len, size_t ending, bool nul)
{
    if (len + ending > MSG_LINE_MAX)
        return FRAME_TOO_LONG;
    return nul ? FRAME_BAD : FRAME_LINE;
}

/* Build a stream of random lines (some too long, some with a NUL), feed it
 * to the framer in random pieces, and compare with what each line should
 * give. */
static void fuzz_frame(void)
{
    enum { LINES = 8 };
    static char stream[LINES * 700];
    static char lines[LINES][700];
    enum frame_status want[LINES];
    size_t len = 0;

    for (int i = 0; i < LINES; i++) {
        size_t n = (size_t)(random_below(3) == 0 ? random_below(700) : random_below(60));
        bool nul = n > 0 && random_below(8) == 0;
        size_t ending = (size_t)(1 + random_below(2));          /* "\n" or "\r\n" */
        random_line(lines[i], n);
        memcpy(stream + len, lines[i], n);
        if (nul)
            stream[len + (size_t)random_below((int)n)] = '\0';
        len += n;
        memcpy(stream + len, ending == 2 ? "\r\n" : "\n", ending);
        len += ending;
        want[i] = expected(n, ending, nul);
    }

    struct strbuf in = { 0 };
    bool skipping = false;
    char line[MSG_LINE_MAX + 1];
    int got = 0;
    for (size_t pos = 0; pos < len;) {
        size_t piece = (size_t)(1 + random_below(random_below(2) ? 4 : 600));
        if (piece > len - pos)
            piece = len - pos;
        if (sb_append(&in, stream + pos, piece) < 0) {
            FAIL("out of memory");
            break;
        }
        pos += piece;
        enum frame_status st;
        while ((st = msg_take_line(&in, &skipping, line)) != FRAME_NONE) {
            if (got >= LINES) {
                FAIL("more lines than were sent");
                break;
            }
            if (st != want[got])
                FAIL("line %d: status %d, expected %d", got, (int)st, (int)want[got]);
            else if (st == FRAME_LINE && strcmp(line, lines[got]) != 0)
                FAIL("line %d: got \"%s\", expected \"%s\"", got, line, lines[got]);
            got++;
        }
    }
    if (got != LINES)
        FAIL("%d lines out, %d in", got, LINES);
    sb_free(&in);
}

int main(int argc, char *argv[])
{
    long rounds = 20000, seed = 1;
    if ((argc > 1 && !str_to_long(argv[1], 1, 100000000, &rounds))
        || (argc > 2 && !str_to_long(argv[2], 0, 2147483647, &seed)) || argc > 3) {
        fprintf(stderr, "usage: fuzz [ROUNDS [SEED]]\n");
        return 2;
    }
    srand((unsigned)seed);
    for (long i = 0; i < rounds && failures < 10; i++) {
        fuzz_parse();
        fuzz_frame();
    }
    printf("fuzz: %ld rounds (seed %ld), %lu failed\n", rounds, seed, failures);
    return failures == 0 ? 0 : 1;
}
