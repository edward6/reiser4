/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/** is it a real block number from real block device or fake block number for
 * not-yet-mapped object? */
int reiser4_blocknr_is_fake(const reiser4_disk_addr * da)
{
	return da->blk & REISER4_FAKE_BLOCKNR_BIT_MASK;
}

/** a generator for tree nodes fake block numbers */
static block_nr get_next_fake_blocknr ()
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
int reiser4_alloc_new_unf_blocks (int count) 
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();
	int ret = 0;

	spin_lock (&info_data->guard);

	if ((unsigned)count > info_data->blocks_free) 
		ret = -ENOSPC; 

	info_data->blocks_free -= count;

	spin_unlock (&info_data->guard);

	return ret;
}

/** take one allocate one block for formatted node */
int reiser4_alloc_new_block (block_nr * block)
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
	struct super_block * super = reiser4_get_current_context()->super;
	reiser4_super_info_data * info_data = reiser4_get_super_private(super);

	spin_lock (&info_data->guard);

	info_data->blocks_free += count;

	assert ("zam-395", reiser4_free_blocks(super) >= reiser4_data_blocks(super));

	spin_unlock (&info_data->guard);
}

/* real blocks allocation */
/** */
int reiser4_alloc_blocks (struct reiser4_blocknr_hint * hint UNUSED_ARG, block_nr *start, int *len)
{
	struct super_block      * super = reiser4_get_current_context()->super;
	reiser4_super_info_data * info_data = reiser4_get_super_private(super); 

	int      actual_len;

	block_nr search_start;
	block_nr search_end;

	assert ("zam-398", super != NULL);
	assert ("zam-397", *start < info_data->blocks_used);

	/* These blocks should have been allocated as "new", "not-yet-mapped"
	 * blocks, so we should not decrease blocks_free count twice. */

	/* first, we use *(@start) as a search start and search from this
	 * @start to the end of the disk */

	search_start = *start;
	search_end = *start + *len;

	if (search_end > info_data->blocks_used) 
		search_end = reiser4_data_blocks (super);

	actual_len = reiser4_bitmap_alloc (&search_start, search_end, 1, *len);

	if (actual_len != 0) goto out;

	/* next step is a scanning from 0 to search_start */
	search_end = search_start;
	search_start = 0;

	actual_len = reiser4_bitmap_alloc (&search_start, search_end, 1, *len);

 out:
	if (actual_len <= 0) return actual_len;

	*len = actual_len;
	*start = search_start;

	return 0;
} 

/** Block deallocation.  In current implementation it means move a node to
 * atom's DELETED SET. Working bitmap and `blocks_free' counter are not
 * modified in both cases of mapped and non-yet-mapped-to-disk nodes */
void reiser4_dealloc_block (jnode *node)
{
	assert ("zam-399", node != NULL);

	spin_lock_jnode(node);

	assert ("zam-400", node->atom != NULL);
	assert ("zam-401", !JF_ISSET(node, ZNODE_DELETED));

	if (JF_ISSET(node, ZNODE_UNFORMATTED)) {
		


	} else {
	}

	spin_unlock_jnode(node);
}


/* FIXME_ZAM: old block allocator function. they are here until new code is completed. */
int  allocate_new_blocks (block_nr *start, block_nr *len)
{
	static block_nr next_block = 1000;

	*start = next_block ++;
	*len   = 1;

	return 0;
}

void reiser4_free_block (block_nr block UNUSED_ARG)
{
	return;
}

int free_blocks (block_nr start UNUSED_ARG, block_nr len UNUSED_ARG)
{
	return 0;
}



/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
