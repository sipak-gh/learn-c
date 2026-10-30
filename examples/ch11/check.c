/* check.c - a tiny test framework: a CHECK macro, a table of tests and a
 * runner that reports every failed check and sets the exit status. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failed_checks;    /* failed checks in the current test */

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);    \
            failed_checks++;                                            \
        }                                                               \
    } while (0)

/* Does s start with prefix? (This version has a bug.) */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(s)) == 0;
}

static void test_starts_with_yes(void)
{
    CHECK(starts_with("NICK al", "NICK"));
    CHECK(starts_with("NICK", "NICK"));
    CHECK(starts_with("MSG", ""));
}

static void test_starts_with_no(void)
{
    CHECK(!starts_with("NI", "NICK"));
    CHECK(!starts_with("", "NICK"));
    CHECK(!starts_with("MSG :hi", "NICK"));
}

static const struct test {
    const char *name;
    void (*run)(void);
} tests[] = {
    { "starts_with_yes", test_starts_with_yes },
    { "starts_with_no", test_starts_with_no },
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
    printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
