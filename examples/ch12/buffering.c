/* buffering.c - stdout is buffered, stderr is not. Where the output goes
 * (terminal or pipe) decides when stdout's buffer is written.
 *     ./buffering          default buffering
 *     ./buffering line     setvbuf: line buffered, even into a pipe */
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "line") == 0)
        setvbuf(stdout, NULL, _IOLBF, 0);    /* before any output */

    printf("out 1\n");
    fprintf(stderr, "err 1\n");
    printf("out 2\n");
    fprintf(stderr, "err 2\n");
    printf("out 3 (BUFSIZ is %d)\n", BUFSIZ);
    return 0;
}
