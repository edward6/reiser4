/*
    misc.c -- miscellaneous useful code. 
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

/* 
    this implements binary search for 'find_it' among 'count' elements.
    return values: 
    1 - key on *ppos  found exact key on *ppos position; 
    0 - exect key has not been found. key on *ppos < then 
*/

#include <misc/misc.h>

int reiserfs_bin_search (
    void * find_it,                 /* element to be found */
    int64_t * ppos,                 /* return position */
    uint32_t count,                 /* count of elements to look through */
    void *entity,                   /* whose elements we are looking through */
    get_element_to_comp_t get_elem, /* function to get element */
    comp_function_t comp_func)      /* function to compare elements */
{
    int64_t rbound, lbound, j;
    int ret = 0;
    void * elem;

    if (count == 0) {
        *ppos = -1;
        return 0;
    }

    lbound = 0;
    rbound = count - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
	elem = get_elem(entity, j);
;
	if (elem == NULL) {
	    *ppos = -1;
	    return -1;
	}
	
        ret =  comp_func (elem, find_it);
        if (ret < 0) { 
	    /* second is greater */
            lbound = j + 1;
            continue;
        } else if (ret > 0) { 
	    /* first is greater */
            if (j == 0)
                break;
            rbound = j - 1;
            continue;
        } else { 
	    /* equal */
            *ppos = j;
            return 1;
        }
    }

    /* lbound == j, set *ppos on the position which element less than 'find_it' 
       on the base of the last search  */
    *ppos = lbound - (ret >= 0);

    return 0;
}

int reiserfs_comp_keys (void *key1, void *key2) {
    return -1;
}


