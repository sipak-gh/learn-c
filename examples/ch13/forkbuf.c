/* forkbuf.c - fork() copies the stdio buffers too. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    printf("line 1, before fork\n");    /* may still sit in the buffer... */
    printf("line 2, before fork\n");
    pid_t pid = fork();                 /* ...and then there are two buffers */
    if (pid < 0)
        return 1;
    printf("after fork, in the %s\n", pid == 0 ? "child" : "parent");
    if (pid > 0)
        waitpid(pid, NULL, 0);
    return 0;                           /* each process flushes its copy */
}
