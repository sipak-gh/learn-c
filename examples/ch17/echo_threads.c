/* echo_threads.c - an echo server that starts a thread per client. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static long lines_total;                /* shared by all threads ... */
static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;   /* ... so locked */

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

/* The thread for one client: echo until end of file, counting lines. */
static void *client_thread(void *arg)
{
    int fd = *(int *)arg;
    free(arg);
    char buf[4096];
    ssize_t n;
    long lines = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        for (ssize_t i = 0; i < n; i++)
            lines += buf[i] == '\n';
        if (n > 0 && write(fd, buf, (size_t)n) < 0)
            break;
    }
    close(fd);

    pthread_mutex_lock(&total_lock);
    lines_total += lines;
    printf("[pid %d] client done: %ld lines, %ld in total\n",
           (int)getpid(), lines, lines_total);
    pthread_mutex_unlock(&total_lock);
    return NULL;
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        int *arg = malloc(sizeof *arg); /* the thread's own copy of fd */
        if (arg == NULL) {
            close(fd);
            continue;
        }
        *arg = fd;
        pthread_t tid;
        int err = pthread_create(&tid, NULL, client_thread, arg);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            free(arg);
            close(fd);
            continue;
        }
        pthread_detach(tid);            /* nobody will join it */
    }
}
