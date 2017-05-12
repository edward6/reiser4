/*
  Copyright (c) 2014-2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <asm/types.h>
#include "../../debug.h"
#include "../../inode.h"
#include "../plugin.h"
#include "aid.h"

distribution_plugin distribution_plugins[LAST_DISTRIB_ID] = {
	[NONE_DISTRIB_ID] = {
		.h = {
			.type_id = REISER4_DISTRIBUTION_PLUGIN_TYPE,
			.id = NONE_DISTRIB_ID,
			.pops = NULL,
			.label = "none",
			.desc = "None Distribution",
			.linkage = {NULL, NULL}
		},
		.seg_size = 0,
		.init = NULL,
		.done = NULL,
		.lookup_bucket = NULL,
		.add_bucket = NULL,
		.remove_bucket = NULL,
		.split = NULL,
		.pack = NULL,
		.unpack = NULL
	},
	[FSW32M_DISTRIB_ID] = {
		.h = {
			.type_id = REISER4_DISTRIBUTION_PLUGIN_TYPE,
			.id = FSW32M_DISTRIB_ID,
			.pops = NULL,
			.label = "fsw32m",
			.desc = "Fiber-Striping over 32-bit Murmur hash",
			.linkage = {NULL, NULL}
		},
		.seg_size = sizeof(u32),
		.init = fsw32_init,
		.done = fsw32_done,
		.lookup_bucket = fsw32m_lookup_bucket,
		.add_bucket = fsw32_add_bucket,
		.remove_bucket = fsw32_remove_bucket,
		.split = fsw32_split,
		.pack = tab32_pack,
		.unpack = tab32_unpack
	}
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
