/* byteorder.c - how a 16-bit port number is stored in memory. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>

static void show(const char *label, uint16_t value)
{
    const unsigned char *b = (const unsigned char *)&value;
    printf("%-22s 0x%04x  bytes in memory: %02x %02x\n", label, value, b[0], b[1]);
}

int main(void)
{
    uint16_t port = 6667;               /* 0x1a0b */
    show("host order:", port);
    show("htons (network order):", htons(port));
    show("ntohs(htons(port)):", ntohs(htons(port)));

    uint32_t one = 1;
    printf("this CPU is %s-endian\n",
           *(const unsigned char *)&one == 1 ? "little" : "big");
    return 0;
}
