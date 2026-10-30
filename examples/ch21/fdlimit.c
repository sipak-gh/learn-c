/* fdlimit.c - run out of file descriptors on purpose. */
#define _XOPEN_SOURCE 700            /* getrlimit/setrlimit are XSI */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

int main(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit");
        return 1;
    }
    printf("limit: soft %llu, hard %llu\n",
           (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);

    rl.rlim_cur = 8;                 /* lower the soft limit: fds 0..7 */
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("setrlimit");
        return 1;
    }
    int spare = open("/dev/null", O_RDONLY);    /* kept in reserve */
    int fd, last = -1;
    while ((fd = open("/dev/null", O_RDONLY)) >= 0)
        last = fd;
    printf("opened up to fd %d, then: %s\n", last, strerror(errno));

    close(spare);                    /* free the reserve ... */
    fd = open("/dev/null", O_RDONLY);
    printf("after closing the spare: fd %d\n", fd);
    return 0;
}
