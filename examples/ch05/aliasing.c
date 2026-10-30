/* aliasing.c - look at an object's bytes the allowed way. */
#include <stdio.h>

int main(void)
{
    float f = 1.0f;
    unsigned int n = 0x11223344;

    /* Any object may be read through an unsigned char pointer. */
    const unsigned char *b = (const unsigned char *)&f;
    printf("bytes of 1.0f:      ");
    for (size_t i = 0; i < sizeof f; i++)
        printf("%02x ", b[i]);
    printf("\n");

    b = (const unsigned char *)&n;
    printf("bytes of 0x11223344: ");
    for (size_t i = 0; i < sizeof n; i++)
        printf("%02x ", b[i]);
    printf("(little-endian: lowest byte first)\n");
    return 0;
}
