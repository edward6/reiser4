/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../debug.h"
#include "../plugin_header.h"
#include "disk_format40.h"
#include "test.h"
#include "disk_format.h"
#include "../oid/oid.h"
#include "../plugin.h"

/* initialization of disk layout plugins */
disk_format_plugin format_plugins[ LAST_FORMAT_ID ] = {
	[ FORMAT40_ID ] = {
		.h = {
			.type_id = REISER4_FORMAT_PLUGIN_TYPE,
			.id      = FORMAT40_ID,
			.pops    = NULL,
			.label   = "reiser40",
			.desc    = "standard disk layout for reiser40",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.get_ready     = format40_get_ready,
		.root_dir_key  = format40_root_dir_key,
		.release       = format40_release,
		.log_super     = format40_log_super,
		.print_info    = format40_print_info
	},
	[ TEST_FORMAT_ID ] = {
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
};
