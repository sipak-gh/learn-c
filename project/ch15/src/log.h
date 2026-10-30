/* log.h - log lines to stderr: "prog: level: message". */
#ifndef PARLEY_LOG_H
#define PARLEY_LOG_H

#include <stdbool.h>

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Set the program name used as prefix, and whether debug lines show. */
void log_init(const char *prog, bool verbose);

/* printf-style; adds the prefix and a newline. */
void log_msg(enum log_level level, const char *fmt, ...);

/* Log an error like log_msg(LOG_ERROR, ...) and exit with status 1. For
 * errors the program cannot recover from. */
void die(const char *fmt, ...);

#endif
