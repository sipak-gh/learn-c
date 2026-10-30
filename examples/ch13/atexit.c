/* atexit.c - functions that run when the program exits normally.
 * usage: atexit [return | exit | _exit | signal] */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void first(void)  { printf("cleanup 1 (registered first)\n"); }
static void second(void) { printf("cleanup 2 (registered second)\n"); }

int main(int argc, char *argv[])
{
    const char *how = argc > 1 ? argv[1] : "return";
    atexit(first);
    atexit(second);
    printf("ending with %s\n", how);    /* no fflush: watch what survives */

    if (strcmp(how, "exit") == 0)
        exit(0);                        /* handlers run, stdio is flushed */
    if (strcmp(how, "_exit") == 0)
        _exit(0);                       /* straight to the kernel */
    if (strcmp(how, "signal") == 0)
        raise(SIGTERM);                 /* killed: nothing runs */
    return 0;                           /* same as exit(0) */
}
