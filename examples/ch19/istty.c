/* istty.c - which of stdin, stdout and stderr is a terminal? */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    const char *names[] = { "stdin", "stdout", "stderr" };
    for (int fd = 0; fd < 3; fd++)
        fprintf(stderr, "fd %d (%s): %s\n", fd, names[fd],
                isatty(fd) ? "a terminal" : "not a terminal");
    return 0;
}
