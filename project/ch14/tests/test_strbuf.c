/* test_strbuf.c - tests for strbuf.c. */
#include "strbuf.h"
#include "test.h"
#include "tests.h"

void test_strbuf_append(void)
{
    struct strbuf sb = { 0 };          /* zeroed: a valid empty buffer */
    CHECK_INT(sb.len, 0);
    CHECK_INT(sb_append(&sb, "hello", 5), 0);
    CHECK_INT(sb_append(&sb, ", world", 7), 0);
    CHECK_INT(sb.len, 12);
    CHECK(sb.cap > sb.len);            /* room for the '\0' */
    CHECK_STR(sb.data, "hello, world");

    /* Grow well past the first allocation. */
    for (int i = 0; i < 1000; i++)
        CHECK_INT(sb_append(&sb, "x", 1), 0);
    CHECK_INT(sb.len, 1012);
    CHECK_INT(sb.data[1012], '\0');
    sb_free(&sb);
    CHECK(sb.data == NULL);
    CHECK_INT(sb.len, 0);
}

void test_strbuf_appendf(void)
{
    struct strbuf sb = { 0 };
    CHECK_INT(sb_appendf(&sb, "%s has %d clients", "parley", 3), 0);
    CHECK_STR(sb.data, "parley has 3 clients");
    CHECK_INT(sb_appendf(&sb, "%s", ""), 0);
    CHECK_INT(sb.len, 20);
    sb_free(&sb);
}

void test_strbuf_consume(void)
{
    struct strbuf sb = { 0 };
    sb_consume(&sb, 5);                /* consuming from empty is fine */
    sb_append(&sb, "NICK al\r\nMSG", 12);
    sb_consume(&sb, 9);
    CHECK_STR(sb.data, "MSG");
    CHECK_INT(sb.len, 3);
    sb_consume(&sb, 100);              /* more than len: empties it */
    CHECK_STR(sb.data, "");
    sb_append(&sb, "again", 5);
    sb_clear(&sb);
    CHECK_INT(sb.len, 0);
    CHECK(sb.cap > 0);                 /* clear keeps the memory */
    sb_free(&sb);
}

void test_strbuf_binary(void)
{
    struct strbuf sb = { 0 };
    sb_append(&sb, "a\0b", 3);         /* bytes, not just strings */
    CHECK_INT(sb.len, 3);
    CHECK_INT(sb.data[1], '\0');
    CHECK_INT(sb.data[2], 'b');
    sb_free(&sb);
}
