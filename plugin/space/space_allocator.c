/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"
#include "bitmap.h"

/* initialization of objectid space allocator plugins */
reiser4_plugin space_plugins[ LAST_SPACE_ALLOCATOR_ID ] = {
	[ BITMAP_SPACE_ALLOCATOR_ID ] = {
		.space_allocator = {
			.h = {
				.type_id = REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE,
				.id      = BITMAP_SPACE_ALLOCATOR_ID,
				.pops    = NULL,
				.label   = "bitmap",
				.desc    = "bitmap based space allocator",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.init_allocator       = bitmap_init_allocator,
			.destroy_allocator    = bitmap_destroy_allocator,
			.alloc_blocks         = bitmap_alloc_blocks,
			.dealloc_blocks       = NULL,
			.pre_commit_hook      = bitmap_pre_commit_hook,
			.post_commit_hook     = bitmap_post_commit_hook,
			.post_write_back_hook = NULL,
			.print_info           = NULL
		}
	},
	[ TEST_SPACE_ALLOCATOR_ID ] = {
		.space_allocator = {
			.h = {
				.type_id = REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE,
				.id      = TEST_SPACE_ALLOCATOR_ID,
				.pops    = NULL,
				.label   = "test",
				.desc    = "allocator with no reusing (for debugging only)",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.init_allocator    = test_init_allocator,
			.destroy_allocator = NULL,
			.alloc_blocks      = test_alloc_blocks,
			.dealloc_blocks    = test_dealloc_blocks,
			.pre_commit_hook      = NULL,
			.post_commit_hook     = NULL,
			.post_write_back_hook = NULL,
			.print_info           = test_print_info
		}
	}
};
