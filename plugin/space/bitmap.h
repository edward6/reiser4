/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */


/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct reiser4_bnode {
	znode * working;	/* working bitmap block */
	znode * commit;		/* commit bitmap block */
};

typedef struct {
	/** an array for bitmap blocks direct access */
	struct reiser4_bnode * bitmap;
} bitmap_allocator;


/* declarations of functions implementing methods of space allocator plugin for
 * bitmap based allocator. The functions theirself are in bitmap.c */
int  bitmap_init_allocator    (reiser4_space_allocator *, struct super_block *,
			       void *);
int  bitmap_destroy_allocator (reiser4_space_allocator *, struct super_block *);
int  bitmap_alloc_blocks      (reiser4_space_allocator *,
			       reiser4_blocknr_hint *, int needed,
			       reiser4_block_nr *start, reiser4_block_nr *len);
void bitmap_dealloc_blocks    (reiser4_space_allocator *,
			       reiser4_block_nr start, reiser4_block_nr len);
