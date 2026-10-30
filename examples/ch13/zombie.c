/* zombie.c - a child that has exited but was not waited for yet. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0)
        _exit(0);                       /* the child ends at once */

    sleep(2);                           /* the child is a zombie meanwhile */
    waitpid(pid, NULL, 0);              /* reaped: the zombie is gone */
    printf("reaped %d\n", (int)pid);
    return 0;
}
