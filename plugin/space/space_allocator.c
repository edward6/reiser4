/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../plugin_header.h"
#include "../plugin.h"
#include "bitmap.h"
#include "test.h"
#include "space_allocator.h"
/* ZAM-FIXME-HANS: implement a compile time selected space allocation plugin with no runtime function dereferences. */
/* initialization of objectid space allocator plugins */
space_allocator_plugin space_plugins[LAST_SPACE_ALLOCATOR_ID] = {
	[BITMAP_SPACE_ALLOCATOR_ID] = {
		.h = {
			.type_id = REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE,
			.id = BITMAP_SPACE_ALLOCATOR_ID,
			.pops = NULL,
			.label = "bitmap",
			.desc = "bitmap based space allocator",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.init_allocator = init_allocator_bitmap,
		.destroy_allocator = destroy_allocator_bitmap,
		.alloc_blocks = alloc_blocks_bitmap,
		.dealloc_blocks = dealloc_blocks_bitmap,
#if REISER4_DEBUG
		.check_blocks = check_blocks_bitmap,
#endif
		.pre_commit_hook = pre_commit_hook_bitmap,
		.post_commit_hook = NULL,
		.post_write_back_hook = NULL,
		.print_info = NULL
	},
	[TEST_SPACE_ALLOCATOR_ID] = {
		.h = {
			.type_id = REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE,
			.id = TEST_SPACE_ALLOCATOR_ID,
			.pops = NULL,
			.label = "test",
			.desc = "allocator with no reusing (for debugging only)",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.init_allocator = init_allocator_test,
		.destroy_allocator = NULL,
		.alloc_blocks = alloc_blocks_test,
		.dealloc_blocks = dealloc_blocks_test,
#if REISER4_DEBUG
		.check_blocks = check_blocks_test,
#endif
		.pre_commit_hook = NULL,
		.post_commit_hook = NULL,
		.post_write_back_hook = NULL,
		.print_info = print_info_test
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/

