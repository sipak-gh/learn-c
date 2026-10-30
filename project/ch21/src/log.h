/* log.h - log lines to stderr: "[time] prog: level: message". */
#ifndef PARLEY_LOG_H
#define PARLEY_LOG_H

#include <stdbool.h>

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* Set the program name used as prefix, and whether debug lines show. */
void log_init(const char *prog, bool verbose);

/* Start each line with the local date and time (off by default). */
void log_timestamps(bool on);

/* printf-style; adds the prefix and a newline. */
void log_msg(enum log_level level, const char *fmt, ...);

/* Log an error like log_msg(LOG_ERROR, ...) and exit with status 1. For
 * errors the program cannot recover from. */
void die(const char *fmt, ...);

/* Have die() call fn before it prints the error, e.g. to put the terminal
 * back in order so the message can be seen. */
void log_on_die(void (*fn)(void));

#endif
