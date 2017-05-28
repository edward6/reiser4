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
		.seg_bits = 0,
		.r = {
			.init = NULL,
			.lookup = NULL,
			.done = NULL,
		},
		.v = {
			.init = NULL,
			.done = NULL,
			.expand = NULL,
			.shrink = NULL,
			.split = NULL,
			.pack = NULL,
			.unpack = NULL
		},
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
		.seg_bits = 2, /* (log(sizeof u32)) */
		.r = {
			.init = initr_fsw32,
			.lookup = lookup_fsw32m,
			.done = doner_fsw32
		},
		.v = {
			.init = initv_fsw32,
			.done = donev_fsw32,
			.expand = expand_fsw32,
			.shrink = shrink_fsw32,
			.split = split_fsw32,
			.pack = pack_fsw32,
			.unpack = unpack_fsw32
		}
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
