/*
    misc.h -- miscellaneous useful code.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <aal/aal.h>

typedef void *(*reiserfs_elem_func_t) (void *, uint32_t);
typedef int (*reiserfs_comp_func_t) (const void *, const void *);

extern int reiserfs_misc_bin_search(void *needle, void *array, 
    uint32_t count, reiserfs_elem_func_t elem_func, 
    reiserfs_comp_func_t comp_func, uint64_t *pos);

#endif

