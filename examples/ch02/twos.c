/* twos.c - how negative numbers are stored: two's complement. */
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    int8_t one = 1, max = 127, minus1 = -1, minus2 = -2, min = -128;

    /* Converting to uint8_t keeps the same 8 bits; %02X shows them in hex. */
    printf("%4d = 0x%02X\n", one, (unsigned)(uint8_t)one);
    printf("%4d = 0x%02X\n", max, (unsigned)(uint8_t)max);
    printf("%4d = 0x%02X\n", minus1, (unsigned)(uint8_t)minus1);
    printf("%4d = 0x%02X\n", minus2, (unsigned)(uint8_t)minus2);
    printf("%4d = 0x%02X\n", min, (unsigned)(uint8_t)min);

    int32_t wide = minus1;           /* sign extension: the sign bit is copied */
    printf("int8_t -1 as int32_t: %" PRId32 " = 0x%08" PRIX32 "\n", wide, (uint32_t)wide);
    return 0;
}
