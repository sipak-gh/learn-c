/* logsafe.c - never log a client's bytes as they are. */
#include <stdio.h>

/* Copy src into dst[size], writing control bytes as \xHH. */
static void escape(char *dst, size_t size, const char *src)
{
    size_t len = 0;
    for (; *src != '\0'; src++) {
        unsigned char c = (unsigned char)*src;
        char tmp[5];
        int n = c < 0x20 || c == 0x7f ? snprintf(tmp, sizeof tmp, "\\x%02x", c)
                                      : snprintf(tmp, sizeof tmp, "%c", c);
        if (len + (size_t)n >= size)
            break;                     /* no room: cut it short */
        for (int i = 0; i < n; i++)
            dst[len++] = tmp[i];
    }
    dst[len] = '\0';
}

int main(void)
{
    /* A QUIT reason chosen by a hostile client: a fake log line, and an
     * escape sequence that clears the screen of whoever reads the log. */
    const char *reason = "bye\nparleyd: info: root joined\x1b[2J";
    char safe[128];

    fprintf(stderr, "parleyd: info: eve left (%s)\n", reason);   /* wrong */
    escape(safe, sizeof safe, reason);
    fprintf(stderr, "parleyd: info: eve left (%s)\n", safe);     /* right */
    return 0;
}
