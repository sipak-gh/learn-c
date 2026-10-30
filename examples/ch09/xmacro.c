/* xmacro.c - one list of replies, expanded four different ways. */
#include <stdio.h>

/* The list. Each line is X(name, code, text); X is chosen by the user. */
#define REPLIES(X)                                          \
    X(RPL_WELCOME,          "001", "Welcome")               \
    X(ERR_NONICKNAMEGIVEN,  "431", "No nickname given")     \
    X(ERR_ERRONEUSNICKNAME, "432", "Erroneous nickname")    \
    X(ERR_NOTREGISTERED,    "451", "You have not registered")

#define AS_ENUM(name, code, text) name,
enum reply { REPLIES(AS_ENUM) REPLY_COUNT };
#undef AS_ENUM

#define AS_CODE(name, code, text) code,
static const char *const reply_code[] = { REPLIES(AS_CODE) };
#undef AS_CODE

#define AS_TEXT(name, code, text) text,
static const char *const reply_text[] = { REPLIES(AS_TEXT) };
#undef AS_TEXT

#define AS_NAME(name, code, text) #name,
static const char *const reply_name[] = { REPLIES(AS_NAME) };
#undef AS_NAME

int main(void)
{
    for (int r = 0; r < REPLY_COUNT; r++)
        printf("%-20s %s :%s\n", reply_name[r], reply_code[r], reply_text[r]);
    printf("ERR_NOTREGISTERED = %d, REPLY_COUNT = %d\n", ERR_NOTREGISTERED, REPLY_COUNT);
    return 0;
}
