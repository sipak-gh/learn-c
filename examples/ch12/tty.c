/* tty.c - which of the standard descriptors are terminals? */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    for (int fd = 0; fd <= 2; fd++)
        fprintf(stderr, "fd %d: %s\n", fd, isatty(fd) ? "terminal" : "not a terminal");
    return 0;
}
