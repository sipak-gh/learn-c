/* readsizes.c - show how many bytes each read() returns. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0)
        fprintf(stderr, "read returned %zd\n", n);
    fprintf(stderr, "read returned %zd: %s\n", n, n == 0 ? "end of file" : "error");
    return n == 0 ? 0 : 1;
}
