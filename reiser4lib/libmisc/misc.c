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
    0 - exact key has not been found. key on *pos < then;
*/
int reiserfs_misc_bin_search(
    void *needle,		    /* element to be found */
    void *array,		    /* array search will be performed on */ 
    uint32_t count,		    /* array size */
    reiserfs_elem_func_t elem_func, /* function for getting next element from given array */
    reiserfs_comp_func_t comp_func, /* function for comparing needle and a lement from array */
    uint64_t *pos)		    /* pointer on found pos a result will be stored in */
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
	if (!(elem = elem_func(array, j))) {
	    *pos = -1;
	    return -1;
	}
	
        if ((ret = comp_func(elem, needle)) < 0) { 
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

    /* lbound == j, set *pos on the position which element less than "needle" 
       on the base of the last search  */
    *pos = lbound - (ret >= 0);

    return 0;
}

int reiserfs_misc_comp_keys(void *key1, void *key2) {
    return -1;
}

