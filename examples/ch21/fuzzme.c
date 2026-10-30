/* fuzzme.c - a tiny fuzzer finds a bug in a tiny parser.
 *     fuzzme [ROUNDS [SEED]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LEN 9    /* nicks have at most 9 bytes */

/* "NICK name": copy name into name[NAME_LEN + 1]. Returns 0, or -1 if
 * the line is not like that. There is a bug in here. */
static int parse_nick(const char *line, char *name)
{
    if (strncmp(line, "NICK ", 5) != 0)
        return -1;
    size_t n = strcspn(line + 5, " ");
    if (n == 0 || n > NAME_LEN + 1)
        return -1;
    memcpy(name, line + 5, n);
    name[n] = '\0';
    return 0;
}

/* A random line: often "NICK ", then 0-15 bytes from a small alphabet. */
static void random_line(char *line)
{
    static const char alphabet[] = "ab ";
    size_t len = 0;
    if (rand() % 2 == 0) {
        strcpy(line, "NICK ");
        len = 5;
    }
    for (int n = rand() % 16; n > 0; n--)
        line[len++] = alphabet[rand() % 3];
    line[len] = '\0';
}

int main(int argc, char *argv[])
{
    long rounds = argc > 1 ? atol(argv[1]) : 100000;
    unsigned seed = argc > 2 ? (unsigned)atol(argv[2]) : 1;
    srand(seed);
    for (long i = 1; i <= rounds; i++) {
        char line[32];
        random_line(line);
        char *name = malloc(NAME_LEN + 1);     /* exactly the size needed */
        if (name == NULL)
            return 1;
        /* The property: a name that parses fits in NAME_LEN bytes. */
        if (parse_nick(line, name) == 0 && strlen(name) > NAME_LEN) {
            printf("round %ld (seed %u): \"%s\" gave a %zu-byte name\n",
                   i, seed, line, strlen(name));
            free(name);
            return 1;
        }
        free(name);
    }
    printf("%ld rounds (seed %u): no bug found\n", rounds, seed);
    return 0;
}
