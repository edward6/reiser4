/*
    bitops.h -- bitops functions
    Some parts of this code was stolen somewhere from linux.
*/

#ifndef BITOPS_H
#define BITOPS_H

#define _ROUND_UP(x,n) (((x)+(n)-1u) & ~((n)-1u))
#define ROUND_UP(x) _ROUND_UP(x,8LL)

extern inline int aal_set_bit (int nr, void *addr);
extern inline int aal_clear_bit (int nr, void *addr);
extern inline int aal_test_bit(int nr, const void *addr);
extern inline int aal_find_first_zero_bit (const void *vaddr, unsigned size);
extern inline int aal_find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset);

#endif

