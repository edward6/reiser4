/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

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

int test_init_allocator(reiser4_space_allocator *, struct super_block *, void *arg);
int test_alloc_blocks(reiser4_space_allocator *,
		      reiser4_blocknr_hint *, int needed, reiser4_block_nr * start, reiser4_block_nr * len);
void test_dealloc_blocks(reiser4_space_allocator *, reiser4_block_nr start, reiser4_block_nr len);
void test_check_blocks(const reiser4_block_nr * start, const reiser4_block_nr * len, int desired);
void test_print_info(const char *, reiser4_space_allocator *);

/* __TEST_H__ */
#endif
