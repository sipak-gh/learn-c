/* room.h - the chat room: what the server does with each line. */
#ifndef PARLEY_ROOM_H
#define PARLEY_ROOM_H

#include "conn.h"

/* Handle one protocol line (without "\r\n") from client c. Modifies line. */
void room_handle_line(struct conn *c, char *line);

#endif
