/* Copyright 2002, Hans Reiser */

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct reiser4_bnode {
	znode * working;	/* working bitmap block */
	znode * commit;		/* commit bitmap block */
};

extern int reiser4_bitmap_alloc (block_nr *, block_nr, int, int);

extern int reiser4_bitmap_prepare_commit (txn_atom*);
extern int reiser4_bitmap_done_commit (txn_atom*);
extern int reiser4_bitmap_done_writeback (txn_atom*);

extern int reiser4_init_bitmap ();
extern void reiser4_done_bitmap ();
