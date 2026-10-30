/* room.h - the chat room: who is here, and what the server does with each
 * line a client sends.
 *
 * The room does no locking itself: the caller must make sure only one
 * thread uses the room (and the connections in it) at a time. */
#ifndef PARLEY_ROOM_H
#define PARLEY_ROOM_H

#include <stddef.h>

#include "conn.h"

/* A growable array of the connected clients, registered or not. A zeroed
 * struct room is a valid empty room. */
struct room {
    struct conn **conns;
    size_t count;
    size_t cap;
};

/* Add c to the room. Returns 0, or -1 if memory is exhausted. */
int room_add(struct room *r, struct conn *c);

/* Remove c from the room (it must be in it). Sends nothing. */
void room_remove(struct room *r, struct conn *c);

/* c leaves the chat: if it has registered, log it and tell the others
 * ":nick QUIT :reason". Afterwards c is unregistered, so a second call
 * does nothing. */
void room_part(struct room *r, struct conn *c, const char *reason);

/* Free the array (not the connections in it). */
void room_free(struct room *r);

/* Handle one protocol line (without "\r\n") from client c. Modifies line. */
void room_handle_line(struct room *r, struct conn *c, char *line);

#endif
