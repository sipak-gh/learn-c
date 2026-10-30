/* byteio.c - turn integers into bytes and back, with a fixed byte order.
 * Never cast a byte pointer to (uint32_t *): the bytes may be misaligned,
 * the byte order depends on the CPU, and it breaks C's aliasing rules. */
#include <stdint.h>
#include <stdio.h>

static void put_u16_be(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static uint16_t get_u16_be(const unsigned char *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put_u32_be(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static uint32_t get_u32_be(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint32_t get_u32_le(const unsigned char *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
           (uint32_t)p[1] << 8 | (uint32_t)p[0];
}

static void dump(const char *what, const unsigned char *p, size_t n)
{
    printf("%-22s", what);
    for (size_t i = 0; i < n; i++)
        printf(" %02x", p[i]);
    putchar('\n');
}

int main(void)
{
    unsigned char buf[6];
    put_u16_be(buf, 0x1234);
    put_u32_be(buf + 2, 0xdeadbeef);   /* buf + 2 is not 4-byte aligned: fine */
    dump("big-endian on the wire:", buf, sizeof buf);

    printf("get_u16_be: 0x%04x\n", (unsigned)get_u16_be(buf));
    printf("get_u32_be: 0x%08lx\n", (unsigned long)get_u32_be(buf + 2));
    printf("get_u32_le: 0x%08lx (same bytes, other order)\n",
           (unsigned long)get_u32_le(buf + 2));

    uint32_t native = 0xdeadbeef;
    dump("0xdeadbeef in memory:", (const unsigned char *)&native, sizeof native);
    return 0;
}
