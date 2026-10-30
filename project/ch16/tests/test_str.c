/* test_str.c - tests for str.c. */
#include "str.h"
#include "test.h"
#include "tests.h"

void test_str_copy_fits(void)
{
    char buf[8];
    CHECK_INT(str_copy(buf, "hello", sizeof buf), 5);
    CHECK_STR(buf, "hello");
    CHECK_INT(str_copy(buf, "", sizeof buf), 0);
    CHECK_STR(buf, "");
}

void test_str_copy_truncates(void)
{
    char buf[4] = "xyz";
    /* The result is the length it wanted: >= size means truncated. */
    CHECK_INT(str_copy(buf, "hello", sizeof buf), 5);
    CHECK_STR(buf, "hel");
    CHECK_INT(str_copy(buf, "abc", 0), 3);   /* size 0: writes nothing */
    CHECK_STR(buf, "hel");
}

void test_str_ieq(void)
{
    CHECK(str_ieq("NICK", "nick"));
    CHECK(str_ieq("", ""));
    CHECK(!str_ieq("NICK", "NICKS"));
    CHECK(!str_ieq("NICKS", "NICK"));
    CHECK(!str_ieq("a", "b"));
}

void test_str_chomp(void)
{
    char a[] = "line\r\n", b[] = "line\n", c[] = "\r\n", d[] = "no newline";
    str_chomp(a);
    str_chomp(b);
    str_chomp(c);
    str_chomp(d);
    CHECK_STR(a, "line");
    CHECK_STR(b, "line");
    CHECK_STR(c, "");
    CHECK_STR(d, "no newline");
}

/* A table-driven test: one row per case, one loop to check them all. */
void test_str_to_long(void)
{
    static const struct {
        const char *input;
        bool ok;
        long value;
    } cases[] = {
        { "6667", true, 6667 },
        { "1", true, 1 },
        { "65535", true, 65535 },
        { "+80", true, 80 },              /* strtol allows a sign ... */
        { " 80", true, 80 },              /* ... and leading spaces */
        { "0", false, 0 },                /* below min */
        { "65536", false, 0 },            /* above max */
        { "-1", false, 0 },
        { "", false, 0 },                 /* no digits */
        { "80x", false, 0 },              /* trailing junk */
        { "0x50", false, 0 },             /* base 10 only */
        { "99999999999999999999999", false, 0 },   /* ERANGE */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        long value = 0;
        bool ok = str_to_long(cases[i].input, 1, 65535, &value);
        CHECK_INT(ok, cases[i].ok);
        CHECK_INT(value, cases[i].value);   /* untouched (0) on failure */
    }
}
