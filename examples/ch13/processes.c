/* processes.c - fork() makes a copy of the running process. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int x = 1;
    printf("parent: pid %d, my parent is %d\n", (int)getpid(), (int)getppid());
    fflush(stdout);                     /* see "fork and stdio buffers" */

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {                     /* in the child: fork returned 0 */
        x = 100;                        /* changes the child's copy only */
        printf("child:  pid %d, my parent is %d, x = %d\n",
               (int)getpid(), (int)getppid(), x);
        exit(7);
    }

    /* in the parent: fork returned the child's pid */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    printf("parent: child %d exited with status %d, x = %d\n",
           (int)pid, WEXITSTATUS(status), x);
    return 0;
}
