/*
  Copyright (c) 2014-2016 Eduard O. Shishkin

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
		.lookup_bucket = NULL,
		.add_bucket = NULL,
		.remove_bucket = NULL,
		.split = NULL,
		.pack = NULL,
		.unpack = NULL
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
