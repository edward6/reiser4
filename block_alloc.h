/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__FS_REISER4_BLOCK_ALLOC_H__)
#define __FS_REISER4_BLOCK_ALLOC_H__

#define REISER4_FAKE_BLOCKNR_BIT_MASK   0x8000000000000000ULL;
#define REISER4_BLOCKNR_STATUS_BIT_MASK 0xF000000000000000ULL;
#define REISER4_UNALLOCATED_BIT_MASK    0xF000000000000000ULL;
#define REISER4_BITMAP_BLOCKS_BIT_MASK  0x9000000000000000ULL;

/** a hint for block allocator */
struct reiser4_blocknr_hint {
	/* FIXME_ZAM: This structure is not used yet for passing of real
	 * parameters */
};

extern int reiser4_blocknr_is_fake (const reiser4_disk_addr *);

extern int reiser4_alloc_new_unf_blocks (int count);
extern int reiser4_alloc_new_block (block_nr * block);
extern void reiser4_dealloc_new_blocks (int count);

extern int reiser4_alloc_blocks (struct reiser4_blocknr_hint * hint UNUSED_ARG, 
				 block_nr * start, int * len);
extern void reiser4_dealloc_block (jnode *node);

#endif /* __FS_REISER4_BLOCK_ALLOC_H__ */

/* FIXME_ZAM: these prototypes of old block allocator functions are here just
 * for now, until full support of new block allocator (it includes working
 * mkreiser4) is completed. */

extern int allocate_new_blocks (block_nr * start, block_nr *len);
extern void reiser4_delete_block (block_nr block UNUSED_ARG);
extern int free_blocks (block_nr start UNUSED_ARG, block_nr len UNUSED_ARG);

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
