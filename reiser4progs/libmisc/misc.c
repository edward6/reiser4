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
//int reiserfs_misc_bin_search(
//    void *array,		    /* array search will be performed on */ 
//    uint32_t count,		    /* array size */
//    void *needle,		    /* element to be found */
//    reiserfs_elem_func_t elem_func, /* getting next element function */
//    reiserfs_comp_func_t comp_func, /* comparing function */
//    void *data,			    /* user-specified data will be passed to both callbacks */
//    uint64_t *pos)		    /* result position will be stored here */
/*{
    void *elem;
    int ret = 0;
    int64_t rbound, lbound, j;

    if (count == 0) {
        *pos = 0;
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

    *pos = j + (ret > 0);

    return 0;
}*/

int reiserfs_misc_bin_search(void *array, uint32_t count, void *needle,
    reiserfs_elem_func_t elem_func, reiserfs_comp_func_t comp_func,
    void *data, uint64_t *pos)
{
    void *elem;
    int res = 0;
    int left, right, i;

    if (count == 0) {
    	*pos = 0;
        return 0;
    }

    left = 0;
    right = count - 1;

    for (i = (right + left) / 2; left <= right; i = (right + left) / 2) {
	
	if (!(elem = elem_func(array, i, data)))
	    return -1;
	
	res = comp_func(elem, needle, data);
	if (res == -1) {
	    left = i + 1;
	    continue;
	} else if (res == 1) {
	    if (i == 0)
		break;
	    
	    right = i - 1;
	    continue;
	} else {
	    *pos = i;
	    return 1;
	}	
    }

    *pos = left;
    return 0;
}

