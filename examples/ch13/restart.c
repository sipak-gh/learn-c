/* restart.c - what happens to a blocked read() when a signal arrives?
 * usage: restart          (sa_flags = 0)
 *        restart -r       (sa_flags = SA_RESTART) */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void on_alarm(int signo)
{
    (void)signo;                        /* just interrupt the read */
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = (argc > 1 && strcmp(argv[1], "-r") == 0) ? SA_RESTART : 0;
    sigaction(SIGALRM, &sa, NULL);

    alarm(1);                           /* SIGALRM in 1 second */
    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n < 0)
        printf("read failed: %s\n", strerror(errno));
    else
        printf("read returned %d bytes\n", (int)n);
    return 0;
}
