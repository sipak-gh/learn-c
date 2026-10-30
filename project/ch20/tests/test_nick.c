/* test_nick.c - tests for nick.c. */
#include "nick.h"
#include "test.h"
#include "tests.h"

void test_nick_valid(void)
{
    CHECK(nick_valid("alice"));
    CHECK(nick_valid("a"));
    CHECK(nick_valid("Bob_2-x"));
    CHECK(nick_valid("abcdefghijklmnop"));       /* 16: the longest */
    CHECK(!nick_valid("abcdefghijklmnopq"));     /* 17 */
    CHECK(!nick_valid(""));
    CHECK(!nick_valid("2pac"));                  /* must start with a letter */
    CHECK(!nick_valid("_x"));
    CHECK(!nick_valid("al ice"));
    CHECK(!nick_valid("al:ice"));
    CHECK(!nick_valid("caf\xc3\xa9"));           /* UTF-8 bytes are not letters */
}

void test_nick_equal(void)
{
    CHECK(nick_equal("Alice", "alice"));
    CHECK(nick_equal("BOB", "bob"));
    CHECK(!nick_equal("alice", "alicia"));
}
