/* io.h - helpers for reading and writing file descriptors.
 *
 * read() and write() may transfer fewer bytes than asked for (a "short"
 * read or write), and may fail with EINTR when a signal arrives. These
 * helpers deal with that so the callers don't have to.
 */
#ifndef PARLEY_IO_H
#define PARLEY_IO_H

#include <stddef.h>
#include <sys/types.h>

#include "strbuf.h"

/* Write all n bytes, looping over short writes and EINTR. Returns 0, or -1
 * with errno set. */
int write_all(int fd, const void *buf, size_t n);

/* Read exactly n bytes, looping over short reads and EINTR. Returns n, a
 * smaller count if end of file came first, or -1 with errno set. */
ssize_t read_exact(int fd, void *buf, size_t n);

/* One read() of up to max bytes, appended to sb. Returns the number of
 * bytes read, 0 at end of file, or -1 with errno set. EINTR is NOT retried
 * here: the caller may want to check a flag set by a signal handler. */
ssize_t read_into(int fd, struct strbuf *sb, size_t max);

/* Put fd in non-blocking mode. Returns 0 or -1. */
int set_nonblock(int fd);

#endif
