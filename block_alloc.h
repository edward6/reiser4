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
	block_nr blk;
};

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct reiser4_bnode {
	znode * working;	/* working bitmap block */
	znode * commit;		/* commit bitmap block */
};

extern int bitmap_alloc (block_nr *, block_nr, int, int);

extern int block_alloc_pre_commit_hook (txn_atom*);
extern int block_alloc_post_commit_hook (txn_atom*);
extern int block_alloc_post_writeback_hook (txn_atom*);

extern int reiser4_init_bitmap ();
extern void reiser4_done_bitmap ();

extern int reiser4_blocknr_is_fake (const reiser4_disk_addr *);

extern int reiser4_alloc_new_unf_blocks (int count);
extern int reiser4_alloc_new_block (block_nr * block);
extern void reiser4_dealloc_new_blocks (int count);

extern int reiser4_alloc_blocks (struct reiser4_blocknr_hint * hint UNUSED_ARG, 
				 block_nr * start, int * len);
extern void reiser4_dealloc_block (jnode *node);

/* FIXME_ZAM: these prototypes of old block allocator functions are here just
 * for now, until full support of new block allocator (it includes working
 * mkreiser4) is completed. */

extern int allocate_new_blocks (block_nr * start, block_nr *len);
extern void reiser4_free_block (block_nr block UNUSED_ARG);
extern int free_blocks (block_nr start UNUSED_ARG, block_nr len UNUSED_ARG);
extern void reiser4_free_block (block_nr block UNUSED_ARG);

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
