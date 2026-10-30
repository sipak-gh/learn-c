/* offsets.c - two open() calls give two file offsets; dup() shares one. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/* Read 4 bytes from fd and print them. */
static void show(const char *what, int fd)
{
    char buf[5] = "";
    ssize_t n = read(fd, buf, 4);
    printf("%-14s fd %d read %zd: \"%s\"\n", what, fd, n, n > 0 ? buf : "");
}

int main(void)
{
    int a = open("offsets.c", O_RDONLY);
    int b = open("offsets.c", O_RDONLY);
    int c = dup(a);
    if (a < 0 || b < 0 || c < 0) {
        perror("offsets");
        return 1;
    }
    show("a", a);
    show("b (own open)", b);
    show("c = dup(a)", c);
    show("a again", a);
    printf("lseek(a, 0, SEEK_CUR) = %ld\n", (long)lseek(a, 0, SEEK_CUR));
    close(a);
    close(b);
    close(c);
    return 0;
}
