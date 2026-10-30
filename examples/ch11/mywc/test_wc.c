/* test_wc.c - unit tests for wc.c. */
#include <stdio.h>
#include <string.h>

#include "wc.h"

static int failed_checks;

#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long a_ = (a), b_ = (b);                                   \
        if (a_ != b_) {                                                 \
            printf("%s:%d: %s == %s: %lld != %lld\n",                   \
                   __FILE__, __LINE__, #a, #b, a_, b_);                 \
            failed_checks++;                                            \
        }                                                               \
    } while (0)

/* The test cases: one row per input. */
static const struct {
    const char *text;
    long lines, words, bytes;
} cases[] = {
    { "",                          0, 0, 0 },
    { "hello\n",                   1, 1, 6 },
    { "hello world\n",             1, 2, 12 },
    { "  two   spaces  \n",        1, 2, 17 },
    { "no newline",                0, 2, 10 },
    { "a\tb\nc\r\n",               2, 3, 7 },
    { "\n\n\n",                    3, 0, 3 },
    { "NICK al\r\nMSG :hi there\r\n", 2, 5, 24 },
};
#define NCASES (sizeof cases / sizeof cases[0])

/* Count text, feeding it to wc_feed in pieces of at most step bytes. */
static struct wc count(const char *text, size_t step)
{
    struct wc w;
    size_t len = strlen(text);
    wc_init(&w);
    for (size_t pos = 0; pos < len; pos += step)
        wc_feed(&w, text + pos, len - pos < step ? len - pos : step);
    return w;
}

/* Count case i in pieces of step bytes and check the counts. A failure
 * also says which case and step it was: the line number alone is the same
 * for every row of the table. */
static void check_case(size_t i, size_t step)
{
    int before = failed_checks;
    struct wc w = count(cases[i].text, step);
    CHECK_INT(w.lines, cases[i].lines);
    CHECK_INT(w.words, cases[i].words);
    CHECK_INT(w.bytes, cases[i].bytes);
    if (failed_checks > before)
        printf("    (case %zu, step %zu)\n", i, step);
}

/* Table-driven: every row, all in one piece. */
static void test_table(void)
{
    for (size_t i = 0; i < NCASES; i++)
        check_case(i, 1000);
}

/* Every row again, cut into pieces of every size from 1 to 5 bytes.
 * The counts must not depend on where the pieces end. */
static void test_pieces(void)
{
    for (size_t i = 0; i < NCASES; i++)
        for (size_t step = 1; step <= 5; step++)
            check_case(i, step);
}

static const struct test {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "table", test_table },
    { "pieces", test_pieces },
};

int main(void)
{
    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
        } else {
            failed++;
            printf("FAIL %s\n", tests[i].name);
        }
    }
    printf("test_wc: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
