/*
    misc.c -- miscellaneous useful code. 
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <misc/misc.h>

/* 
    This implements binary search for "needle" among "count" elements.
    
    Return values: 
    1 - key on *pos found exact key on *pos position; 
    0 - exact key has not been found. key of *pos < then wanted.
*/
int reiserfs_misc_bin_search(
    void *array,		    /* array search will be performed on */ 
    uint32_t count,		    /* array size */
    void *needle,		    /* element to be found */
    reiserfs_elem_func_t elem_func, /* getting next element function */
    reiserfs_comp_func_t comp_func, /* comparing function */
    void *data,			    /* user-specified data will be passed to both callbacks */
    uint64_t *pos)		    /* result position will be stored here */
{
    void *elem;
    int ret = 0;
    int right, left, j;

    if (count == 0) {
        *pos = 0;
        return 0;
    }

    left = 0;
    right = count - 1;

    for (j = (right + left) / 2; left <= right; j = (right + left) / 2) {
	if (!(elem = elem_func(array, j, data)))
	    return -1;
	
        if ((ret = comp_func(elem, needle, data)) < 0) { 
            left = j + 1;
            continue;
        } else if (ret > 0) { 
            if (j == 0) 
		break;
            right = j - 1;
            continue;
        } else { 
            *pos = j;
            return 1;
        }
    }

    *pos = left;
    return 0;
}

