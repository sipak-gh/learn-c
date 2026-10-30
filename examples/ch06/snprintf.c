/* snprintf.c - bounded formatting, and how to detect truncation. */
#include <stdio.h>

int main(void)
{
    char buf[8];
    const char *names[] = { "bob", "parley-server" };

    for (int i = 0; i < 2; i++) {
        int n = snprintf(buf, sizeof buf, "%s", names[i]);
        if (n < 0) {
            fprintf(stderr, "snprintf: encoding error\n");
            return 1;
        }
        printf("wanted %2d bytes, got \"%s\"%s\n", n, buf,
               (size_t)n >= sizeof buf ? " (truncated)" : "");
    }

    /* Size 0 writes nothing: a way to measure first. */
    int need = snprintf(NULL, 0, ":%s MSG :%s", "alice", "hello there");
    printf("the reply needs %d bytes + 1 for '\\0'\n", need);
    return 0;
}
