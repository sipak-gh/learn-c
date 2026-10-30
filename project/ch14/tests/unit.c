/* unit.c - the unit-test runner.
 *
 *     unit            run every test
 *     unit -v         ... and list each test as it runs
 *     unit NAME...    run only the tests whose name starts with NAME
 *
 * Everything goes to stdout, so failures and results stay in order.
 * Exit status: 0 if every check passed, 1 otherwise.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "test.h"
#include "tests.h"

struct test {
    const char *name;
    void (*run)(void);
};

#define TEST_ENTRY(name) { #name, test_##name },
static const struct test tests[] = { TESTS(TEST_ENTRY) };
#undef TEST_ENTRY

#define NTESTS (sizeof tests / sizeof tests[0])

static int failed_checks;    /* failed checks in the current test */

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

/* Should the test called name run, given the command-line patterns? */
static bool selected(const char *name, int npatterns, char **patterns)
{
    if (npatterns == 0)
        return true;
    for (int i = 0; i < npatterns; i++)
        if (strncmp(name, patterns[i], strlen(patterns[i])) == 0)
            return true;
    return false;
}

int main(int argc, char *argv[])
{
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int first = verbose ? 2 : 1;
    int passed = 0, failed = 0;

    for (size_t i = 0; i < NTESTS; i++) {
        if (!selected(tests[i].name, argc - first, argv + first))
            continue;
        failed_checks = 0;
        tests[i].run();
        if (failed_checks == 0) {
            passed++;
            if (verbose)
                printf("ok   %s\n", tests[i].name);
        } else {
            failed++;
            printf("FAIL %s\n", tests[i].name);
        }
        fflush(stdout);    /* if the next test crashes, this is already out */
    }
    printf("unit: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
