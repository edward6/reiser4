/*
  Copyright (c) 2016 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "../../debug.h"
#include "../../super.h"
#include "../../inode.h"
#include "volume.h"

/*
 * Reiser4 Logical Volume (LV) of format 5.X is a matrix of subvolumes.
 * The first column always represents meta-data suvolume and its replicas.
 * Other columns represent data subvolumes.
 *
 * In asymmetric LV all extent pointers are on the meta-data subvolume.
 * In symmetric LV every extent pointer is stored on a respective data
 * subvolume.
 *
 * For asymmetric LV any data search procedure is performed only on the
 * meta-data subvolume. For symmetric LV every data search procedure is
 * launched on a respective data subvolume.
 */

#define VOLMAP_MAGIC "R4VoLMaP"
#define VOLMAP_MAGIC_SIZE (8)

struct voltab_entry {
	reiser4_block_nr block; /* address of the unformatted voltab block */
	u32 csum;  /* checksum of the voltab block */
}PACKED;

struct volmap {
	u32 csum; /* checksum of this volmap block */
	char magic[8];
	reiser4_block_nr next; /* disk address of the next volmap block */
	struct voltab_entry entries [0];
}PACKED;

static reiser4_block_nr get_next_volmap(struct volmap *volmap)
{
	return le64_to_cpu(get_unaligned(&volmap->next));
}

static void set_next_volmap(struct volmap *volmap, reiser4_block_nr cpu_value)
{
	put_unaligned(cpu_to_le64(cpu_value), &volmap->next);
}

static int voltab_nodes_per_block(void)
{
	return (current_blocksize - sizeof (struct volmap)) /
		sizeof(struct voltab_entry);
}

static int segments_per_block(reiser4_volume *vol)
{
	distribution_plugin *dist_plug = vol->dist_plug;

	return 1 << (current_blocksize_bits - dist_plug->seg_bits);
}

static int num_voltab_nodes(reiser4_volume *vol, int nums_bits)
{
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1818",
	       nums_bits + dist_plug->seg_bits >= current_blocksize_bits);

	return 1 << (nums_bits + dist_plug->seg_bits - current_blocksize_bits);
}

static int num_volmap_nodes(reiser4_volume *vol, int nums_bits)
{
	int result;

	result = num_voltab_nodes(vol, nums_bits) / voltab_nodes_per_block();
	if (num_voltab_nodes(vol, nums_bits) % voltab_nodes_per_block())
		result ++;
	return result;
}

static int volinfo_absent(void)
{
	return current_origin(METADATA_SUBVOL_ID)->volmap_loc == 0;
}

static void release_volinfo_nodes(reiser4_volume *vol)
{
	u64 i;
	for (i = 0; i < vol->num_volmaps + vol->num_voltabs; i++)
		if (vol->volmap_nodes[i]) {
			jput(vol->volmap_nodes[i]);
			jdrop(vol->volmap_nodes[i]);
			vol->volmap_nodes[i] = NULL;
		}
	kfree(vol->volmap_nodes);
	vol->volmap_nodes = NULL;
	vol->voltab_nodes = NULL;
}

static void done_volume_asym(reiser4_subvol *subv)
{
	reiser4_volume *vol = super_volume(subv->super);

	if (subv->volmap_loc != 0)
		release_volinfo_nodes(vol);
}

/**
 * Load system volume information to memory
 */
static int load_volume_info(reiser4_subvol *subv)
{
	int ret;
	int i, j;
	u64 segments_loaded = 0;
	u64 voltab_nodes_loaded = 0;
	reiser4_volume *vol = super_volume(subv->super);
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc = subv->volmap_loc;

	if (volmap_loc == 0) {
		/*
		 * It can happen only for simple volumes
		 */
		assert("edward-1847", vol->num_origins == 1);
		return 0;
	}
	vol->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vol->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(*vol->volmap_nodes), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		jnode *volmapj;
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		volmapj = vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, subv);
		if (!volmapj) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(volmapj);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(volmapj);
		/*
		 * load all voltabs pointed by current volmap
		 */
		for (j = 0; j < voltab_nodes_per_block(); j++) {
			jnode *voltabj;
			reiser4_block_nr voltab_loc;

			voltab_loc = volmap->entries[j].block;
			voltabj = vol->voltab_nodes[voltab_nodes_loaded] =
				reiser4_alloc_volinfo_head(&voltab_loc, subv);
			if (!voltabj) {
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jload(voltabj);
			if (ret)
				goto unpin;
			dist_plug->v.unpack(&vol->aid,
					    jdata(voltabj),
					    segments_loaded,
					    segments_per_block(vol));
			jrelse(voltabj);
			segments_loaded += segments_per_block(vol);
			voltab_nodes_loaded ++;
		}
		volmap_loc = get_next_volmap(volmap);
		jrelse(volmapj);
	}
 unpin:
	done_volume_asym(subv);
	return ret;
}

/**
 * pin volinfo nodes;
 * pack in-memory volume system info to voltab nopes.
 * Volinfo nodes don't change their location.
 */
static int update_voltab_nodes(reiser4_volume *vol)
{
	int ret;
	int i, j;
	u64 segments_loaded = 0;
	u64 voltab_nodes_loaded = 0;
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_subvol *mtd_subv = current_origin(METADATA_SUBVOL_ID);
	reiser4_block_nr volmap_loc = mtd_subv->volmap_loc;

	assert("edward-1835", volmap_loc != 0);
	assert("edward-1836", vol->num_volmaps != 0);
	assert("edward-1837", vol->num_voltabs != 0);

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(*vol->volmap_nodes), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		jnode *volmapj;
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		volmapj = vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, mtd_subv);
		if (!volmapj) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(volmapj);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(volmapj);
		/*
		 * upate all voltabs pointed by current volmap
		 */
		for (j = 0; j < voltab_nodes_per_block(); j++) {
			jnode *voltabj;
			reiser4_block_nr voltab_loc;

			voltab_loc = volmap->entries[j].block;
			voltabj = vol->voltab_nodes[voltab_nodes_loaded] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   mtd_subv);
			if (!voltabj) {
				ret = -ENOMEM;
				goto unpin;
			}
			/*
			 * we don't want to read voltab block,
			 * as it will be completely overwritten
			 * with new data
			 */
			ret = jinit_new(voltabj, GFP_KERNEL);
			if (ret)
				return ret;
			ret = jload(voltabj);
			if (ret)
				goto unpin;
			dist_plug->v.pack(&vol->aid,
					  jdata(voltabj),
					  segments_loaded,
					  segments_per_block(vol));
			jrelse(voltabj);
			segments_loaded += segments_per_block(vol);
			voltab_nodes_loaded ++;
		}
		volmap_loc = get_next_volmap(volmap);
		jrelse(volmapj);
	}
	return 0;
 unpin:
	release_volinfo_nodes(vol);
	return ret;
}

static int alloc_volinfo_block(reiser4_block_nr *block, reiser4_subvol *subv)
{
	reiser4_blocknr_hint hint;

	reiser4_blocknr_hint_init(&hint);
	hint.block_stage = BLOCK_NOT_COUNTED;

	return reiser4_alloc_block(&hint, block,
				   BA_FORMATTED | BA_USE_DEFAULT_SEARCH_START,
				   subv);
}

/**
 * Create and pin volinfo nodes, allocate disk addresses for them,
 * and pack in-memory volume system information to those nodes
 */
static int create_volinfo_nodes(reiser4_volume *vol)
{
	int ret;
	int i, j;
	u64 segments_loaded = 0;
	u64 voltab_nodes_loaded = 0;
	reiser4_subvol *meta_subv = current_origin(METADATA_SUBVOL_ID);

	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc;
	/*
	 * allocate disk address of the first volmap block
	 */
	ret = alloc_volinfo_block(&volmap_loc, meta_subv);
	if (ret)
		return ret;
	meta_subv->volmap_loc = volmap_loc;

	vol->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vol->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(*vol->volmap_nodes), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		jnode *volmapj;
		struct volmap *volmap;
		reiser4_block_nr voltab_loc;

		volmapj = vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&voltab_loc, meta_subv);
		if (!volmapj) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jinit_new(volmapj, GFP_KERNEL);
		if (ret)
			goto unpin;
		ret = jload(volmapj);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(volmapj);
		/*
		 * load all voltabs pointed by current volmap
		 */
		for (j = 0; j < voltab_nodes_per_block(); j++) {
			jnode *voltabj;
			reiser4_block_nr voltab_loc;
			/*
			 * allocate disk address for voltab node
			 */
			ret = alloc_volinfo_block(&voltab_loc, meta_subv);
			if (ret)
				goto unpin;
			assert("edward-1838", voltab_loc != 0);

			volmap->entries[j].block = voltab_loc;

			voltabj = vol->voltab_nodes[voltab_nodes_loaded] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   meta_subv);
			if (!voltabj) {
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jinit_new(voltabj, GFP_KERNEL);
			if (ret)
				goto unpin;
			ret = jload(voltabj);
			if (ret)
				goto unpin;
			dist_plug->v.pack(&vol->aid,
					  jdata(voltabj),
					  segments_loaded,
					  segments_per_block(vol));
			jrelse(voltabj);

			segments_loaded += segments_per_block(vol);
			voltab_nodes_loaded ++;
		}
		if (i == vol->num_volmaps - 1)
			/*
			 * current volmap node is the last one
			 */
			set_next_volmap(volmap, 0);
		else {
			/*
			 * allocate disk address of the next volmap block
			 * and store it in the current volmap block
			 */
			ret = alloc_volinfo_block(&volmap_loc, meta_subv);
			if (ret)
				goto unpin;
			set_next_volmap(volmap, volmap_loc);
		}
		jrelse(volmapj);
	}
	return 0;
 unpin:
	release_volinfo_nodes(vol);
	return ret;
}

/*
 * Write array of jnodes in atomic manner
 */
static int write_array_nodes(jnode **start, u64 count)
{
	u64 i;
	int ret;

	for (i = 0; i < count; i++) {
		jnode *node;
		node = start[i];

		spin_lock_jnode(node);
		ret = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(ret != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	reiser4_txn_restart_current();
	return 0;
}

static int write_volinfo_nodes(reiser4_volume *vol)
{
	u64 i;
	int ret;
	/*
	 * Format superblocks of all subvolumes to be overwritten
	 * with updated data room size and location of the first
	 * volmap block. So, put them to the transaction.
	 */
	for (i = 0; i < current_num_origins(); i++) {
		ret = reiser4_capture_super_block(current_origin(i));
		if (ret)
			return ret;
	}
	return write_array_nodes(vol->volmap_nodes,
				 vol->num_volmaps + vol->num_voltabs);
}

static int write_voltab_nodes(reiser4_volume *vol)
{
	return write_array_nodes(vol->voltab_nodes, vol->num_voltabs);
}

static int write_volume_info(reiser4_volume *vol)
{
	int ret;

	if (volinfo_absent()) {
		ret = create_volinfo_nodes(vol);
		if (ret)
			return ret;
		ret = write_volinfo_nodes(vol);
	} else {
		ret = update_voltab_nodes(vol);
		if (ret)
			return ret;
		ret = write_voltab_nodes(vol);
	}
	release_volinfo_nodes(vol);
	return ret;
}

static int load_volume_asym(reiser4_subvol *subv)
{
	if (subv->id != 0)
		/*
		 * Nothing to load:
		 * Asymmetric LV stores system info only
		 * on meta-data subvolume with id = 0
		 */
		return 0;
	return load_volume_info(subv);
}

static u64 default_data_room(reiser4_subvol *subv)
{
	if (subv != current_meta_subvol())
		return subv->block_count;
	else
		/* 70% of block_count */
		return (90 * subv->block_count) >> 7;		
}

/*
 * Init volume system info, which has been already loaded
 * diring disk formats inialization of subvolumes (components).
 */
static int init_volume_asym(reiser4_volume *vol)
{
	distribution_plugin *dist_plug = vol->dist_plug;

	if (dist_plug->r.init)
		return dist_plug->r.init(&vol->aid, vol->num_sgs_bits);
	return 0;
}

static u64 cap_at_asym(void *buckets, u64 index)
{
	return current_aid_subvols()[index][0]->data_room;
}

static void *fib_of_asym(void *bucket)
{
	struct reiser4_subvol *subv = bucket;

	return subv->fiber;
}

static void *fib_at_asym(void *buckets, u64 index)
{
	return current_aid_subvols()[index][0]->fiber;
}

static void fib_set_at_asym(void *buckets, u64 index, void *fiber)
{
	current_aid_subvols()[index][0]->fiber = fiber;
}

static u64 *fib_lenp_at_asym(void *buckets, u64 index)
{
	return &current_aid_subvols()[index][0]->fiber_len;
}

static int expand_subvol(reiser4_volume *vol, u64 pos, u64 delta)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;

	current_aid_subvols()[pos][0]->data_room += delta;

	if (num_aid_subvols(vol) == 1)
		return 0;

	ret = dist_plug->v.expand(&vol->aid, pos, 0);
	if (ret)
		/* roll back */
		current_aid_subvols()[pos][0]->data_room -= delta;
	return ret;
}

/*
 * Insert a meta-data subvolume to AID
 */
static int add_meta_subvol(reiser4_volume *vol, reiser4_subvol *new)
{
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1820", new == current_meta_subvol());

	if (meta_subvol_is_in_aid())
		/*
		 * Can not add a subvolume, which is already
		 * present in the array
		 */
		return -EINVAL;

	assert("edward-1822", meta_subvol_is_in_aid());

	if (vol->num_origins == 1)
		/*
		 *  nothing to do any more
		 */
		return 0;
	return dist_plug->v.expand(&vol->aid, METADATA_SUBVOL_ID, 1);
}

/*
 * Insert a data subvolume to AID at position @pos
 */
static int add_data_subvol(reiser4_volume *vol, u64 pos,
			   reiser4_subvol *new_subv)
{
	int ret;
	reiser4_aid *raid = &vol->aid;
	distribution_plugin *dist_plug = vol->dist_plug;

	struct reiser4_subvol ***new;
	struct reiser4_subvol ***old = vol->subvols;
	u64 old_num_subvols = vol->num_origins;
	u64 pos_in_vol;

	if (num_aid_subvols(vol) == 0) {
		warning("edward-1840",
			"Can not add data subvolume to empty AID");
		return -EINVAL;
	}
	pos_in_vol = pos;
	if (!meta_subvol_is_in_aid())
		pos_in_vol ++;

	new = kmalloc(sizeof(reiser4_subvol) * (1 + old_num_subvols),
		      reiser4_ctx_gfp_mask_get());
	if (!new)
		return -ENOMEM;

	memcpy(new, old, sizeof(*new) * pos_in_vol);
	new[pos_in_vol][0] = new_subv;
	memcpy(new + pos_in_vol + 1, old + pos_in_vol,
	       sizeof(*new) * (old_num_subvols - pos_in_vol));

	vol->subvols = new;
	vol->num_origins ++;

	ret = dist_plug->v.expand(raid, pos, 1);
	if (ret) {
		vol->subvols = old;
		vol->num_origins --;
		kfree(new);
		return ret;
	}
	kfree(old);
	return 0;
}

/*
 * Insert a new subvolume to AID at position @pos
 */
static int add_subvol(reiser4_volume *vol, u64 pos, u64 cap, reiser4_subvol *new)
{
	assert("edward-1843", new->data_room == 0);

	if (cap != 0)
		new->data_room = cap;
	else
		new->data_room = default_data_room(new);

	if (new->data_room == 0)
		return -EINVAL;

	if (new == current_meta_subvol())
		return add_meta_subvol(vol, new);
	else
		return add_data_subvol(vol, pos, new);
}

/*
 * Check position in LV, and convert it to the position in AID
 */
static int check_convert_pos_for_expand(reiser4_volume *vol,
					u64 *pos, reiser4_subvol *new)
{
	if (new) {
		/*
		 * insert a new subvolume
		 */
		if (*pos == 0 && meta_subvol_is_in_aid())
			return -EINVAL;
		if (*pos == 0 && new != current_meta_subvol())
			return -EINVAL;
		if (*pos > 0 && new == current_meta_subvol())
			return -EINVAL;
		if (*pos > vol->num_origins)
			return -EINVAL;
	} else {
		/*
		 * expand existing subvolume
		 */
		if (*pos == 0 && !meta_subvol_is_in_aid())
			return -EINVAL;
		if (*pos >= vol->num_origins)
			return -EINVAL;
	}
	/*
	 * Convert @pos to the position in AID
	 */
	if (meta_subvol_is_in_aid())
		/*
		 * position in volume coincides with position in AID,
		 * nothing to convert
		 */
		return 0;
	/*
	 * meda-data subvolume is not in AID
	 */
	if (pos == 0)
		/*
		 * meta-data subvolume is to be inserted, or expanded,
		 * so that position in AID is also 0 */
		return 0;
	/*
	 * pos > 0
	 * data subvolume is to be inserted to AID, whish
	 * doesn't contain meta-data subvolume
	 */
	(*pos) --;

	return 0;
}

static int can_expand(reiser4_volume *vol, reiser4_subvol *new)
{
	if (vol->dist_plug->h.id == TRIV_DISTRIB_ID ||
	    get_meta_subvol()->df_plug->h.id == FORMAT40_ID ||
	    (new && (get_meta_subvol()->df_plug->h.id != new->df_plug->h.id)))
		return 0;
	return 1;
}

/**
 * Increase capacity of asymmetric LV.
 */
static int expand_asym(reiser4_volume *vol, u64 pos, u64 delta,
		       reiser4_subvol *new)
{
	int ret;
	reiser4_aid *raid = &vol->aid;
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1824", raid != NULL);
	assert("edward-1825", dist_plug != NULL);

	if (!can_expand(vol, new))
		return -EINVAL;
	ret = check_convert_pos_for_expand(vol, &pos, new);
	if (ret)
		return RETERR(ret);

	ret = dist_plug->v.init(vol,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->aid_ops, raid);
	if (ret)
		return RETERR(ret);
	if (new)
		ret = add_subvol(vol, pos, delta, new);
	else
		ret = expand_subvol(vol, pos, delta);
	if (ret)
		goto error;
	/*
	 * Wake up balancing thread.
	 * After balancing is finished it will call dist_plug->v.done()
	 */
	ret = write_volume_info(vol);
	if (ret)
		goto error;
	return 0;
 error:
	dist_plug->v.done(raid);
	return ret;
}

static int shrink_subvol(reiser4_volume *vol, u64 pos, u64 delta)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;

	current_aid_subvols()[pos][0]->data_room -= delta;

	if (num_aid_subvols(vol) == 1)
		return 0;

	ret = dist_plug->v.shrink(&vol->aid, pos, NULL);
	if (ret)
		current_aid_subvols()[pos][0]->data_room += delta;
	return ret;
}

/*
 * Remove meta-data subvolume from AID
 */
static int remove_meta_subvol(reiser4_volume *vol)
{
	int ret;
	reiser4_subvol *mtd_subv = current_meta_subvol();
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1844", num_aid_subvols(vol) != 0);
	assert("edward-1826", meta_subvol_is_in_aid());

	if (num_aid_subvols(vol) == 1)
		goto finish;

	ret = dist_plug->v.shrink(&vol->aid, METADATA_SUBVOL_ID, mtd_subv);
	if (ret)
		return ret;
 finish:
	mtd_subv->data_room = 0;

	assert("edward-1827", !meta_subvol_is_in_aid());
	return 0;
}

/*
 * Remove a data subvolume from AID at position @pos
 */
static int remove_data_subvol(reiser4_volume *vol, u64 pos)
{
	int ret;
	reiser4_subvol *victim;
	distribution_plugin *dist_plug = vol->dist_plug;

	struct reiser4_subvol ***new;
	struct reiser4_subvol ***old = vol->subvols;
	u64 old_num_subvols = vol->num_origins;
	u64 pos_in_vol;

	assert("edward-1842", num_aid_subvols(vol) != 0);

	if (num_aid_subvols(vol) == 1) {
		warning("edward-1841",
			"Can not remove single data subvolume");
		return -EINVAL;
	}
	pos_in_vol = pos;
	if (!meta_subvol_is_in_aid())
		pos_in_vol ++;

	new = kmalloc(sizeof(reiser4_subvol) * (old_num_subvols - 1),
		      reiser4_ctx_gfp_mask_get());
	if (!new)
		return RETERR(-ENOMEM);

	memcpy(new, old, pos_in_vol * sizeof(*new));
	victim = old[pos_in_vol][0];
	memcpy(new + pos_in_vol, old + pos_in_vol + 1,
	       sizeof(*new) * (old_num_subvols - pos_in_vol - 1));

	vol->subvols = new;
	vol->num_origins --;

	ret = dist_plug->v.shrink(&vol->aid, pos, victim);
	if (ret) {
		vol->subvols = old;
		vol->num_origins ++;
		kfree(new);
		return ret;
	}
	deactivate_subvol(reiser4_get_current_sb(), victim);
	kfree(old);

	return 0;
}

static int remove_subvol(reiser4_volume *vol, u64 pos)
{
	if (pos == 0 && meta_subvol_is_in_aid())
		return remove_meta_subvol(vol);
	else
		return remove_data_subvol(vol, pos);
}

/*
 * Check and convert position @pos in volume to the position in AID
 */
static int check_convert_pos_for_shrink(reiser4_volume *vol, u64 *pos)
{
	if (*pos == 0 && !meta_subvol_is_in_aid())
		return -EINVAL;

	if (*pos >= vol->num_origins)
		return -EINVAL;

	if (!meta_subvol_is_in_aid())
		(*pos) --;
	return 0;
}

/**
 * Decrease capacity of asymmetric LV
 */
static int shrink_asym(reiser4_volume *vol, u64 pos, u64 delta, int remove)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1830", vol != NULL);
	assert("edward-1846", dist_plug != NULL);
	assert("edward-1831", vol->subvols != NULL);
	assert("edward-1832", pos < num_aid_subvols(vol));
	assert("edward-1833", vol->subvols[pos] != NULL);

	if (dist_plug->h.id == TRIV_DISTRIB_ID)
		return -EINVAL;

	ret = check_convert_pos_for_shrink(vol, &pos);
	if (ret)
		return ret;

	ret = dist_plug->v.init(vol,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->aid_ops, &vol->aid);
	if (ret)
		return ret;
	if (remove)
		ret = remove_subvol(vol, pos);
	else
		ret = shrink_subvol(vol, pos, delta);
	if (ret)
		goto error;
	/*
	 * Wake up balancing thread.
	 * After balancing is finished it will call dist_plug->v.done()
	 */
	ret = write_volume_info(vol);
	if (ret)
		goto error;
	return 0;
 error:
	dist_plug->v.done(&vol->aid);
	return ret;
}

static u64 sys_subvol_id_simple(void)
{
	return METADATA_SUBVOL_ID;
}

static u64 meta_subvol_id_simple(void)
{
	return METADATA_SUBVOL_ID;
}

static u64 data_subvol_id_simple(oid_t oid, loff_t offset)
{
	return METADATA_SUBVOL_ID;
}

static u64 data_subvol_id_by_key_simple(reiser4_key *key)
{
	return METADATA_SUBVOL_ID;
}

static int shrink_simple(reiser4_volume *vol, u64 pos, u64 delta, int remove)
{
	return -EINVAL;
}

static int expand_simple(reiser4_volume *vol, u64 pos, u64 delta,
		       reiser4_subvol *new)
{
	return -EINVAL;
}

static int build_body_key_simple(struct inode *inode,
				 loff_t offset, reiser4_key *key)
{
	/*
	 * For simple volumes body key's ordering is
	 * inherited from inode
	 */
	return key_by_inode_offset_ordering(inode,
					    offset,
					    get_inode_ordering(inode),
					    key);
}

static u64 data_subvol_id_asym(oid_t oid, loff_t offset)
{
	reiser4_volume *vol;
	distribution_plugin *dist_plug;
	u64 stripe_idx;

	vol = current_volume();
	dist_plug = current_dist_plug();
	stripe_idx = offset >> current_stripe_bits;

	return dist_plug->r.lookup(&vol->aid,
				   (const char *)&stripe_idx,
				   sizeof(stripe_idx), (u32)oid);
}

static int build_body_key_asym(struct inode *inode,
			       loff_t offset, reiser4_key *key)
{
	oid_t oid;

	oid = get_inode_oid(inode);
	/*
	 * For compound volumes body key's ordering is a subvolume ID
	 */
	return key_by_inode_offset_ordering(inode,
					    offset,
					    data_subvol_id_asym(oid, offset),
					    key);
}

static u64 data_subvol_id_by_key_asym(reiser4_key *key)
{
	return get_key_ordering(key);
}

reiser4_subvol *get_meta_subvol(void)
{
	return current_origin(current_vol_plug()->meta_subvol_id());
}

reiser4_subvol *get_data_subvol(const struct inode *inode, loff_t offset)
{
	return current_origin(current_vol_plug()->data_subvol_id(get_inode_oid(inode), offset));
}


reiser4_subvol *subvol_by_extent(const coord_t *coord)
{
	reiser4_key key;
	volume_plugin *vol_plug = current_vol_plug();

	item_key_by_coord(coord, &key);
	return current_origin(vol_plug->data_subvol_id_by_key(&key));
}

reiser4_subvol *subvol_by_coord(const coord_t *coord)
{
	if (item_is_extent(coord))
		return subvol_by_extent(coord);
	else if (item_is_internal(coord))
		return get_meta_subvol();
	else
		return NULL;
}

/**
 * Check if @coord is an extent item.
 * If yes, then store its key in @arg.
 */
static int iter_check_extent(reiser4_tree *tree, coord_t *coord,
			lock_handle *lh, void *arg)
{
	assert("edward-1877", arg != NULL);

	if (!item_is_extent(coord)) {
		assert("edward-1878", item_is_internal(coord));
		return 1;
	}
	item_key_by_coord(coord, (reiser4_key *)arg);
	return 0;
}

struct reiser4_iterate_context {
	reiser4_key *curr;
	reiser4_key *next;
};

/**
 * Check if @coord is an extent item of the next file.
 * If yes, then store its key.
 */
static int iter_check_extent_next_file(reiser4_tree *tree, coord_t *coord,
				       lock_handle *lh, void *arg)
{
	struct reiser4_iterate_context *iter_ctx = arg;

	assert("edward-1879", iter_ctx != NULL);

	if (!item_is_extent(coord)) {
		assert("edward-1880", item_is_internal(coord));
		return 1;
	}
	item_key_by_coord(coord, iter_ctx->next);

	if (get_key_objectid(iter_ctx->next) ==
	    get_key_objectid(iter_ctx->curr))
		return 1;
	return 0;
}

/**
 * Online balancing of asymmetric logical volume.
 * This procedure is to complete any volume operations above.
 * If it returns 0, then the volume is fully balanced. Otherwise, the
 * procedure should be repeated in some context.
 *
 * NOTE: correctness of this balancing procedure is guaranteed by our
 * single stupid objectid allocator. If you want to add another one,
 * then please prove the correctness, or write another balancing which
 * works for that new allocator.
 *
 * @super: super-block of the volume that we want to balance
 *
 * FIXME: use hint/seal to not traverse tree every time
 */
int balance_volume_asym(struct super_block *super)
{
	int ret;
	int err = 0;
	coord_t coord;
	lock_handle lh;

	reiser4_key k1, k2;
	reiser4_key *curr = &k1; /* key of current extent */
	reiser4_key *next = &k2; /* key of next file extent */

	assert("edward-1881", super != NULL);

 restart:
	init_lh(&lh);
	/*
	 * Prepare start position:
	 * find leftmost item of the leftmost node on the twig level.
	 * Such item always exists, even in the case of empty volume
	 */
	ret = coord_by_key(meta_subvol_tree(), reiser4_min_key(),
			   &coord, &lh, ZNODE_READ_LOCK,
			   FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL,
			   0, NULL /* FIXME: set proper read-ahead info */);

	if (ret != CBK_COORD_NOTFOUND) {
		err = 1;
		goto exit;
	}
	assert("edward-1882", coord.item_pos == 0);
	assert("edward-1883", coord.unit_pos == 0);
	assert("edward-1884", coord.between == BEFORE_UNIT);
	/*
	 * find leftmost extent on the twig level,
	 * and store its key in @curr
	 */
	coord.between = AT_UNIT;
	ret = reiser4_iterate_tree(meta_subvol_tree(), &coord, &lh,
				   iter_check_extent, curr, ZNODE_READ_LOCK, 0);
	done_lh(&lh);
	if (ret < 0) {
		if (ret != -E_NO_NEIGHBOR)
			err = 1;
		/*
		 * leftmost extent not found,
		 * so nothing to do any more
		 */
		goto exit;
	}
	/*
	 * leftmost extent found
	 */
	assert("edward-1885", get_key_offset(curr) == 0);

	while(1) {
		int last_iter = 0;
		reiser4_key *temp;
		struct inode *inode;
		struct reiser4_iterate_context iter_ctx;

		ret = coord_by_key(meta_subvol_tree(), curr,
				   &coord, &lh, ZNODE_READ_LOCK,
				   FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL,
				   0, NULL /* FIXME: set proper
					      read-ahead info */);
		if (ret == CBK_COORD_NOTFOUND) {
			/*
			 * Payment for the "onliness". We have lost scan
			 * position, because someone deleted extent that
			 * we found in the previous iteraton.
			 * If the file still exists, then it doesn't have
			 * a body, so its system table will be updated by
			 * insert_first_extent().
			 */
			done_lh(&lh);
			notice("edward-1886",
			       "Online balancing restarted on %s", super->s_id);
			goto restart;
		}
		else if (ret != CBK_COORD_FOUND) {
			done_lh(&lh);
			err = 1;
			goto exit;
		}
		/*
		 * find leftmost extent of the next file,
		 * store its key in @next
		 */
		assert("edward-1887", coord_is_existing_item(&coord));

		iter_ctx.curr = curr;
		iter_ctx.next = next;

		ret = reiser4_iterate_tree(meta_subvol_tree(), &coord,
					   &lh, iter_check_extent_next_file,
					   &iter_ctx, ZNODE_READ_LOCK, 0);
		done_lh(&lh);

		if (ret == -E_NO_NEIGHBOR)
			/*
			 * next extent not found
			 */
			last_iter = 1;
		else if (ret < 0) {
			err = 1;
			goto exit;
		}
		else
			assert("edward-1888", get_key_offset(next) == 0);
		/*
		 * Construct stat-data key from @curr and get the inode.
		 * reiser4_iget() wants @curr to be a precise stat-data key.
		 * However, we don't know ordering of that stat-data, so we
		 * set maximal possible value.
		 */
		set_key_ordering(curr, KEY_ORDERING_MASK);
		set_key_offset(curr, 0);

		inode = reiser4_iget(super, curr, 0);

		if (!IS_ERR(inode) && inode_file_plugin(inode)->balance) {
			reiser4_iget_complete(inode);
			/*
			 * Relocate data of this file.
			 */
			ret = inode_file_plugin(inode)->balance(inode);
			if (ret) {
				err = 1;
				warning("edward-1889",
				      "Inode %lli: data migration failed (%d)",
				      (unsigned long long)get_inode_oid(inode),
				      ret);
			}
			iput(inode);
		}
		if (last_iter)
			break;

		temp = curr;
		curr = next;
		next = temp;
	}
 exit:
	/*
	 * if (!err)
	 *         clear REBALANCING_IN_PROGRESS flag in super-block
	 */
	return err;
}

volume_plugin volume_plugins[LAST_VOLUME_ID] = {
	[SIMPLE_VOLUME_ID] = {
		.h = {
			.type_id = REISER4_VOLUME_PLUGIN_TYPE,
			.id = SIMPLE_VOLUME_ID,
			.pops = NULL,
			.label = "simple",
			.desc = "Simple Logical Volume",
			.linkage = {NULL, NULL}
		},
		.sys_subvol_id = sys_subvol_id_simple,
		.meta_subvol_id = meta_subvol_id_simple,
		.data_subvol_id = data_subvol_id_simple,
		.data_subvol_id_by_key = data_subvol_id_by_key_simple,
		.build_body_key = build_body_key_simple,
		.load = NULL,
		.done = NULL,
		.init = NULL,
		.expand = expand_simple,
		.shrink = shrink_simple,
		.aid_ops = {
			.cap_at = NULL,
			.fib_of = NULL,
			.fib_at = NULL,
			.fib_set_at = NULL,
			.fib_lenp_at = NULL
		}
	},
	[ASYM_VOLUME_ID] = {
		.h = {
			.type_id = REISER4_VOLUME_PLUGIN_TYPE,
			.id = ASYM_VOLUME_ID,
			.pops = NULL,
			.label = "asym",
			.desc = "Asymmetric Heterogeneous Logical Volume",
			.linkage = {NULL, NULL}
		},
		.sys_subvol_id = sys_subvol_id_simple,
		.meta_subvol_id = meta_subvol_id_simple,
		.data_subvol_id = data_subvol_id_asym,
		.data_subvol_id_by_key = data_subvol_id_by_key_asym,
		.build_body_key = build_body_key_asym,
		.load = load_volume_asym,
		.done = done_volume_asym,
		.init = init_volume_asym,
		.expand = expand_asym,
		.shrink = shrink_asym,
		.aid_ops = {
			.cap_at = cap_at_asym,
			.fib_of = fib_of_asym,
			.fib_at = fib_at_asym,
			.fib_set_at = fib_set_at_asym,
			.fib_lenp_at = fib_lenp_at_asym
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
