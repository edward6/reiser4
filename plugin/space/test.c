/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../../super.h"
#include "../../block_alloc.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "space_allocator.h"
#include "test.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

/* this file contains:
   - dummy implementation of space allocation plugin, it is for debugging only */

/* plugin->u.space_allocator.init_allocator */
int
init_allocator_test(reiser4_space_allocator * allocator, struct super_block *super UNUSED_ARG, void *arg)
{
	reiser4_block_nr *next;

	assert("vs-629", arg);
	next = arg;

	spin_lock_init(&allocator->u.test.guard);

	spin_lock(&allocator->u.test.guard);
	allocator->u.test.new_block_nr = *next;
	spin_unlock(&allocator->u.test.guard);

#if 0
	/* FIXME-GREEN This should be done by disk-layout */
	get_super_private(super)->blocks_free = ~(__u64) 0;
#endif

	return 0;
}

/* Probability of getting blocks perfectly allocated (eek, floating point in kernel?)
   When it is 10, you get what you asked for. */
#define P 10

/* plugin->u.space_allocator.alloc_blocks */
int
alloc_blocks_test(reiser4_space_allocator * allocator, reiser4_blocknr_hint * hint, int needed,
		  reiser4_block_nr * start, /* first of allocated blocks */
		  reiser4_block_nr * num /* num of allocated blocks */ )
{
	int p;
	reiser4_block_nr min_free;
	unsigned int rand;

	assert("vs-460", needed > 0);

	spin_lock(&allocator->u.test.guard);

	/* minimal free block is stored in space allocator */
	min_free = allocator->u.test.new_block_nr;

	if (hint->blk < min_free) {
		/* hint is set such that we can not return
		   desired, return at least desired amount */
		hint->blk = min_free;
	}
#ifndef __KERNEL__
	p = 1 + (int) (10.0 * random() / (RAND_MAX + 1.0));
#else
	p = jiffies % 10;
#endif
	if (1 /*p <= P */ ) {
		/* return what we were asked for */
		*start = hint->blk;
		*num = needed;
	} else {
		assert("jmacd-10081", P != 10);

		/* return blocks not contiguous with hint->blk */
		*start = hint->blk + 3;
		/* choose amount of free blocks randomly in the range
		   from 1 to needed */
		rand = jiffies;
		*num = 1 + (int) ((double) (needed) * rand / (~0ul + 1.0));
	}

	min_free = *start + *num;

	/* update space allocator */
	if (min_free > reiser4_block_count(reiser4_get_current_sb())) {
		allocator->u.test.new_block_nr = reiser4_block_count(reiser4_get_current_sb());
		spin_unlock(&allocator->u.test.guard);

		ON_TRACE(TRACE_ALLOC, "test_alloc_blocks: "
			 "asked for %d blocks from %llu. ENOSPC returned\n", needed, hint->blk);
		ON_TRACE(TRACE_ALLOC, "test_alloc_blocks: "
			 "next free is %llu, block count %llu, free %llu\n",
			 allocator->u.test.new_block_nr,
			 reiser4_block_count
			 (reiser4_get_current_sb()), reiser4_free_blocks(reiser4_get_current_sb()));

		return RETERR(-ENOSPC);
	}
	allocator->u.test.new_block_nr = min_free;

	ON_TRACE(TRACE_ALLOC, "test_alloc_blocks: "
		 "asked for %d blocks from %llu - got %llu from %llu\n",
		 needed, hint->blk, *num, *start);
	ON_TRACE(TRACE_ALLOC, "test_alloc_blocks: "
		 "next free is %llu, block count %llu, free %llu\n",
		 allocator->u.test.new_block_nr,
		 reiser4_block_count(reiser4_get_current_sb
				     ()), reiser4_free_blocks(reiser4_get_current_sb()));

	/* update hint to next free */
	hint->blk = min_free;

	spin_unlock(&allocator->u.test.guard);

	return 0;
}

void
dealloc_blocks_test(reiser4_space_allocator * allocator UNUSED_ARG,
		    reiser4_block_nr start UNUSED_ARG, reiser4_block_nr len UNUSED_ARG)
{
	ON_TRACE(TRACE_ALLOC, "test_dealloc_blocks: %llu blocks from %llu\n", start, len);
	return;
}

void
check_blocks_test(const reiser4_block_nr * start, const reiser4_block_nr * len, int desired)
{
	test_space_allocator *allocator;

	allocator = &(get_space_allocator(reiser4_get_current_sb())->u.test);

	spin_lock(&allocator->guard);

	if (desired)
		/* we only can check that those blocks will never be allocated */
		assert("vs-836", *start + *len <= allocator->new_block_nr);
	else
		assert("vs-837", *start >= allocator->new_block_nr);

	spin_unlock(&allocator->guard);
}

void
print_info_test(const char *str, reiser4_space_allocator * allocator)
{
	spin_lock(&allocator->u.test.guard);
	printk("%s: test space allocator: next free block is %lli\n", str, allocator->u.test.new_block_nr);
	spin_unlock(&allocator->u.test.guard);
}
