/* msg.c - the parley protocol: parse, format and frame lines. */
#include "msg.h"

#include <assert.h>
#include <string.h>

/* End the word at p (overwrite the first space with '\0') and return a
 * pointer to the start of the next word, skipping extra spaces. */
static char *cut_word(char *p)
{
    while (*p != '\0' && *p != ' ')
        p++;
    while (*p == ' ')
        *p++ = '\0';
    return p;
}

enum msg_status msg_parse(char *line, struct msg *m)
{
    *m = (struct msg){ .prefix = NULL };   /* zero every member */

    char *p = line;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return MSG_EMPTY;

    if (*p == ':') {
        m->prefix = p + 1;
        p = cut_word(p);
        if (m->prefix[0] == '\0' || *p == '\0')
            return MSG_NO_COMMAND;
    }

    m->command = p;
    p = cut_word(p);

    while (*p != '\0') {
        if (*p == ':') {              /* trailing parameter: rest of line */
            m->trailing = p + 1;
            break;
        }
        if (m->nparams == MSG_PARAMS_MAX)
            return MSG_TOO_MANY_PARAMS;
        m->params[m->nparams++] = p;
        p = cut_word(p);
    }
    return MSG_OK;
}

const char *msg_param(const struct msg *m, int i)
{
    assert(i >= 0);
    if (i < m->nparams)
        return m->params[i];
    if (i == m->nparams)
        return m->trailing;
    return NULL;
}

/* Append text to out (holding *len bytes, room for size) without going
 * past size - 1. Returns false if text did not fit completely. */
static bool append(char *out, size_t size, size_t *len, const char *text)
{
    size_t n = strlen(text);
    size_t room = size - 1 - *len;
    bool fits = n <= room;
    if (!fits)
        n = room;
    memcpy(out + *len, text, n);
    *len += n;
    out[*len] = '\0';
    return fits;
}

int msg_format(char *out, size_t size, const struct msg *m)
{
    assert(size >= 3);                 /* room for at least "\r\n" */

    /* Build the line without "\r\n" in at most MSG_LINE_MAX - 2 bytes. */
    if (size > MSG_LINE_MAX + 1)
        size = MSG_LINE_MAX + 1;
    size -= 2;                         /* keep room for "\r\n" */
    size_t len = 0;
    bool ok = true;

    out[0] = '\0';
    if (m->prefix != NULL) {
        ok = ok && append(out, size, &len, ":");
        ok = ok && append(out, size, &len, m->prefix);
        ok = ok && append(out, size, &len, " ");
    }
    ok = ok && append(out, size, &len, m->command);
    for (int i = 0; i < m->nparams; i++) {
        ok = ok && append(out, size, &len, " ");
        ok = ok && append(out, size, &len, m->params[i]);
    }
    if (m->trailing != NULL) {
        ok = ok && append(out, size, &len, " :");
        if (ok)
            append(out, size, &len, m->trailing);   /* may be cut short */
    }
    if (!ok)
        return -1;
    memcpy(out + len, "\r\n", 3);      /* 3: copies the '\0' too */
    return (int)len + 2;
}

enum frame_status msg_take_line(struct strbuf *in, bool *skipping,
                                char line[MSG_LINE_MAX + 1])
{
    for (;;) {
        char *nl = in->len > 0 ? memchr(in->data, '\n', in->len) : NULL;
        if (nl == NULL) {
            if (in->len < MSG_LINE_MAX)
                return FRAME_NONE;      /* the line may still be completed */
            sb_clear(in);               /* already too long: drop it */
            if (*skipping)
                return FRAME_NONE;
            *skipping = true;
            return FRAME_TOO_LONG;
        }

        size_t n = (size_t)(nl - in->data) + 1;    /* bytes including '\n' */
        if (*skipping) {                /* the end of a too-long line */
            sb_consume(in, n);
            *skipping = false;
            continue;
        }
        if (n > MSG_LINE_MAX) {
            sb_consume(in, n);
            return FRAME_TOO_LONG;
        }
        memcpy(line, in->data, n - 1);
        line[n - 1] = '\0';
        if (n >= 2 && line[n - 2] == '\r')
            line[n - 2] = '\0';
        sb_consume(in, n);
        return FRAME_LINE;
    }
}
