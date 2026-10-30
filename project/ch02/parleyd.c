/* parleyd.c - the parley chat server (Chapter 2: read the port number). */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { DEFAULT_PORT = 6667, PORT_MIN = 1, PORT_MAX = 65535 };

int main(int argc, char *argv[])
{
    /* argv is the list of command-line words (Chapter 4 explains argc/argv,
     * Chapter 5 pointers). For now we expect "parleyd -p PORT", so the
     * port text is the third word, argv[2]. Without it we use the default. */
    const char *text = argc > 2 ? argv[2] : "6667";

    /* strtol converts text to a long. It reports problems three ways:
     * errno (out of long's range), end == text (no digits at all), and
     * *end != '\0' (junk after the digits). */
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    bool ok = errno == 0 && end != text && *end == '\0'
              && value >= PORT_MIN && value <= PORT_MAX;

    /* A TCP port is a 16-bit unsigned number: uint16_t is the exact type.
     * The cast is safe because ok guarantees 1 <= value <= 65535. */
    uint16_t port = ok ? (uint16_t)value : DEFAULT_PORT;

    /* if/else is covered in Chapter 3. */
    if (!ok) {
        fprintf(stderr, "parleyd: invalid port '%s' (expected %d-%d)\n",
                text, PORT_MIN, PORT_MAX);
        return 2;
    }
    printf("parleyd: port %" PRIu16 " (%zu bytes as uint16_t, %zu as long)\n",
           port, sizeof port, sizeof value);
    return 0;
}
