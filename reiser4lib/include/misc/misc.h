/*
    misc.h -- miscellaneous useful code.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <aal/aal.h>

typedef void *(get_element_to_comp_t)(void *, int64_t);
typedef int (comp_function_t)(void *, void *);

extern int reiserfs_bin_search (void * find_it, int64_t * ppos, 
    uint32_t count, void *entity, get_element_to_comp_t get_elem, 
    comp_function_t comp_func);
    
extern int reiserfs_comp_keys (void *key1, void *key2);

#endif
