/*
    bitops.c -- bitops functions. They are used for working with bitmap.
    Copyright (C) 1996-2002 Hans Reiser.
    Some parts of this code stolen somewhere from linux.
*/

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#include <aal/endian.h>
#include <misc/bitops.h>

static inline int reiserfs_misc_le_set_bit(int nr, void *addr) {
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);

    retval = (mask & *p) != 0;
    *p |= mask;

    return retval;
}

static inline int reiserfs_misc_le_clear_bit(int nr, void *addr) {
    uint8_t * p, mask;
    int retval;

    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);

    retval = (mask & *p) != 0;
    *p &= ~mask;

    return retval;
}

static inline int reiserfs_misc_le_test_bit(int nr, const void *addr) {
    uint8_t *p, mask;
  
    p = (uint8_t *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    return ((mask & *p) != 0);
}

static inline int reiserfs_misc_le_find_first_zero_bit(const void *vaddr, 
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
	if (!reiserfs_misc_test_bit(res, p)) break;
    
    return (p - addr) * 8 + res;
}

static inline int reiserfs_misc_le_find_next_zero_bit(const void *vaddr, 
    unsigned size, unsigned offset) 
{
    const uint8_t *addr = vaddr;
    const uint8_t *p = addr + (offset >> 3);
    int bit = offset & 7, res;
  
    if (offset >= size)
	return size;
  
    if (bit) {
	for (res = bit; res < 8; res++)
	    if (!reiserfs_misc_test_bit (res, p))
		return (p - addr) * 8 + res;
	p++;
    }

    res = reiserfs_misc_find_first_zero_bit (p, size - 8 * (p - addr));
    return (p - addr) * 8 + res;
}

static inline int reiserfs_misc_be_set_bit(int nr, void *addr) {
    uint8_t mask = 1 << (nr & 0x7);
    uint8_t *p = (uint8_t *) addr + (nr >> 3);
    uint8_t old = *p;

    *p |= mask;

    return (old & mask) != 0;
}
 
static inline int reiserfs_misc_be_clear_bit(int nr, void *addr) {
    uint8_t mask = 1 << (nr & 0x07);
    uint8_t *p = (unsigned char *) addr + (nr >> 3);
    uint8_t old = *p;
 
    *p = *p & ~mask;
    return (old & mask) != 0;
}
 
static inline int reiserfs_misc_be_test_bit(int nr, const void *addr) {
    const uint8_t *a = (__const__ uint8_t *)addr;
 
    return ((a[nr >> 3] >> (nr & 0x7)) & 1) != 0;
}
 
static inline int reiserfs_misc_be_find_first_zero_bit(const void *vaddr, unsigned size) {
    return reiserfs_misc_find_next_zero_bit(vaddr, size, 0);
}

static inline unsigned long reiserfs_misc_ffz(unsigned long word) {
    unsigned long result = 0;
 
    while(word & 1) {
        result++;
        word >>= 1;
    }
    return result;
}

static inline int reiserfs_misc_be_find_next_zero_bit(const void *vaddr, unsigned size, 
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
    return result + reiserfs_misc_ffz(aal_swap32(tmp) | (~0ul< size));
found_middle:
    return result + reiserfs_misc_ffz(aal_swap32(tmp));
}

inline int reiserfs_misc_set_bit(int nr, void *addr) {
#ifndef WORDS_BIGENDIAN    
    return reiserfs_misc_le_set_bit(nr, addr);
#else 
    return reiserfs_misc_be_set_bit(nr, addr);
#endif 
    return 0;
}

inline int reiserfs_misc_clear_bit(int nr, void *addr) {
#ifndef WORDS_BIGENDIAN    
    return reiserfs_misc_le_clear_bit(nr, addr);
#else 
    return reiserfs_misc_be_clear_bit(nr, addr);
#endif 
    return 0;
}

inline int reiserfs_misc_test_bit(int nr, const void *addr) {
#ifndef WORDS_BIGENDIAN    
    return reiserfs_misc_le_test_bit(nr, addr);
#else 
    return reiserfs_misc_be_test_bit(nr, addr);
#endif 
    return 0;
}

inline int reiserfs_misc_find_first_zero_bit(const void *vaddr, 
    unsigned size) 
{
#ifndef WORDS_BIGENDIAN    
    return reiserfs_misc_le_find_first_zero_bit(vaddr, size);
#else 
    return reiserfs_misc_be_find_first_zero_bit(vaddr, size);
#endif 
    return 0;
}

inline int reiserfs_misc_find_next_zero_bit(const void *vaddr, 
    unsigned size, unsigned offset) 
{
#ifndef WORDS_BIGENDIAN    
    return reiserfs_misc_le_find_next_zero_bit(vaddr, size, offset);
#else 
    return reiserfs_misc_be_find_next_zero_bit(vaddr, size, offset);
#endif 
    return 0;
}
