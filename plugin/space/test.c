/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/*
 * this file contains:
 * - dummy implementation of space allocation plugin, it is for debugging only
 */



/*
 * probability of getting blocks perfectly allocated (eek, floating point in kernel?)
 */
#define P 0.2


/* plugin->u.space_allocator.alloc_blocks */
int test_alloc_blocks (reiser4_blocknr_hint * hint, int needed,
		       reiser4_block_nr * start, int * len)
{
	double p;
	static reiser4_block_nr min_free = 10000;

	assert ("vs-460", *len > 0);


	if (hint->blk < min_free) {
		/* hint is set such that we can not return
		 * desired, return at least desired amount */
		hint->blk = min_free;
	}

	p = drand48 ();
	if (p < P) {
		/*
		 * return what we were asked for
		 */
		*start = hint->blk;
		*len = needed;
	} else {
		/* return blocks not contiguous with hint->blk */
		*start = hint->blk + 3;
		/*
		 * choose amount of free blocks randomly in the range
		 * from 1 to needed
		 */
		*len = 1 + (int) ((double)(needed) * rand () / (RAND_MAX + 1.0));
	}

	min_free = *start + *len;
	
	/* update hint to next free */
	hint->blk = min_free;
	return 0;
}


void test_dealloc_blocks (reiser4_block_nr start UNUSED_ARG,
			  int len UNUSED_ARG)
{
	return;
}

