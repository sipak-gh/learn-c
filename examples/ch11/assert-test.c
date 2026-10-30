/* assert-test.c - testing with assert(): it works, but badly. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Does s start with prefix? (This version has a bug.) */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(s)) == 0;
}

int main(void)
{
    assert(starts_with("NICK al", "NICK"));
    assert(!starts_with("NI", "NICK"));
    assert(!starts_with("", "NICK"));
    assert(starts_with("MSG", ""));
    puts("all tests passed");
    return 0;
}
