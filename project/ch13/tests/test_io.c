/* test_io.c - tests for io.c, using a pipe: two connected descriptors. */
#include <errno.h>
#include <unistd.h>

#include "io.h"
#include "test.h"
#include "tests.h"

void test_io_write_read(void)
{
    int fds[2];                        /* fds[0]: read end, fds[1]: write end */
    CHECK_INT(pipe(fds), 0);
    CHECK_INT(write_all(fds[1], "NICK al\r\nQUIT\r\n", 15), 0);

    char buf[16] = "";
    CHECK_INT(read_exact(fds[0], buf, 9), 9);
    CHECK_STR(buf, "NICK al\r\n");

    close(fds[1]);                     /* no writers left: EOF after the data */
    struct strbuf sb = { 0 };
    CHECK_INT(read_into(fds[0], &sb, 100), 6);
    CHECK_STR(sb.data, "QUIT\r\n");
    CHECK_INT(read_into(fds[0], &sb, 100), 0);         /* end of file */
    CHECK_INT(read_exact(fds[0], buf, 9), 0);
    sb_free(&sb);
    close(fds[0]);
}

void test_io_short_read(void)
{
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    write_all(fds[1], "abc", 3);
    close(fds[1]);
    char buf[10];
    CHECK_INT(read_exact(fds[0], buf, sizeof buf), 3);  /* EOF came first */
    close(fds[0]);
}

void test_io_nonblock(void)
{
    int fds[2];
    CHECK_INT(pipe(fds), 0);
    CHECK_INT(set_nonblock(fds[0]), 0);
    CHECK_INT(set_nonblock(fds[1]), 0);

    /* Reading an empty pipe would block; non-blocking, it fails at once. */
    struct strbuf sb = { 0 };
    errno = 0;
    CHECK_INT(read_into(fds[0], &sb, 100), -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);

    /* Fill the pipe: write() stops at its capacity with EAGAIN. */
    static char block[4096];
    size_t total = 0;
    ssize_t w;
    while ((w = write(fds[1], block, sizeof block)) > 0)
        total += (size_t)w;
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    CHECK(total >= 4096);              /* Linux: 65536 by default */
    sb_free(&sb);
    close(fds[0]);
    close(fds[1]);
}
