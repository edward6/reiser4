/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/* initialization of disk layout plugins */
reiser4_plugin layout_plugins[ LAST_LAYOUT_ID ] = {
	[ LAYOUT_40_ID ] = {
		.h = {
			.type_id = REISER4_LAYOUT_PLUGIN_TYPE,
			.id      = LAYOUT_40_ID,
			.pops    = NULL,
			.label   = "reiser40",
			.desc    = "standard disk layout for reiser40",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.u = {
			.layout = {
				.get_ready     = layout_40_get_ready,
				.root_dir_key  = layout_40_root_dir_key
			}
		}
	}
};
