/* run.c - run a command, wait for it, and explain how it ended.
 * usage: run COMMAND [ARG...] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: run COMMAND [ARG...]\n");
        return 2;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("run: fork");
        return 1;
    }
    if (pid == 0) {
        /* The child becomes the command. execvp searches PATH and only
         * returns if it failed. */
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "run: %s: %s\n", argv[1], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);  /* the shell's conventions */
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("run: waitpid");
            return 1;
        }
    }
    if (WIFEXITED(status)) {
        printf("run: %s exited with status %d\n", argv[1], WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("run: %s was killed by signal %d (%s)\n", argv[1], sig, strsignal(sig));
        return 128 + sig;                /* again the shell's convention */
    }
    return 1;
}
