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
    int64_t rbound, lbound, j;

    if (count == 0) {
        *pos = -1;
        return 0;
    }

    lbound = 0;
    rbound = count - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
	if (!(elem = elem_func(array, j, data))) {
	    *pos = -1;
	    return -1;
	}
	
        if ((ret = comp_func(elem, needle, data)) < 0) { 
            lbound = j + 1;
            continue;
        } else if (ret > 0) { 
            if (j == 0) 
		break;
            rbound = j - 1;
            continue;
        } else { 
            *pos = j;
            return 1;
        }
    }

    /* 
	set *pos on the position where elem less than "needle" 
	on the base of the last search.
    */
    *pos = j + (ret > 0);

    return 0;
}
