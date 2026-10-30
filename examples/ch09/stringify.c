/* stringify.c - # and ##, predefined macros, and a variadic logging macro. */
#include <stdarg.h>
#include <stdio.h>

#define SHOW(expr) printf("%-14s = %d\n", #expr, (expr))   /* #: text to string */

#define STR(x) #x                       /* stringify the argument as written */
#define XSTR(x) STR(x)                  /* expand the argument first, then stringify */
#define PORT 6667

#define CMD_NAME(name) cmd_##name       /* ##: glue two tokens into one */

/* C99 variadic macro: ... collects the arguments, __VA_ARGS__ pastes them.
 * fmt is part of the ..., so LOG("text") with no other arguments is fine. */
#define LOG(...) log_at(__FILE__, __LINE__, __func__, __VA_ARGS__)

static void log_at(const char *file, int line, const char *func, const char *fmt, ...)
{
    fprintf(stderr, "%s:%d: %s: ", file, line, func);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void cmd_ping(void)
{
    LOG("ping handled");
}

int main(void)
{
    SHOW(2 + 3 * 4);
    SHOW(PORT % 1000);

    printf("STR(PORT)  = \"%s\"\n", STR(PORT));
    printf("XSTR(PORT) = \"%s\"\n", XSTR(PORT));

    CMD_NAME(ping)();                   /* calls cmd_ping() */

    LOG("listening on port %d", PORT);
    printf("__STDC_VERSION__ = %ldL\n", (long)__STDC_VERSION__);
    printf("compiled on %s at %s\n", __DATE__, __TIME__);
    return 0;
}
