/* errtext.c - perror() and strerror(): turn errno into a message. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SHOW(e) printf("%-10s %3d  %s\n", #e, e, strerror(e))

int main(void)
{
    FILE *f = fopen("/no/such/dir/file.txt", "r");
    if (f == NULL) {
        int err = errno;                   /* save it before any other call */
        perror("fopen");                   /* prints "fopen: <text>\n" to stderr */
        fprintf(stderr, "saved errno: %s\n", strerror(err));
        fprintf(stderr, "errno now:   %s\n", strerror(errno));
    }

    SHOW(ENOENT);
    SHOW(EACCES);
    SHOW(EINTR);
    SHOW(ENOMEM);
    SHOW(ERANGE);
    SHOW(EIO);
    SHOW(EADDRINUSE);
    SHOW(EMFILE);
    printf("%-10s %3d  %s\n", "(none)", 0, strerror(0));
    printf("%-10s %3d  %s\n", "(unknown)", 999, strerror(999));
    return 0;
}
