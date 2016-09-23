/*
  Copyright (c) 2016 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "../../debug.h"
#include "../../super.h"
#include "../plugin.h"

/*
 * This file describes format of Reiser5 Asymmetric Logical Volumes
 * (see the comment about Reiser5 volumes at the beginning of init_volume.c)
 *
 * We define 2 volume groups:
 *
 * . meta-data volumes
 * . data volumes
 *
 * Some subvolumes can be of "mixed type" (we'll also use to say that a
 * meta-data subvolume contains a room for data).
 *
 * Every subvolume of an asymmetric logical volume has an "internal id", a
 * unique serial number from the set [0, N-1], wherte N is number of all
 * subvolumes in the asymmetric volume. The set of subvolumes is implemented
 * as an array of the following format (in ascending internal IDs):
 *
 * . meta-data subvolumes;
 * . subvolumes of mixed type (meta-data subvolumes with rooms for data);
 * . data subvolumes.
 *
 * Every asymmetric volume has at least one meta-data subvolume, which can be
 * of mixed type (i.e. with a room for data). So the first subvolume in the
 * array is always a meta-data subvolume.
 *
 * Every subvolume of asymmetric logical volume has the following important
 * parameters:
 *
 * . total number of subvolumes;
 * . number of meta-data subvolumes w/o room for data;
 * . number of subvolumes of mixed type (w/ room for data).
 */

static u64 subvol_cap(void *data, u64 index)
{
	struct reiser4_volume *vol = data;

	return vol->subvols[index][0]->dev_cap;
}

static void *subvol_fib(void *data, u64 index)
{
	struct reiser4_volume *vol = data;

	return vol->subvols[index][0]->fiber;
}

static void subvol_fib_set(void *data, u64 index, void *fiber)
{
	struct reiser4_volume *vol = data;

	vol->subvols[index][0]->fiber = fiber;
}

static u64 *subvol_fib_lenp(void *data, u64 index)
{
	struct reiser4_volume *vol = data;

	return &vol->subvols[index][0]->fiber_len;
}

static int subvol_add(void *data, void *new)
{
#if 0
	struct reiser4_volume *vol = data;
	struct reiser4_subvol **new_subvols;

	new_subvols = kmalloc(sizeof(reiser4_subvol) *
			      (vol->num_subvols + 1),
			      reiser4_ctx_gfp_mask_get());
	if (!new_subvols)
		return -ENOMEM;

	memcpy(new_subvols, vol->subvols,
	       sizeof(*new_subvols) * vol->num_subvols);
	new_subvols[vol->num_subvols] = new;

	kfree(vol->subvols);
	vol->subvols = new_subvols;
	vol->num_subvols ++;
#endif
	return 0;
}

/**
 * @index: serial number (internal ID) of subvolume to be deleted
 */
static int subvol_del(void *data, u64 index)
{
#if 0
	struct reiser4_volume *vol = data;
	struct reiser4_subvol **new_subvols;

	assert("edward-1783", index < vol->num_subvols);

	new_subvols = kmalloc(sizeof(reiser4_subvol) *
			      (vol->num_subvols - 1),
			      reiser4_ctx_gfp_mask_get());

	memcpy(new_subvols, vol->subvols, index * sizeof(*new_subvols));
	memcpy(new_subvols + index * sizeof(*new_subvols),
	       vol->subvols + (index + 1),
	       sizeof(*new_subvols)*(vol->num_subvols - index - 1));
	kfree(vol->subvols);
	vol->subvols = new_subvols;
	vol->num_subvols --;
#endif
	return 0;
}

static u32 sys_subvol_id_triv(void)
{
	return 0;
}

static u32 meta_subvol_id_triv(void)
{
	return 0;
}

static u32 data_subvol_id_triv(void)
{
	return 0;
}

volume_plugin volume_plugins[LAST_VOLUME_ID] = {
	[TRIV_VOLUME_ID] = {
		.h = {
			.type_id = REISER4_VOLUME_PLUGIN_TYPE,
			.id = TRIV_VOLUME_ID,
			.pops = NULL,
			.label = "triv",
			.desc = "Trivial Logical Volume",
			.linkage = {NULL, NULL}
		},
		.sys_subvol_id = sys_subvol_id_triv,
		.meta_subvol_id = meta_subvol_id_triv,
		.data_subvol_id = data_subvol_id_triv,
		.aib_ops = {
			.bucket_cap = NULL,
			.bucket_add = NULL,
			.bucket_del = NULL,
			.bucket_fib = NULL,
			.bucket_fib_set = NULL,
			.bucket_fib_lenp = NULL
		}
	},
	[ASYM_VOLUME_ID] = {
		.h = {
			.type_id = REISER4_VOLUME_PLUGIN_TYPE,
			.id = ASYM_VOLUME_ID,
			.pops = NULL,
			.label = "asym",
			.desc = "Asymmetric Logical Volume",
			.linkage = {NULL, NULL}
		},
		.aib_ops = {
			.bucket_cap = subvol_cap,
			.bucket_add = subvol_add,
			.bucket_del = subvol_del,
			.bucket_fib = subvol_fib,
			.bucket_fib_set = subvol_fib_set,
			.bucket_fib_lenp = subvol_fib_lenp
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
