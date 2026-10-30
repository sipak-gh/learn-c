/* echo_fork.c - an echo server that forks a child process per client. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static long lines_total;                /* every process has its own copy */

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 64) < 0)
        return -1;
    return fd;
}

/* Echo until end of file, counting lines. */
static void echo(int fd)
{
    char buf[4096];
    ssize_t n;
    long lines = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        for (ssize_t i = 0; i < n; i++)
            lines += buf[i] == '\n';
        if (n > 0 && write(fd, buf, (size_t)n) < 0)
            break;
    }
    lines_total += lines;
    printf("[pid %d] client done: %ld lines, %ld in total\n",
           (int)getpid(), lines, lines_total);
}

/* Reap every child that has ended, without blocking (Chapter 13). */
static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved;
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_sigchld, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);   /* one line at a time, even into a pipe */

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;                   /* EINTR from SIGCHLD, or an error */
        pid_t pid = fork();
        if (pid == 0) {                 /* the child: serve this client */
            close(lfd);
            echo(fd);
            _exit(0);
        }
        if (pid < 0)
            perror("fork");
        close(fd);                      /* the parent: the child has it now */
    }
}
