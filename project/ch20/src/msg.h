/* msg.h - the parley protocol: parse, format and frame lines.
 *
 *     message = [":" prefix " "] command {" " param} [" :" trailing] "\r\n"
 *
 * A line is at most MSG_LINE_MAX bytes including the "\r\n". Receivers
 * also accept a bare "\n".
 */
#ifndef PARLEY_MSG_H
#define PARLEY_MSG_H

#include <stdbool.h>
#include <stddef.h>

#include "strbuf.h"

#define MSG_LINE_MAX 512     /* longest line, including "\r\n" */
#define MSG_PARAMS_MAX 8     /* most middle parameters in one message */

/* One protocol line split into its parts. After msg_parse() all pointers
 * point into the parsed line, so the line must outlive the msg. */
struct msg {
    const char *prefix;                   /* who sent it; NULL if absent */
    const char *command;                  /* "NICK", "001", ... */
    int nparams;                          /* number of middle parameters */
    const char *params[MSG_PARAMS_MAX];   /* the middle parameters */
    const char *trailing;                 /* text after " :"; NULL if absent */
};

enum msg_status {
    MSG_OK,
    MSG_EMPTY,             /* blank line: not an error, just ignore it */
    MSG_NO_COMMAND,        /* a prefix with nothing after it */
    MSG_TOO_MANY_PARAMS
};

/* Parse line (without "\r\n") into *m. Modifies line: the spaces between
 * the parts become '\0' terminators. */
enum msg_status msg_parse(char *line, struct msg *m);

/* Parameter i, counting the trailing parameter as the last one: for both
 * "NICK bob" and "NICK :bob", msg_param(m, 0) is "bob". NULL if absent. */
const char *msg_param(const struct msg *m, int i);

/* Format *m as a line ending in "\r\n" into out[size], size >= 3. A too
 * long trailing parameter is cut so the line fits in MSG_LINE_MAX bytes
 * (and in size - 1). Returns the line length, or -1 if the parts before
 * the trailing parameter don't fit. */
int msg_format(char *out, size_t size, const struct msg *m);

/* Framing: take the next complete line out of the input buffer in. */
enum frame_status {
    FRAME_NONE,       /* no complete line yet: read more input */
    FRAME_LINE,       /* a line (without "\r\n") was copied into line[] */
    FRAME_TOO_LONG    /* a line longer than MSG_LINE_MAX was thrown away */
};

/* *skipping must start false and is kept between calls: it remembers that
 * we are throwing away the rest of a too-long line. Works however the input
 * was split up, even one byte at a time. */
enum frame_status msg_take_line(struct strbuf *in, bool *skipping,
                                char line[MSG_LINE_MAX + 1]);

#endif
