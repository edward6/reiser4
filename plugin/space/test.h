/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

int test_alloc_blocks (reiser4_blocknr_hint * hint, int needed,
		       reiser4_block_nr * start, int * len);
void test_dealloc_blocks (reiser4_block_nr start, int len);
