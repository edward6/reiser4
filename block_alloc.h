/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__FS_REISER4_BLOCK_ALLOC_H__)
#define __FS_REISER4_BLOCK_ALLOC_H__

#define REISER4_FAKE_BLOCKNR_BIT_MASK   0x8000000000000000ULL
#define REISER4_BLOCKNR_STATUS_BIT_MASK 0xF000000000000000ULL
#define REISER4_UNALLOCATED_BIT_MASK    0xF000000000000000ULL
#define REISER4_BITMAP_BLOCKS_BIT_MASK  0x9000000000000000ULL


/** a hint for block allocator */
struct reiser4_blocknr_hint {
	/* FIXME: I think we want to add a longterm lock on the bitmap block here.  This
	 * is to prevent jnode_flush() calls from interleaving allocations on the same
	 * bitmap, once a hint is established. */
	reiser4_block_nr blk;	     /* search start hint */
	reiser4_block_nr max_dist;   /* if not zero, it is a region size we search for free blocks in */
};

extern void blocknr_hint_init (reiser4_blocknr_hint *hint);
extern void blocknr_hint_done (reiser4_blocknr_hint *hint);



int blocknr_is_fake(const reiser4_block_nr * da);
void get_next_fake_blocknr (reiser4_block_nr *bnr);
extern int reiser4_alloc_blocks (reiser4_blocknr_hint * hint,
				 reiser4_block_nr * start, reiser4_block_nr * len);
extern int alloc_blocknr (znode *neighbor, reiser4_block_nr *blocknr);



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
