/*
    bitops.c -- bitops functions.
    Some parts of this code stolen somewhere from linux.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#include <aal/aal.h>

/* 
    These functions are standard bitopts functions for both (big and little 
    endian) systems. As endianess of system is determining durring configure
    of package, we are using WORDS_BIGENDIAN macro for complilation time 
    determinig system endianess. This way is more preffered as system endianess
    doesn't changing in runtime :)
*/

#ifndef WORDS_BIGENDIAN

static inline int aal_le_set_bit(int nr, void *addr) {
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);

    retval = (mask & *p) != 0;
    *p |= mask;

    return retval;
}

static inline int aal_le_clear_bit(int nr, void *addr) {
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);

    retval = (mask & *p) != 0;
    *p &= ~mask;

    return retval;
}

static inline int aal_le_test_bit(int nr, const void *addr) {
    uint8_t *p, mask;
  
    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    return ((mask & *p) != 0);
}

static inline int aal_le_find_first_zero_bit(const void *vaddr, 
    unsigned size) 
{
    const uint8_t *p = vaddr, *addr = vaddr;
    int res;

    if (!size)
	return 0;

    size = (size >> 3) + ((size & 0x7) > 0);
    while (*p++ == 255) {
	if (--size == 0)
	    return (p - addr) << 3;
    }
  
    --p;
    for (res = 0; res < 8; res++)
	if (!aal_le_test_bit(res, p)) break;
    
    return (p - addr) * 8 + res;
}

static inline int aal_le_find_next_zero_bit(const void *vaddr, 
    unsigned size, unsigned offset) 
{
    const uint8_t *addr = vaddr;
    const uint8_t *p = addr + (offset >> 3);
    int bit = offset & 7, res;
  
    if (offset >= size)
	return size;
  
    if (bit) {
	for (res = bit; res < 8; res++)
	    if (!aal_le_test_bit (res, p))
		return (p - addr) * 8 + res;
	p++;
    }

    res = aal_le_find_first_zero_bit(p, size - 8 * (p - addr));
    return (p - addr) * 8 + res;
}

#else

static inline int aal_be_set_bit(int nr, void *addr) {
    uint8_t mask = 1 << (nr & 0x7);
    uint8_t *p = (uint8_t *) addr + (nr >> 3);
    uint8_t old = *p;

    *p |= mask;

    return (old & mask) != 0;
}
 
static inline int aal_be_clear_bit(int nr, void *addr) {
    uint8_t mask = 1 << (nr & 0x07);
    uint8_t *p = (unsigned char *) addr + (nr >> 3);
    uint8_t old = *p;
 
    *p = *p & ~mask;
    return (old & mask) != 0;
}
 
static inline int aal_be_test_bit(int nr, const void *addr) {
    const uint8_t *a = (__const__ uint8_t *)addr;
 
    return ((a[nr >> 3] >> (nr & 0x7)) & 1) != 0;
}
 
static inline int aal_be_find_first_zero_bit(const void *vaddr, unsigned size) {
    return aal_be_find_next_zero_bit(vaddr, size, 0);
}

static inline unsigned long aal_ffz(unsigned long word) {
    unsigned long result = 0;
 
    while(word & 1) {
        result++;
        word >>= 1;
    }
    return result;
}

static inline int aal_be_find_next_zero_bit(const void *vaddr, unsigned size, 
    unsigned offset) 
{
    uint32_t *p = ((uint32_t *) vaddr) + (offset >> 5);
    uint32_t result = offset & ~31ul;
    uint32_t tmp;

    if (offset >= size)
        return size;

    size -= result;
    offset &= 31ul;
    if (offset) {
        tmp = *(p++);
        tmp |= aal_swap32(~0ul >> (32 - offset));
	
        if (size < 32)
	    goto found_first;
	
	if (~tmp)
	    goto found_middle;
	
	size -= 32;
	result += 32;
    }
    
    while (size & ~31ul) {
    
        if (~(tmp = *(p++)))
            goto found_middle;
	    
	result += 32;
	size -= 32;
    }
    
    if (!size)
        return result;
    
    tmp = *p;

found_first:
    return result + aal_ffz(aal_swap32(tmp) | (~0ul< size));
found_middle:
    return result + aal_ffz(aal_swap32(tmp));
}

#endif

/* Public wrappers for all bitops functions. */
inline int aal_set_bit(int nr, void *addr) {
#ifndef WORDS_BIGENDIAN    
    return aal_le_set_bit(nr, addr);
#else 
    return aal_be_set_bit(nr, addr);
#endif 
    return 0;
}

inline int aal_clear_bit(int nr, void *addr) {
#ifndef WORDS_BIGENDIAN    
    return aal_le_clear_bit(nr, addr);
#else 
    return aal_be_clear_bit(nr, addr);
#endif 
    return 0;
}

inline int aal_test_bit(int nr, const void *addr) {
#ifndef WORDS_BIGENDIAN    
    return aal_le_test_bit(nr, addr);
#else 
    return aal_be_test_bit(nr, addr);
#endif 
    return 0;
}

inline int aal_find_first_zero_bit(const void *vaddr, 
    unsigned size) 
{
#ifndef WORDS_BIGENDIAN    
    return aal_le_find_first_zero_bit(vaddr, size);
#else 
    return aal_be_find_first_zero_bit(vaddr, size);
#endif 
    return 0;
}

inline int aal_find_next_zero_bit(const void *vaddr, 
    unsigned size, unsigned offset) 
{
#ifndef WORDS_BIGENDIAN    
    return aal_le_find_next_zero_bit(vaddr, size, offset);
#else 
    return aal_be_find_next_zero_bit(vaddr, size, offset);
#endif 
    return 0;
}
