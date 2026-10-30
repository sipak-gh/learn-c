/* log.c - log lines to stderr. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static const char *log_prog = "parley";
static bool log_verbose = false;

void log_init(const char *prog, bool verbose)
{
    log_prog = prog;
    log_verbose = verbose;
}

static const char *level_name(enum log_level level)
{
    switch (level) {
    case LOG_DEBUG: return "debug";
    case LOG_INFO:  return "info";
    case LOG_WARN:  return "warning";
    case LOG_ERROR: return "error";
    }
    return "?";
}

void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !log_verbose)
        return;

    fprintf(stderr, "%s: %s: ", log_prog, level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
