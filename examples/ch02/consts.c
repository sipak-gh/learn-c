/* consts.c - named constants: enum, const and #define. */
#include <stdio.h>

#define LINE_MAX_BYTES 512           /* preprocessor text replacement (Chapter 9) */

enum { NICK_MAX = 9, CHANNELS_MAX = 20 };   /* true integer constants */
enum color { RED, GREEN, BLUE = 10, CYAN };  /* RED=0, GREEN=1, CYAN=11 */

int main(void)
{
    const double pi = 3.14159265358979;  /* read-only variable */
    enum color c = CYAN;

    printf("NICK_MAX %d, CHANNELS_MAX %d, LINE_MAX_BYTES %d\n",
           NICK_MAX, CHANNELS_MAX, LINE_MAX_BYTES);
    printf("RED %d, GREEN %d, BLUE %d, c %d\n", RED, GREEN, BLUE, c);
    printf("pi %.5f, sizeof(enum color) %zu\n", pi, sizeof(enum color));
    return 0;
}
