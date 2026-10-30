/* nicks.c - a small program with a crash, for gdb practice. */
#include <stdio.h>
#include <string.h>

/* Return the parameter of a "COMMAND param" line. */
static const char *param(const char *line)
{
    const char *sp = strchr(line, ' ');
    return sp + 1;                    /* BUG: sp is NULL when there is no space */
}

static int total_len(const char *lines[], int n)
{
    int total = 0;

    for (int i = 0; i < n; i++) {
        const char *p = param(lines[i]);
        total += (int)strlen(p);
    }
    return total;
}

int main(void)
{
    const char *lines[] = { "NICK alice", "JOIN #c", "QUIT", "NICK bob" };

    printf("total = %d\n", total_len(lines, 4));
    return 0;
}
