/*
 * These have been stolen somewhere from linux
 */

#include <aal/endian.h>

int le_set_bit (int nr, void * addr)
{
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p |= mask;
    /*sti();*/
    return retval;
}


int le_clear_bit (int nr, void * addr)
{
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p &= ~mask;
    /*sti();*/
    return retval;
}

int le_test_bit(int nr, const void * addr)
{
    uint8_t * p, mask;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    return ((mask & *p) != 0);
}

int be_set_bit (int nr, void * addr)
{
    uint8_t mask = 1 << (nr & 0x7);
    uint8_t *p = (uint8_t *) addr + (nr >> 3);
    uint8_t old = *p;

    *p |= mask;

    return (old & mask) != 0;
}

int be_clear_bit (int nr, void * addr)
{
    uint8_t mask = 1 << (nr & 0x07);
    uint8_t *p = (unsigned char *) addr + (nr >> 3);
    uint8_t old = *p;

    *p = *p & ~mask;
    return (old & mask) != 0;
}

int be_test_bit(int nr, const void * addr)
{
    const uint8_t *ADDR = (__const__ uint8_t *) addr;

    return ((ADDR[nr >> 3] >> (nr & 0x7)) & 1) != 0;
}

