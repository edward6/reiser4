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
#include "dst.h"

static u64 lookup_triv(reiser4_dcx *rdcx, const char *str,
		       int len, u32 seed, void *tab)
{
	return 0;
}

distribution_plugin distribution_plugins[LAST_DISTRIB_ID] = {
	[TRIV_DISTRIB_ID] = {
		.h = {
			.type_id = REISER4_DISTRIBUTION_PLUGIN_TYPE,
			.id = TRIV_DISTRIB_ID,
			.pops = NULL,
			.label = "triv",
			.desc = "Trivial Distribution",
			.linkage = {NULL, NULL}
		},
		.seg_bits = 0,
		.r = {
			.init = NULL,
			.lookup = lookup_triv,
			.done = NULL,
		},
		.v = {
			.init = NULL,
			.done = NULL,
			.inc = NULL,
			.dec = NULL,
			.spl = NULL,
			.pack = NULL,
			.unpack = NULL,
			.dump = NULL,
		},
		.f = {
			.fix = NULL,
		},
	},
	[FSX32M_DISTRIB_ID] = {
		.h = {
			.type_id = REISER4_DISTRIBUTION_PLUGIN_TYPE,
			.id = FSX32M_DISTRIB_ID,
			.pops = NULL,
			.label = "fsx32m",
			.desc = "Fiber-Striping over 32-bit Murmur hash",
			.linkage = {NULL, NULL}
		},
		.seg_bits = 2, /* (log(sizeof u32)) */
		.r = {
			.init = initr_fsx32,
			.lookup = lookup_fsx32m,
			.replace = replace_fsx32,
			.free = free_fsx32,
			.done = doner_fsx32
		},
		.v = {
			.init = initv_fsx32,
			.done = donev_fsx32,
			.inc = inc_fsx32,
			.dec = dec_fsx32,
			.spl = spl_fsx32,
			.pack = pack_fsx32,
			.unpack = unpack_fsx32,
			.dump = dump_fsx32,
		},
		.f = {
			.fix = fix_data_reservation,
		},
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
