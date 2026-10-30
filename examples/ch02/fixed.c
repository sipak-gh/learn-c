/* fixed.c - exact-width integers from <stdint.h>, printed with <inttypes.h>. */
#include <inttypes.h>   /* includes <stdint.h> and adds the PRI... macros */
#include <stdio.h>

int main(void)
{
    uint8_t  level = 255;                   /* exactly 8 bits, unsigned */
    int16_t  delta = -300;                  /* exactly 16 bits, signed */
    uint32_t ip = 0x7F000001;               /* 127.0.0.1 as one number */
    int64_t  ms = INT64_C(86400000) * 365;  /* milliseconds in a year */
    uint64_t total = UINT64_MAX;
    size_t   n = sizeof total;              /* sizeof gives a size_t */

    printf("level %" PRIu8 ", delta %" PRId16 "\n", level, delta);
    printf("ip    %" PRIu32 " = 0x%08" PRIX32 "\n", ip, ip);
    printf("ms    %" PRId64 "\n", ms);
    printf("total %" PRIu64 " (%zu bytes)\n", total, n);
    printf("PRIu64 is \"%s\" here\n", PRIu64);
    return 0;
}
