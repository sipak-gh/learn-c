/* fds.c - file descriptors are small integers: 0, 1 and 2 are open at
 * start, and open() returns the lowest number that is free. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *out = "to fd 1 (stdout)\n", *err = "to fd 2 (stderr)\n";
    if (write(STDOUT_FILENO, out, strlen(out)) < 0)
        perror("write");
    if (write(STDERR_FILENO, err, strlen(err)) < 0)
        perror("write");

    int a = open("fds.c", O_RDONLY);
    int b = open("fds.c", O_RDONLY);
    printf("open gave %d and %d\n", a, b);

    close(a);
    int c = open("/dev/null", O_WRONLY);
    printf("after close(%d), open gave %d\n", a, c);

    printf("fileno(stdin)=%d fileno(stdout)=%d fileno(stderr)=%d\n",
           fileno(stdin), fileno(stdout), fileno(stderr));
    close(b);
    close(c);
    return 0;
}
