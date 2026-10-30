/* lookup.c - name lookups: the host "localhost" and a user.
 *     lookup [USER]          (default: root) */
#define _POSIX_C_SOURCE 200809L
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    const char *user = argc > 1 ? argv[1] : "root";
    struct addrinfo hints, *res;
    struct passwd *pw;
    int rc;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo("localhost", "6667", &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }
    printf("localhost: found\n");
    freeaddrinfo(res);

    pw = getpwnam(user);                    /* look up a user by name */
    if (pw)
        printf("%s: uid %d\n", user, (int)pw->pw_uid);
    else
        printf("%s: no such user\n", user);
    return 0;
}
