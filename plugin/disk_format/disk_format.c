/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/* initialization of disk layout plugins */
reiser4_plugin format_plugins[ LAST_FORMAT_ID ] = {
	[ FORMAT_40_ID ] = {
		.format = {
			.h = {
				.type_id = REISER4_FORMAT_PLUGIN_TYPE,
				.id      = FORMAT_40_ID,
				.pops    = NULL,
				.label   = "reiser40",
				.desc    = "standard disk layout for reiser40",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.get_ready     = format_40_get_ready,
			.root_dir_key  = format_40_root_dir_key,
			.release       = format_40_release,
			.log_super     = format_40_log_super,
			.print_info    = format_40_print_info
		}
	},
	[ TEST_FORMAT_ID ] = {
		.format = {
			.h = {
				.type_id = REISER4_FORMAT_PLUGIN_TYPE,
				.id      = TEST_FORMAT_ID,
				.pops    = NULL,
				.label   = "test",
				.desc    = "layout for debugging",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.get_ready     = test_format_get_ready,
			.root_dir_key  = test_format_root_dir_key,
			.release       = test_format_release,
			.log_super     = NULL,
			.print_info    = test_format_print_info
		}
	}
};
