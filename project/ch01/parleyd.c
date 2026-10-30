/* parleyd.c - the parley chat server (Chapter 1: it only introduces itself). */
#include <stdio.h>

int main(void)
{
    printf("parleyd 0.1 - a tiny IRC-style chat server\n");
    printf("usage: parleyd [-p PORT] [-b ADDR] [-m MAXCLIENTS] [-v]\n");
    return 0;
}
