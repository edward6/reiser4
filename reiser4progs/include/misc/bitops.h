/*
    bitops.h -- bitops functions
    Copyright (C) 1996-2002 Hans Reiser.
    Some parts of this code stolen somewhere from linux.
*/

#ifndef BITOPS_H
#define BITOPS_H

#define _ROUND_UP(x,n) (((x)+(n)-1u) & ~((n)-1u))
#define ROUND_UP(x) _ROUND_UP(x,8LL)

extern inline int reiserfs_misc_set_bit (int nr, void *addr);
extern inline int reiserfs_misc_clear_bit (int nr, void *addr);
extern inline int reiserfs_misc_test_bit(int nr, const void *addr);
extern inline int reiserfs_misc_find_first_zero_bit (const void *vaddr, unsigned size);
extern inline int reiserfs_misc_find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset);

#endif

