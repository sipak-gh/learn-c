/* ui.c - the split-screen terminal user interface of the client.
 *
 * The screen is drawn with ANSI (VT100) escape sequences: ESC [ ... a
 * letter. The ones used here:
 *   ESC[r;cH   move the cursor to row r, column c (counted from 1)
 *   ESC[2J     clear the screen        ESC[2K   clear the cursor's line
 *   ESC[t;br   scroll region: only rows t..b scroll
 *   ESC[7m     reverse video           ESC[0m   back to normal
 *   ESC[31m    red text (36m: cyan, 35m: magenta)
 *   ESC[?1049h switch to the alternate screen (l: back to the normal one)
 */
#include "ui.h"

#include <string.h>
#include <unistd.h>

#include "io.h"
#include "str.h"
#include "strbuf.h"
#include "term.h"

#define HISTORY_MAX 200          /* chat lines kept for redrawing */
#define INPUT_BUF 512

static bool active = false;
static int rows, cols;
static struct strbuf screen;     /* drawn, not yet written to the terminal */

/* The last HISTORY_MAX chat lines, in a ring: the newest is just before
 * history[next]. */
static char history[HISTORY_MAX][UI_LINE_MAX];
static int hist_count, hist_next;

static char status[128];

/* The line editor: input[0..len) is the text, cursor a byte index. */
static char input[INPUT_BUF];
static size_t input_len, cursor, input_max;

/* An escape sequence (e.g. an arrow key: ESC [ D) read so far. */
static char esc[8];
static size_t esc_len;

/* UTF-8: a character is 1-4 bytes; the bytes after the first one look
 * like 10xxxxxx. We count characters to find screen columns. (Wide
 * characters such as CJK take 2 columns; we ignore that.) */
static bool is_continuation(char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

static int columns(const char *s, size_t n)
{
    int count = 0;
    for (size_t i = 0; i < n; i++)
        if (!is_continuation(s[i]))
            count++;
    return count;
}

/* A color for the line, from its kind: errors, notices or actions. */
static const char *color_for(const char *line)
{
    if (line[0] == '[') {                       /* skip "[12:00] " */
        const char *end = strstr(line, "] ");
        if (end != NULL)
            line = end + 2;
    }
    if (strncmp(line, "!!", 2) == 0)
        return "\x1b[31m";
    if (strncmp(line, "--", 2) == 0 || strncmp(line, "<--", 3) == 0)
        return "\x1b[36m";
    if (strncmp(line, "* ", 2) == 0)
        return "\x1b[35m";
    return "";
}

static void draw_chat_line(const char *line)
{
    /* At the bottom of the scroll region, "\n" scrolls the region up by
     * one row; the new line is then written on the freed bottom row. */
    sb_appendf(&screen, "\x1b[%d;1H\n%s%s\x1b[0m", rows - 2, color_for(line), line);
}

static void draw_status(void)
{
    /* %-*.*s: pad with spaces to exactly cols characters, or cut. */
    sb_appendf(&screen, "\x1b[%d;1H\x1b[7m%-*.*s\x1b[0m", rows - 1, cols, cols, status);
}

/* Draw the input line and put the cursor where the user types. If the
 * text is wider than the screen, show the part around the cursor. */
static void draw_input(void)
{
    int width = cols - 3;              /* "> " in front, 1 column spare */
    size_t start = 0;
    while (columns(input + start, cursor - start) > width) {
        start++;
        while (start < cursor && is_continuation(input[start]))
            start++;
    }
    sb_appendf(&screen, "\x1b[%d;1H\x1b[2K> ", rows);
    int col = 0;
    size_t end = start;
    while (end < input_len) {
        if (!is_continuation(input[end]) && col++ == width)
            break;
        end++;
    }
    sb_append(&screen, input + start, end - start);
    sb_appendf(&screen, "\x1b[%d;%dH", rows, 3 + columns(input + start, cursor - start));
}

static void redraw(void)
{
    term_size(&rows, &cols);
    if (rows < 4)
        rows = 4;
    if (cols < 10)
        cols = 10;
    /* Clear, then set the scroll region to the chat area. */
    sb_appendf(&screen, "\x1b[r\x1b[2J\x1b[1;%dr", rows - 2);
    int n = hist_count < rows - 2 ? hist_count : rows - 2;
    for (int i = n; i > 0; i--)
        draw_chat_line(history[(hist_next - i + HISTORY_MAX) % HISTORY_MAX]);
    draw_status();
    draw_input();
}

int ui_start(const char *status_text, size_t max)
{
    if (term_raw() < 0)
        return -1;
    active = true;
    input_max = max < INPUT_BUF - 1 ? max : INPUT_BUF - 1;
    str_copy(status, status_text, sizeof status);
    sb_appendf(&screen, "\x1b[?1049h");
    redraw();
    ui_flush();
    return 0;
}

void ui_stop(void)
{
    if (!active)
        return;
    active = false;
    /* Reset the scroll region, leave the alternate screen. */
    sb_appendf(&screen, "\x1b[r\x1b[?1049l");
    ui_flush();
    term_restore();
    sb_free(&screen);
}

void ui_resize(void)
{
    if (active)
        redraw();
}

void ui_print(const char *line)
{
    str_copy(history[hist_next], line, UI_LINE_MAX);
    hist_next = (hist_next + 1) % HISTORY_MAX;
    if (hist_count < HISTORY_MAX)
        hist_count++;
    if (!active)
        return;
    draw_chat_line(line);
    draw_input();                      /* puts the cursor back */
}

void ui_set_status(const char *status_text)
{
    str_copy(status, status_text, sizeof status);
    if (!active)
        return;
    draw_status();
    draw_input();
}

void ui_flush(void)
{
    if (screen.len > 0) {
        write_all(STDOUT_FILENO, screen.data, screen.len);
        sb_clear(&screen);
    }
}

/* Line editing. */

static void insert_byte(char c)
{
    if (input_len >= input_max) {
        sb_append(&screen, "\a", 1);   /* full: ring the bell */
        return;
    }
    memmove(input + cursor + 1, input + cursor, input_len - cursor);
    input[cursor++] = c;
    input_len++;
}

static void cursor_left(void)
{
    while (cursor > 0 && is_continuation(input[--cursor]))
        continue;
}

static void cursor_right(void)
{
    if (cursor < input_len)
        cursor++;
    while (cursor < input_len && is_continuation(input[cursor]))
        cursor++;
}

/* Delete the character before the cursor (Backspace). */
static void delete_back(void)
{
    size_t end = cursor;
    cursor_left();
    memmove(input + cursor, input + end, input_len - end);
    input_len -= end - cursor;
}

/* A complete escape sequence is in esc[0..esc_len). */
static void handle_escape(void)
{
    esc[esc_len] = '\0';
    const char *seq = esc + 1;         /* skip the ESC */
    if (strcmp(seq, "[D") == 0)
        cursor_left();
    else if (strcmp(seq, "[C") == 0)
        cursor_right();
    else if (strcmp(seq, "[H") == 0 || strcmp(seq, "OH") == 0 || strcmp(seq, "[1~") == 0)
        cursor = 0;
    else if (strcmp(seq, "[F") == 0 || strcmp(seq, "OF") == 0 || strcmp(seq, "[4~") == 0)
        cursor = input_len;
    else if (strcmp(seq, "[3~") == 0 && cursor < input_len) {   /* Delete */
        cursor_right();
        delete_back();
    }
    /* anything else (up, down, F1, ...): ignored */
}

/* Collect one more byte of an escape sequence. A sequence is ESC, then
 * '[' or 'O', then parameters, then a final byte from '@' to '~'. */
static void escape_byte(char c)
{
    esc[esc_len++] = c;
    if (esc_len == 2 && c != '[' && c != 'O') {
        esc_len = 0;                   /* not a sequence we know: drop it */
        return;
    }
    if (esc_len >= 3 && c >= '@' && c <= '~') {
        handle_escape();
        esc_len = 0;
    } else if (esc_len == sizeof esc - 1) {
        esc_len = 0;                   /* too long: drop it */
    }
}

bool ui_keys(const char *keys, size_t n, void (*on_line)(char *line))
{
    for (size_t i = 0; i < n; i++) {
        char c = keys[i];
        if (esc_len > 0) {
            escape_byte(c);
            continue;
        }
        switch (c) {
        case '\x1b':                   /* ESC: start of a sequence */
            esc[0] = c;
            esc_len = 1;
            break;
        case '\r':                     /* Enter */
        case '\n': {
            char line[INPUT_BUF];
            memcpy(line, input, input_len);
            line[input_len] = '\0';
            input_len = cursor = 0;
            on_line(line);
            break;
        }
        case '\x7f':                   /* Backspace (some send Ctrl-H) */
        case '\b':
            delete_back();
            break;
        case '\x15':                   /* Ctrl-U: clear the line */
            input_len = cursor = 0;
            break;
        case '\x01':                   /* Ctrl-A: start of line */
            cursor = 0;
            break;
        case '\x05':                   /* Ctrl-E: end of line */
            cursor = input_len;
            break;
        case '\x03':                   /* Ctrl-C */
            return false;
        case '\x04':                   /* Ctrl-D: leave if the line is empty */
            if (input_len == 0)
                return false;
            break;
        default:
            /* Printable ASCII, or a byte of a UTF-8 character. Other
             * control characters are ignored. */
            if ((unsigned char)c >= 0x20)
                insert_byte(c);
            break;
        }
    }
    if (active)
        draw_input();
    return true;
}
