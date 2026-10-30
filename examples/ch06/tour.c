/* tour.c - the most used functions in <string.h>. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *line = "MSG #chat :hello, world";

    /* length and comparison */
    printf("strlen            = %zu\n", strlen(line));
    printf("strcmp(a, b)      = %d\n", strcmp("alice", "bob") < 0 ? -1 : 1);
    printf("strncmp 3 bytes   = %d\n", strncmp(line, "MSGX", 3));

    /* searching: each returns a pointer into line, or NULL */
    const char *colon = strchr(line, ':');
    printf("strchr ':'        -> \"%s\" (index %td)\n", colon, colon - line);
    printf("strrchr 'o'       -> \"%s\"\n", strrchr(line, 'o'));
    printf("strstr \"world\"    -> \"%s\"\n", strstr(line, "world"));
    printf("strstr \"bob\"      -> %s\n", strstr(line, "bob") ? "found" : "NULL");

    /* spans: how many leading bytes are (not) in a set */
    printf("strspn  upper     = %zu\n", strspn(line, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
    printf("strcspn \" \"       = %zu\n", strcspn(line, " "));

    /* raw memory: works on bytes, '\0' is not special */
    char buf[16];
    memset(buf, '-', sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    memcpy(buf, "abc", 3);              /* no '\0' copied */
    printf("memset+memcpy     = %s\n", buf);
    memmove(buf + 2, buf, 5);           /* overlapping: must be memmove */
    printf("memmove           = %s\n", buf);
    printf("memcmp            = %d\n", memcmp("ab\0x", "ab\0y", 4) < 0 ? -1 : 1);
    return 0;
}
