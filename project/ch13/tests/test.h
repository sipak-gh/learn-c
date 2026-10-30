/* test.h - a tiny unit-test framework: check macros.
 *
 * A failed check prints "file:line: what went wrong" and marks the current
 * test as failed, then the test carries on (so one run shows every failed
 * check). The macros are wrapped in do { ... } while (0) so they act like a
 * single statement, even after an if without braces.
 */
#ifndef PARLEY_TEST_H
#define PARLEY_TEST_H

#include <string.h>

/* Report a failed check (defined in unit.c). */
void test_fail(const char *file, int line, const char *fmt, ...);

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond))                                                    \
            test_fail(__FILE__, __LINE__, "CHECK(%s)", #cond);          \
    } while (0)

/* Compare two integers; both are evaluated exactly once. */
#define CHECK_INT(a, b)                                                 \
    do {                                                                \
        long long check_a_ = (a), check_b_ = (b);                       \
        if (check_a_ != check_b_)                                       \
            test_fail(__FILE__, __LINE__, "%s == %s: %lld != %lld",     \
                      #a, #b, check_a_, check_b_);                      \
    } while (0)

/* Compare two strings; either may be NULL (NULL only equals NULL). */
#define CHECK_STR(a, b)                                                 \
    do {                                                                \
        const char *check_a_ = (a), *check_b_ = (b);                    \
        if (check_a_ == NULL || check_b_ == NULL                        \
                ? check_a_ != check_b_                                  \
                : strcmp(check_a_, check_b_) != 0)                      \
            test_fail(__FILE__, __LINE__, "%s == %s: \"%s\" != \"%s\"", \
                      #a, #b, check_a_ ? check_a_ : "(null)",           \
                      check_b_ ? check_b_ : "(null)");                  \
    } while (0)

#endif
