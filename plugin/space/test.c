/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

/*
 * this file contains:
 * - dummy implementation of space allocation plugin, it is for debugging only
 */


/* plugin->u.space_allocator.init_allocator */
int test_init_allocator (reiser4_space_allocator * allocator,
			 struct super_block * super UNUSED_ARG, void * arg)
{
	reiser4_block_nr * next;

	assert ("vs-629", arg);
	next = arg;

	spin_lock_init (&allocator->u.test.guard);

	spin_lock (&allocator->u.test.guard);
	allocator->u.test.new_block_nr = *next;
	spin_unlock (&allocator->u.test.guard);

#if 0
	/* FIXME-GREEN This should be done by disk-layout */
	get_super_private( super ) -> blocks_free = ~(__u64)0;
#endif

	return 0;
}

/*
 * probability of getting blocks perfectly allocated (eek, floating point in kernel?)
 */
#define P 2

/* plugin->u.space_allocator.alloc_blocks */
int test_alloc_blocks (reiser4_space_allocator * allocator,
		       reiser4_blocknr_hint * hint, int needed,
		       reiser4_block_nr * start /* first of allocated blocks */,
		       reiser4_block_nr * num /* num of allocated blocks */)
{
	int p;
	reiser4_block_nr min_free;
	unsigned int rand;

	assert ("vs-460", needed > 0);


	spin_lock (&allocator->u.test.guard);

	/* minimal free block is stored in space allocator */
	min_free = allocator->u.test.new_block_nr;

	if (hint->blk < min_free) {
		/* hint is set such that we can not return
		 * desired, return at least desired amount */
		hint->blk = min_free;
	}

	p = 1 + (int) (10.0 * random () / (RAND_MAX + 1.0));
	if (p < P) {
		/*
		 * return what we were asked for
		 */
		*start = hint->blk;
		*num = needed;
	} else {
		/* return blocks not contiguous with hint->blk */
		*start = hint->blk + 3;
		/*
		 * choose amount of free blocks randomly in the range
		 * from 1 to needed
		 */
		rand = jiffies;
		*num = 1 + (int) ((double)(needed) * rand / (~0ul + 1.0));
	}

	min_free = *start + *num;

	/* update space allocator */
	if (min_free > reiser4_block_count (reiser4_get_current_sb ())) {
		allocator->u.test.new_block_nr = reiser4_block_count (reiser4_get_current_sb ());
		return -ENOSPC;
	}
	allocator->u.test.new_block_nr = min_free;

	/* update hint to next free */
	hint->blk = min_free;

	spin_unlock (&allocator->u.test.guard);

	return 0;
}


void test_dealloc_blocks (reiser4_space_allocator * allocator UNUSED_ARG,
			  reiser4_block_nr start UNUSED_ARG,
			  reiser4_block_nr len UNUSED_ARG)
{
	return;
}

void test_print_info (reiser4_space_allocator * allocator)
{
	spin_lock (&allocator->u.test.guard);
	info ("test space allocator: next free block is %lli\n",
	      allocator->u.test.new_block_nr);
	spin_unlock (&allocator->u.test.guard);
}
