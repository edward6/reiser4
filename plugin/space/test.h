/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __TEST_H__
#define __TEST_H__

#include "../../forward.h"
#include "../../dformat.h"

#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>

typedef struct {
	spinlock_t guard;
	reiser4_block_nr new_block_nr;
} test_space_allocator;

int init_allocator_test(reiser4_space_allocator *, struct super_block *, void *arg);
int alloc_blocks_test(reiser4_space_allocator *,
		      reiser4_blocknr_hint *, int needed, reiser4_block_nr * start, reiser4_block_nr * len);
void dealloc_blocks_test(reiser4_space_allocator *, reiser4_block_nr start, reiser4_block_nr len);
void check_blocks_test(const reiser4_block_nr * start, const reiser4_block_nr * len, int desired);
void print_info_test(const char *, reiser4_space_allocator *);

/* __TEST_H__ */
#endif
