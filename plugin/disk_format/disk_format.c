/* Copyright 2002 Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../plugin_header.h"
#include "disk_format40.h"
#include "test.h"
#include "disk_format.h"
#include "../plugin.h"

/* initialization of disk layout plugins */
disk_format_plugin format_plugins[LAST_FORMAT_ID] = {
	[FORMAT40_ID] = {
		.h = {
			.type_id = REISER4_FORMAT_PLUGIN_TYPE,
			.id = FORMAT40_ID,
			.pops = NULL,
			.label = "reiser40",
			.desc = "standard disk layout for reiser40",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.get_ready = get_ready_format40,
		.root_dir_key = root_dir_key_format40,
		.release = release_format40,
		.log_super = log_super_format40,
		.print_info = print_info_format40
	},
	[TEST_FORMAT_ID] = {
		.h = {
			.type_id = REISER4_FORMAT_PLUGIN_TYPE,
			.id = TEST_FORMAT_ID,
			.pops = NULL,
			.label = "test",
			.desc = "layout for debugging",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.get_ready = get_ready_test_format,
		.root_dir_key = root_dir_key_test_format,
		.release = release_test_format,
		.log_super = NULL,
		.print_info = print_info_test_format
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
