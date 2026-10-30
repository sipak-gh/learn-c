/* pipeline.c - run "A | B" like a shell: A's stdout feeds B's stdin.
 * usage: pipeline CMD-A [ARG...] '|' CMD-B [ARG...] */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Start argv[0] in a child whose fd `target` (0 or 1) is `fd`. */
static pid_t spawn(char **argv, int fd, int target, int fds[2])
{
    pid_t pid = fork();
    if (pid != 0)
        return pid;                     /* parent (or -1 on error) */
    if (dup2(fd, target) < 0)           /* now fd `target` IS the pipe end */
        _exit(126);
    close(fds[0]);                      /* close every original pipe fd, */
    close(fds[1]);                      /* or the reader never sees EOF */
    execvp(argv[0], argv);
    fprintf(stderr, "pipeline: %s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

static void report(const char *name, int status)
{
    if (WIFEXITED(status))
        fprintf(stderr, "%s: exit %d\n", name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "%s: killed by signal %d (%s)\n", name,
                WTERMSIG(status), strsignal(WTERMSIG(status)));
}

int main(int argc, char *argv[])
{
    int bar = 1;
    while (bar < argc && strcmp(argv[bar], "|") != 0)
        bar++;
    if (bar == 1 || bar >= argc - 1) {
        fprintf(stderr, "usage: pipeline CMD-A [ARG...] '|' CMD-B [ARG...]\n");
        return 2;
    }
    argv[bar] = NULL;                   /* ends A's argument list */
    char **a = &argv[1], **b = &argv[bar + 1];

    int fds[2];                         /* fds[0]: read end, fds[1]: write end */
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }
    pid_t pa = spawn(a, fds[1], STDOUT_FILENO, fds);
    pid_t pb = spawn(b, fds[0], STDIN_FILENO, fds);
    close(fds[0]);                      /* the parent uses neither end */
    close(fds[1]);
    if (pa < 0 || pb < 0) {
        perror("fork");
        return 1;
    }

    int sa, sb;
    waitpid(pa, &sa, 0);
    waitpid(pb, &sb, 0);
    report(a[0], sa);
    report(b[0], sb);
    return WIFEXITED(sb) ? WEXITSTATUS(sb) : 1;
}
