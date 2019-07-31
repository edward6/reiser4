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
#include "../volume/volume.h"
#include "dst.h"

static u64 lookup_triv(reiser4_dcx *rdcx, const struct inode *inode,
		       const char *str, int len, u32 seed, void *tab)
{
	return METADATA_SUBVOL_ID;
}

static u64 lookup_custom(reiser4_dcx *rdcx, const struct inode *inode,
			 const char *str, int len, u32 seed, void *tab)
{
	u64 id;
	lv_conf *conf;

	if (reiser4_is_set(reiser4_get_current_sb(), REISER4_FILE_BASED_DIST)) {
		assert("edward-2342", inode != NULL);
		id = atomic_read(&unix_file_inode_data(inode)->custom_brick_id);
	} else
		id = atomic_read(&current_volume()->custom_brick_id);
	/*
	 * check if there is a brick with such ID.
	 */
	conf = rcu_dereference(current_volume()->conf);

	if (id >= conf_nr_mslots(conf) || !conf_mslot_at(conf, id)) {
		warning("edward-2343", "Invalid custom brick ID %llu",
			(unsigned long long)id);
		return METADATA_SUBVOL_ID;
	}
	return id;
}

static void dist_lock_noop(struct inode *inode)
{
	;
}

static void read_dist_lock_generic(struct inode *inode)
{
	if (reiser4_is_set(reiser4_get_current_sb(), REISER4_FILE_BASED_DIST))
		down_read(&unix_file_inode_data(inode)->latch);
	else
		down_read(&current_volume()->dist_sem);
}

static void read_dist_unlock_generic(struct inode *inode)
{
	if (reiser4_is_set(reiser4_get_current_sb(), REISER4_FILE_BASED_DIST))
		up_read(&unix_file_inode_data(inode)->latch);
	else
		up_read(&current_volume()->dist_sem);
}

static void write_dist_lock_generic(struct inode *inode)
{
	if (reiser4_is_set(reiser4_get_current_sb(), REISER4_FILE_BASED_DIST))
		down_write(&unix_file_inode_data(inode)->latch);
	else
		down_write(&current_volume()->dist_sem);
}

static void write_dist_unlock_generic(struct inode *inode)
{
	if (reiser4_is_set(reiser4_get_current_sb(), REISER4_FILE_BASED_DIST))
		up_write(&unix_file_inode_data(inode)->latch);
	else
		up_write(&current_volume()->dist_sem);
}

static void read_dist_lock_vol(struct inode *inode)
{
	down_read(&current_volume()->dist_sem);
}

static void read_dist_unlock_vol(struct inode *inode)
{
	up_read(&current_volume()->dist_sem);
}

static void write_dist_lock_vol(struct inode *inode)
{
	down_write(&current_volume()->dist_sem);
}

static void write_dist_unlock_vol(struct inode *inode)
{
	up_write(&current_volume()->dist_sem);
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
			.read_dist_lock = dist_lock_noop,
			.read_dist_unlock = dist_lock_noop,
			.write_dist_lock = dist_lock_noop,
			.write_dist_unlock = dist_lock_noop
		}
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
			.read_dist_lock = read_dist_lock_vol,
			.read_dist_unlock = read_dist_unlock_vol,
			.write_dist_lock = write_dist_lock_vol,
			.write_dist_unlock = write_dist_unlock_vol
		}
	},
	[CUSTOM_DISTRIB_ID] = {
		.h = {
			.type_id = REISER4_DISTRIBUTION_PLUGIN_TYPE,
			.id = CUSTOM_DISTRIB_ID,
			.pops = NULL,
			.label = "custom",
			.desc = "Custom Distribution",
			.linkage = {NULL, NULL}
		},
		.seg_bits = 2, /* (log(sizeof u32)) */
		.r = {
			.init = NULL,
			.lookup = lookup_custom,
			.replace = NULL,
			.free = NULL,
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
			.read_dist_lock = read_dist_lock_generic,
			.read_dist_unlock = read_dist_unlock_generic,
			.write_dist_lock = write_dist_lock_generic,
			.write_dist_unlock = write_dist_unlock_generic
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
