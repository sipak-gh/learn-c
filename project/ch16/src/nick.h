/* nick.h - nickname rules. */
#ifndef PARLEY_NICK_H
#define PARLEY_NICK_H

#include <stdbool.h>

#define NICK_MAX 16   /* longest nickname, in bytes */

/* 1-16 characters: a letter, then letters, digits, '_' or '-'. */
bool nick_valid(const char *nick);

/* Nicknames are case-insensitive: "Alice" and "alice" are the same. */
bool nick_equal(const char *a, const char *b);

#endif
