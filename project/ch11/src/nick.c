/* nick.c - nickname rules. */
#include "nick.h"

#include <ctype.h>
#include <string.h>

#include "str.h"

bool nick_valid(const char *nick)
{
    size_t len = strlen(nick);
    if (len == 0 || len > NICK_MAX || !isalpha((unsigned char)nick[0]))
        return false;
    for (size_t i = 1; i < len; i++) {
        unsigned char c = (unsigned char)nick[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
    }
    return true;
}

bool nick_equal(const char *a, const char *b)
{
    return str_ieq(a, b);
}
