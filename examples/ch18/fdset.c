/* fdset.c - select()'s fd_set has room for FD_SETSIZE descriptors, no more. */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <unistd.h>

int main(void)
{
    printf("FD_SETSIZE = %d, sizeof(fd_set) = %zu, sizeof(struct pollfd) = %zu\n",
           FD_SETSIZE, sizeof(fd_set), sizeof(struct pollfd));
    fflush(stdout);                     /* out before a possible crash */

    /* A busy server easily has descriptor numbers above 1024. Raise our
     * limit if we may, and make one: a copy of standard input. */
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    if (rl.rlim_cur < 2000 && rl.rlim_max >= 2000) {
        rl.rlim_cur = 2000;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    int fd = dup2(0, 1500);
    if (fd < 0) {
        perror("dup2");
        return 1;
    }

    struct {
        fd_set set;
        unsigned char after[256];       /* whatever happens to come next */
    } s;
    memset(s.after, 0, sizeof s.after);
    FD_ZERO(&s.set);
    FD_SET(fd, &s.set);                 /* bit 1500 of a 1024-bit set! */

    for (size_t i = 0; i < sizeof s.after; i++)
        if (s.after[i] != 0)
            printf("FD_SET(%d) changed the byte %zu bytes after the fd_set\n",
                   fd, i);
    return 0;
}
