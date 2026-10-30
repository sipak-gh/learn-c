/* enums.c - named integer constants. */
#include <stdio.h>

enum level { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_NLEVELS };

enum numeric {          /* explicit values, like IRC reply numbers */
    RPL_WELCOME = 1,
    ERR_NOSUCHNICK = 401,
    ERR_UNKNOWNCOMMAND = 421,
};

enum { BUF_SIZE = 64 }; /* an anonymous enum: just a compile-time int */

static const char *const level_names[LOG_NLEVELS] = {
    [LOG_ERROR] = "error",
    [LOG_WARN] = "warn",
    [LOG_INFO] = "info",
    [LOG_DEBUG] = "debug",
};

static const char *numeric_text(enum numeric n)
{
    switch (n) {        /* no default: -Wall warns if you forget a value */
    case RPL_WELCOME:
        return "welcome";
    case ERR_NOSUCHNICK:
        return "no such nick";
    case ERR_UNKNOWNCOMMAND:
        return "unknown command";
    }
    return "?";
}

int main(void)
{
    char buf[BUF_SIZE];         /* an enum constant can size an array */
    for (enum level l = LOG_ERROR; l < LOG_NLEVELS; l++)
        printf("%d=%s ", (int)l, level_names[l]);
    putchar('\n');

    snprintf(buf, sizeof buf, "%03d %s", ERR_UNKNOWNCOMMAND,
             numeric_text(ERR_UNKNOWNCOMMAND));
    printf("%s\n", buf);

    /* Enums are just ints. C does not stop this: */
    enum level odd = 42;
    printf("odd = %d, sizeof(enum level) = %zu\n", (int)odd, sizeof odd);
    return 0;
}
