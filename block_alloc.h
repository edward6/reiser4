/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__FS_REISER4_BLOCK_ALLOC_H__)
#define __FS_REISER4_BLOCK_ALLOC_H__

/* Mask when is applied to given block number shows is that block number is a fake one */
#define REISER4_FAKE_BLOCKNR_BIT_MASK   0x8000000000000000ULL
/* Mask which isolates a type of object this fake block number was assigned to */
#define REISER4_BLOCKNR_STATUS_BIT_MASK 0xF000000000000000ULL


/*result after applying the REISER4_BLOCKNR_STATUS_BIT_MASK should be compared
 * against these two values to understand is the object unallocated or bitmap
 * shadow object (WORKING BITMAP block, look at the plugin/space/bitmap.c) */
#define REISER4_UNALLOCATED_STATUS_VALUE    0xF000000000000000ULL
#define REISER4_BITMAP_BLOCKS_STATUS_VALUE  0x9000000000000000ULL

/* specification how block allocation was counted in sb block counters */
typedef enum { 
	BLOCK_NOT_COUNTED = 0,	/* reiser4 has no info about this block yet */
	BLOCK_GRABBED = 1,	/* free space grabbed for further allocation
				 * of this block */
	BLOCK_UNALLOCATED = 3,	/* block is used for existing in-memory object
				 * ( unallocated formatted or unformatted
				 * node) */
	BLOCK_ALLOCATED = 4	/* block is mapped to disk, real on-disk block
				 * number assigned */
} block_stage_t;

/** a hint for block allocator */
struct reiser4_blocknr_hint {
	/* FIXME: I think we want to add a longterm lock on the bitmap block here.  This
	 * is to prevent jnode_flush() calls from interleaving allocations on the same
	 * bitmap, once a hint is established. */

	/* search start hint */
	reiser4_block_nr blk;
	/* if not zero, it is a region size we search for free blocks in */
	reiser4_block_nr max_dist;
	/* level for allocation, may be useful have branch-level and higher
	 * write-optimized. */
	tree_level       level;
	/* block allocator assumes that blocks, which will be mapped to disk,
	 * are in this specified block_stage */
	block_stage_t    block_stage;
};

extern void blocknr_hint_init (reiser4_blocknr_hint *hint);
extern void blocknr_hint_done (reiser4_blocknr_hint *hint);

extern int reiser4_grab_space (reiser4_block_nr *, __u64, __u64);
extern int reiser4_grab_space1 (__u64);
extern void reiser4_release_grabbed_space (__u64 count);
extern void reiser4_release_all_grabbed_space (void);

extern void reiser4_count_fake_allocation   (__u64);
extern void reiser4_count_fake_deallocation (__u64);

extern void reiser4_count_block_mapping     (__u64);
extern void reiser4_count_block_unmapping   (__u64);

extern void reiser4_count_real_allocation   (__u64);
extern void reiser4_count_real_deallocation (__u64);


extern int blocknr_is_fake(const reiser4_block_nr * da);
extern int reiser4_alloc_blocks (reiser4_blocknr_hint * hint,
				 reiser4_block_nr * start, reiser4_block_nr * len);
extern int reiser4_dealloc_blocks (const reiser4_block_nr *, const reiser4_block_nr *, int, block_stage_t);

static inline int reiser4_dealloc_block (const reiser4_block_nr *block, int defer, block_stage_t stage)
{
	const reiser4_block_nr one = 1;
	return reiser4_dealloc_blocks (block, &one, defer, stage);
}

extern int assign_fake_blocknr (reiser4_block_nr *);
extern void release_blocknr (reiser4_block_nr *);

extern void pre_commit_hook      (void);
extern void post_commit_hook     (void);
extern void post_write_back_hook (void);

#endif /* __FS_REISER4_BLOCK_ALLOC_H__ */

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
