/* log.c - log lines to stderr. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

/* The shared part of log_msg() and die(): a va_list can be handed on to
 * another function, but "..." cannot.
 *
 * stderr is unbuffered, so three fprintf calls would be three write()
 * system calls, and lines from two processes could get mixed up. So we
 * build the whole line first and write it with one fputs (one write). */
static void log_va(enum log_level level, const char *fmt, va_list ap)
{
    char line[1024];
    size_t max = sizeof line - 1;      /* keep room for the '\n' */
    int n = snprintf(line, max, "%s: %s: ", log_prog, level_name(level));
    if (n < 0)
        return;
    size_t len = (size_t)n;
    if (len < max) {
        n = vsnprintf(line + len, max - len, fmt, ap);
        if (n < 0)
            return;
        len += (size_t)n;
    }
    if (len >= max)
        len = max - 1;                 /* it was cut short to fit */
    line[len] = '\n';
    line[len + 1] = '\0';
    fputs(line, stderr);
}

void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level == LOG_DEBUG && !log_verbose)
        return;

    va_list ap;
    va_start(ap, fmt);
    log_va(level, fmt, ap);
    va_end(ap);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_va(LOG_ERROR, fmt, ap);
    va_end(ap);
    exit(1);
}
