/*
    misc.h -- miscellaneous useful code.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <aal/aal.h>

typedef void *(*reiser4_elem_func_t) (void *, uint32_t, void *);
typedef int (*reiser4_comp_func_t) (void *, void *, void *);

extern int reiser4_misc_bin_search(void *array, uint32_t count, 
    void *needle, reiser4_elem_func_t elem_func, 
    reiser4_comp_func_t comp_func, void *, uint64_t *pos);

#endif

