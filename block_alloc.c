/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

#define REISER4_FAKE_BLOCKNR_BIT_MASK   0x8000000000000000ULL;
#define REISER4_BLOCKNR_STATUS_BIT_MASK 0xF000000000000000ULL;
#define REISER4_UNALLOCATED_BIT_MASK    0xF000000000000000ULL;
#define REISER4_BITMAP_BLOCKS_BIT_MASK  0x9000000000000000ULL;

/** is it a real block number from real block device or fake block number for
 * not-yet-mapped object? */
int reiser4_blocknr_is_fake(const reiser4_disk_addr * da)
{
	return da->blk & REISER4_FAKE_BLOCKNR_BIT_MASK;
}

/** a generator for tree nodes fake block numbers */
static block_nr get_next_fake_blocknr ()
int allocate_new_blocks (block_nr * hint,
			 block_nr * count)
{
	static block_nr gen = 0;

	++ gen;

	gen &= ~REISER4_BLOCKNR_STATUS_BIT_MASK;
	gen |= REISER4_UNALLOCATED_BIT_MASK;

#if REISER4_DEBUG
	{
		reiser4_disk_addr da = {.blk = gen};
		znode * node;

		node = zlook(current_tree, &da, 0);
		assert ("zam-394", node == NULL);
	}
#endif

	return gen;
}

/* There are two kinds of block allocation functions: first kind which
 * allocates fake blocks or just count disk space (in case of new unformatted
 * nodes allocation), and another kind which does search in disk bitmap for
 * real disk block numbers. 
 * 
 * FIXME_ZAM: There would be nice to hide all this fake/real block allocation
 * details from caller code. */

/** Count disk space allocated for unformatted nodes. Because unformatted
 * nodes do not need block numbers (even fake ones) we do not call
 * get_next_fake_blocknr()  */
int reiser4_allocate_new_unf_blocks (block_nr count) 
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();
	int ret = 0;

	spin_lock (&info_data->guard);

	if (count > info_data->blocks_free) 
		ret = -ENOSPC; 

	info_data->blocks_free -= count;

	spin_unlock (&info_data->guard);

	return ret;
}

/** take one allocate one block for formatted node */
int reiser4_allocate_new_block (block_nr * block)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();
	int ret = 0;

	spin_lock (&info_data->guard);

	if (info_data->blocks_free == 0)
		ret = -ENOSPC;

	-- info_data->blocks_free;
	*block = get_next_fake_blocknr();

	spin_unlock (&info_data->guard);

	return ret;
}

void reiser4_dealloc_new_blocks (int count)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();

	spin_lock (&info_data->guard);

	info_data->blocks_free += count;

	assert ("zam-395", info_data->blocks_free >= info_data->used_blocks);

	spin_unlock (&info_data->guard);
}

/* real blocks allocation */

/** */
int reiser4_allocate_blocks (struct reiser4_blocknr_hint * hint, block_nr *start, block_nr *len)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();
	
}


/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
