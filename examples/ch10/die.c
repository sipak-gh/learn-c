/* die.c - a portable die(): print a message and exit. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "die";

/* Print "prog: message" to stderr and exit(1). A message ending in ':'
 * gets the errno text appended, like perror(). */
static void die(const char *fmt, ...)
{
    int err = errno;                      /* before anything can change it */
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':')
        fprintf(stderr, " %s", strerror(err));
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        die("usage: %s FILE", argv[0]);

    FILE *f = fopen(argv[1], "r");
    if (f == NULL)
        die("cannot open %s:", argv[1]);
    printf("opened %s\n", argv[1]);
    fclose(f);
    return 0;
}
