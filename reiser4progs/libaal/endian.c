/*
    Bits working functions. They are used by endianess macro.
    Some part of this code was stolen somewhere from linux.
*/

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

/* 
    Turns on specified bit in passed bit array. This variant of function is used 
    for little endian machines.
*/
inline int aal_le_set_bit(
    int nr,		    /* bit number to be set */
    void *addr		    /* bit array pointer */
) {
    int val;
    uint8_t *p, mask;

    p = (uint8_t *)addr;
    p += nr >> 3;
    
    mask = 1 << (nr & 0x7);
    val = (mask & *p) != 0;
    *p |= mask;
    
    return val;
}

/* 
    Clears specified bit in passed bit array. This variant of function is used 
    for little endian machines.
*/
inline int aal_le_clear_bit(
    int nr,		    /* bit number to be clean */
    void *addr		    /* bit array pointer */
) {
    int val;
    uint8_t *p, mask;

    p = (uint8_t *)addr;
    p += nr >> 3;
    
    mask = 1 << (nr & 0x7);
    val = (mask & *p) != 0;
    *p &= ~mask;
    
    return val;
}

/* 
    Tests specified bit in passed bit array. This variant of function is used 
    for little endian machines.
*/
inline int aal_le_test_bit(
    int nr,		    /* bit number to be tested */
    const void *addr	    /* bit array pointer */
) {
    uint8_t *p, mask;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    
    return ((mask & *p) != 0);
}

/* 
    Sets specified bit in passed bit array. This variant of function is used 
    for big endian machines.
*/
inline int aal_be_set_bit(
    int nr,		    /* bit number to be set */
    void *addr		    /* pointer to bit array */
) {
    uint8_t mask = 1 << (nr & 0x7);
    uint8_t *p = (uint8_t *) addr + (nr >> 3);
    uint8_t old = *p;

    *p |= mask;

    return (old & mask) != 0;
}

/* 
    Clears specified bit in passed bit array. This variant of function is used 
    for big endian machines.
*/
inline int aal_be_clear_bit(
    int nr,		    /* bit number to be clean */
    void *addr		    /* pointer to bit array */
) {
    uint8_t mask = 1 << (nr & 0x07);
    uint8_t *p = (unsigned char *) addr + (nr >> 3);
    uint8_t old = *p;

    *p = *p & ~mask;
    return (old & mask) != 0;
}

/* 
    Tests specified bit in passed bit array. This variant of function is used 
    for big endian machines.
*/
inline int aal_be_test_bit(
    int nr,		    /* bit number to be tested */
    const void *addr	    /* bit array pointer */
) {
    const uint8_t *arr = (const uint8_t *)addr;
    return ((arr[nr >> 3] >> (nr & 0x7)) & 1) != 0;
}

