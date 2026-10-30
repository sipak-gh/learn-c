/* tests.h - the list of every unit test, as an X-macro.
 *
 * To add a test: write void test_NAME(void) in a test_*.c file and add
 * X(NAME) here. unit.c expands this list into a table, and the line below
 * declares every test function, so a typo is a compile error.
 */
#ifndef PARLEY_TESTS_H
#define PARLEY_TESTS_H

#define TESTS(X)                    \
    X(str_copy_fits)                \
    X(str_copy_truncates)           \
    X(str_ieq)                      \
    X(str_chomp)                    \
    X(str_to_long)                  \
    X(nick_valid)                   \
    X(nick_equal)                   \
    X(strbuf_append)                \
    X(strbuf_appendf)               \
    X(strbuf_consume)               \
    X(strbuf_binary)                \
    X(msg_parse_table)              \
    X(msg_parse_errors)             \
    X(msg_param)                    \
    X(msg_format)                   \
    X(msg_format_truncates)         \
    X(msg_roundtrip)                \
    X(frame_lines)                  \
    X(frame_one_byte_at_a_time)     \
    X(frame_too_long)

#define TEST_DECLARE(name) void test_##name(void);
TESTS(TEST_DECLARE)

#endif
