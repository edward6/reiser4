/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

typedef struct {
	reiser4_block_nr new_block_nr;
} test_space_allocator;


int test_init_allocator (reiser4_space_allocator * allocator,
			 struct super_block * super, void * arg);
int test_alloc_blocks (reiser4_blocknr_hint * hint, int needed,
		       reiser4_block_nr * start, reiser4_block_nr * len);
void test_dealloc_blocks (reiser4_block_nr start, reiser4_block_nr len);
