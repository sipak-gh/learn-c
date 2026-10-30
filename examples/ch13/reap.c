/* reap.c - start children, collect each one as it exits (SIGCHLD). */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t child_exited = 0;

static void on_child(int signo)
{
    (void)signo;
    child_exited = 1;
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_child;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* Block SIGCHLD while we check the flag, so it can't slip in between
     * the check and the wait (see sigsuspend below). */
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    const char *naps[] = { "0.3", "0.1", "0.2" };
    int running = 0;
    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            sigprocmask(SIG_SETMASK, &old, NULL);   /* the child: undo */
            execlp("sleep", "sleep", naps[i], (char *)NULL);
            _exit(127);
        }
        if (pid > 0) {
            printf("started sleep %s\n", naps[i]);
            running++;
        }
    }

    while (running > 0) {
        while (child_exited == 0)
            sigsuspend(&old);           /* unblock + wait, in one step */
        child_exited = 0;

        /* One SIGCHLD may stand for several exits: reap all that are done.
         * WNOHANG: return 0 at once instead of waiting for a live child. */
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("reaped child %d, exit %d\n", (int)pid, WEXITSTATUS(status));
            running--;
        }
    }
    printf("no children left\n");
    return 0;
}
