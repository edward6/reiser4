/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/* initialization of disk layout plugins */
reiser4_plugin layout_plugins[ LAST_LAYOUT_ID ] = {
	[ LAYOUT_40_ID ] = {
		.layout = {
			.h = {
				.type_id = REISER4_LAYOUT_PLUGIN_TYPE,
				.id      = LAYOUT_40_ID,
				.pops    = NULL,
				.label   = "reiser40",
				.desc    = "standard disk layout for reiser40",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.get_ready     = format_40_get_ready,
			.root_dir_key  = format_40_root_dir_key,
			.release       = format_40_release,
			.print_info    = NULL
		}
	},
	[ TEST_LAYOUT_ID ] = {
		.layout = {
			.h = {
				.type_id = REISER4_LAYOUT_PLUGIN_TYPE,
				.id      = TEST_LAYOUT_ID,
				.pops    = NULL,
				.label   = "test",
				.desc    = "layout for debugging",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.get_ready     = test_layout_get_ready,
			.root_dir_key  = test_layout_root_dir_key,
			.release       = test_layout_release,
			.print_info    = test_layout_print_info
		}
	}
};
