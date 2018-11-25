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

/**
 * Implementations of simple and asymmetric Logical Volumes (LV).
 * Asymmetric Logical Volume is a matrix of subvolumes.
 * Its first column always represents meta-data suvolume and its replicas.
 * Other columns represent data subvolumes.
 *
 * In asymmetric LV all extent pointers are on the meta-data subvolume.
 * In symmetric LV every extent pointer is stored on a respective data
 * subvolume.
 *
 * For asymmetric LV any search-by-key procedure is performed only on the
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

static u32 volmap_get_csum(struct volmap *vmap)
{
	return le32_to_cpu(get_unaligned(&vmap->csum));
}

static void volmap_set_csum(struct volmap *vmap, u32 val)
{
	put_unaligned(cpu_to_le32(val), &vmap->csum);
}

static reiser4_block_nr volmap_get_entry_blk(struct volmap *vmap, int nr)
{
	return le64_to_cpu(get_unaligned(&vmap->entries[nr].block));
}

static void volmap_set_entry_blk(struct volmap *vmap, int nr, u64 val)
{
	put_unaligned(cpu_to_le64(val), &vmap->entries[nr].block);
}

static u32 volmap_get_entry_csum(struct volmap *vmap, int nr)
{
	return le32_to_cpu(get_unaligned(&vmap->entries[nr].csum));
}

static void volmap_set_entry_csum(struct volmap *vmap, int nr, u32 val)
{
	put_unaligned(cpu_to_le32(val), &vmap->entries[nr].csum);
}

static reiser4_block_nr get_next_volmap_addr(struct volmap *vmap)
{
	return le64_to_cpu(get_unaligned(&vmap->next));
}

static void set_next_volmap_addr(struct volmap *vmap, reiser4_block_nr val)
{
	put_unaligned(cpu_to_le64(val), &vmap->next);
}

static int balance_volume_asym(struct super_block *sb);

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

/**
 * find a meta-data brick of not yet activated volume
 */
reiser4_subvol *find_meta_brick(reiser4_volume *vol)
{
	struct reiser4_subvol *subv;

	list_for_each_entry(subv, &vol->subvols_list, list)
		if (is_meta_brick(subv))
			return subv;
	return NULL;
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
	return get_meta_subvol()->volmap_loc == 0;
}

static void release_volinfo_nodes(reiser4_volume *vol)
{
	u64 i;

	if (vol->volmap_nodes == NULL)
		return;

	for (i = 0; i < vol->num_volmaps + vol->num_voltabs; i++)
		if (vol->volmap_nodes[i]) {
			reiser4_drop_volinfo_head(vol->volmap_nodes[i]);
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
	u32 num_aid_bricks;
	u64 packed_segments = 0;
	reiser4_volume *vol = super_volume(subv->super);
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc = subv->volmap_loc;
	u64 voltabs_needed;

	assert("edward-1984", subv->id == METADATA_SUBVOL_ID);

	if (subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM))
		num_aid_bricks = vol_nr_origins(vol);
	else {
		assert("edward-1985", vol_nr_origins(vol) > 1);
		num_aid_bricks = vol_nr_origins(vol) - 1;
	}
	if (dist_plug->r.init) {
		ret = dist_plug->r.init(&vol->aid,
					num_aid_bricks,
					vol->num_sgs_bits);
		if (ret)
			return ret;
	}
	if (volmap_loc == 0) {
		assert("edward-1847", vol_nr_origins(vol) == 1);
		return 0;
	}
	vol->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vol->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);
	voltabs_needed = vol->num_voltabs;

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(*vol->volmap_nodes), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, subv);
		if (!vol->volmap_nodes[i]) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(vol->volmap_nodes[i]);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(vol->volmap_nodes[i]);
		/*
		 * load all voltabs pointed by current volmap
		 */
		for (j = 0;
		     j < voltab_nodes_per_block() && voltabs_needed;
		     j++, voltabs_needed --) {

			reiser4_block_nr voltab_loc;

			voltab_loc = volmap_get_entry_blk(volmap, j);
			assert("edward-1986", voltab_loc != 0);

			vol->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   subv);
			if (!vol->voltab_nodes[j]) {
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jload(vol->voltab_nodes[j]);
			if (ret)
				goto unpin;
			dist_plug->v.unpack(&vol->aid,
					    jdata(vol->voltab_nodes[j]),
					    packed_segments,
					    segments_per_block(vol));
			jrelse(vol->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		volmap_loc = get_next_volmap_addr(volmap);
		jrelse(vol->volmap_nodes[i]);
	}
 unpin:
	release_volinfo_nodes(vol);
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
	u64 packed_segments = 0;
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_subvol *mtd_subv = get_meta_subvol();
	reiser4_block_nr volmap_loc = mtd_subv->volmap_loc;
	u64 voltabs_needed;

	assert("edward-1835", volmap_loc != 0);
	assert("edward-1836", vol->num_volmaps != 0);
	assert("edward-1837", vol->num_voltabs != 0);

	voltabs_needed = vol->num_voltabs;

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(*vol->volmap_nodes), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, mtd_subv);
		if (!vol->volmap_nodes[i]) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(vol->volmap_nodes[i]);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(vol->volmap_nodes[i]);
		/*
		 * upate all voltabs pointed by current volmap
		 */
		for (j = 0;
		     j < voltab_nodes_per_block() && voltabs_needed;
		     j++, voltabs_needed --) {

			reiser4_block_nr voltab_loc;

			voltab_loc = volmap_get_entry_blk(volmap, j);
			assert("edward-1987", voltab_loc != 0);

			vol->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   mtd_subv);
			if (!vol->voltab_nodes[j]) {
				ret = -ENOMEM;
				goto unpin;
			}
			/*
			 * we don't read voltab block fom disk,
			 * as it will be completely overwritten
			 * with new data
			 */
			ret = jinit_new(vol->voltab_nodes[j], GFP_KERNEL);
			if (ret)
				goto unpin;

			dist_plug->v.pack(&vol->aid,
					  jdata(vol->voltab_nodes[j]),
					  packed_segments,
					  segments_per_block(vol));
			jrelse(vol->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		volmap_loc = get_next_volmap_addr(volmap);
		jrelse(vol->volmap_nodes[i]);
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
				   BA_FORMATTED | BA_PERMANENT |
				   BA_USE_DEFAULT_SEARCH_START, subv);
}

/**
 * Create and pin volinfo nodes, allocate disk addresses for them,
 * and pack in-memory volume system information to those nodes
 */
noinline int create_volinfo_nodes(reiser4_volume *vol)
{
	int ret;
	int i, j;
	u64 packed_segments = 0;
	reiser4_subvol *meta_subv = get_meta_subvol();

	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc;
	u64 voltabs_needed;

	ret = reiser4_create_atom();
	if (ret)
		return ret;
	/*
	 * allocate disk address of the first volmap block
	 */
	ret = alloc_volinfo_block(&volmap_loc, meta_subv);
	if (ret)
		return ret;
	meta_subv->volmap_loc = volmap_loc;

	vol->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vol->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);
	voltabs_needed = vol->num_voltabs;

	vol->volmap_nodes =
		kzalloc((vol->num_volmaps + vol->num_voltabs) *
			sizeof(void *), GFP_KERNEL);

	if (!vol->volmap_nodes)
		return -ENOMEM;

	vol->voltab_nodes = vol->volmap_nodes + vol->num_volmaps;

	for (i = 0; i < vol->num_volmaps; i++) {
		struct volmap *volmap;

		vol->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, meta_subv);
		if (!vol->volmap_nodes[i]) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jinit_new(vol->volmap_nodes[i], GFP_KERNEL);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(vol->volmap_nodes[i]);
		/*
		 * load all voltabs pointed by current volmap
		 */
		for (j = 0;
		     j < voltab_nodes_per_block() && voltabs_needed;
		     j++, voltabs_needed --) {

			reiser4_block_nr voltab_loc;
			/*
			 * allocate disk address for voltab node
			 */
			ret = alloc_volinfo_block(&voltab_loc, meta_subv);
			if (ret)
				goto unpin;
			assert("edward-1838", voltab_loc != 0);

			volmap_set_entry_blk(volmap, j, voltab_loc);

			vol->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   meta_subv);
			if (!vol->voltab_nodes[j]) {
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jinit_new(vol->voltab_nodes[j],
					GFP_KERNEL);
			if (ret)
				goto unpin;
			dist_plug->v.pack(&vol->aid,
					  jdata(vol->voltab_nodes[j]),
					  packed_segments,
					  segments_per_block(vol));
			jrelse(vol->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		if (i == vol->num_volmaps - 1)
			/*
			 * current volmap node is the last one
			 */
			set_next_volmap_addr(volmap, 0);
		else {
			/*
			 * allocate disk address of the next volmap block
			 * and store it in the current volmap block
			 */
			ret = alloc_volinfo_block(&volmap_loc, meta_subv);
			if (ret)
				goto unpin;
			set_next_volmap_addr(volmap, volmap_loc);
		}
		/*
		 * update volmap csum
		 */
		jrelse(vol->volmap_nodes[i]);
	}
	return 0;
 unpin:
	release_volinfo_nodes(vol);
	return ret;
}

/*
 * Capture an array of jnodes and make them dirty
 * if @new is true, then jnode is recently allocated
 */
static int capture_array_nodes(jnode **start, u64 count, int new)
{
	u64 i;
	int ret;

	for (i = 0; i < count; i++) {
		jnode *node;
		node = start[i];
		set_page_dirty_notag(jnode_page(node));

		spin_lock_jnode(node);
		if (new)
			jnode_set_reloc(node);
		ret = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(ret != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	return 0;
}

static int capture_volinfo_nodes(reiser4_volume *vol, int new)
{
	int ret;
	/*
	 * Capture format superblock of meta-data brick with
	 * updated location of the first volmap block.
	 */
	ret = capture_brick_super(current_origin(METADATA_SUBVOL_ID));
	if (ret)
		return ret;
	return capture_array_nodes(vol->volmap_nodes,
				   vol->num_volmaps + vol->num_voltabs, new);
}

static int capture_voltab_nodes(reiser4_volume *vol, int new)
{
	return capture_array_nodes(vol->voltab_nodes, vol->num_voltabs, new);
}

/**
 * Put created or modified volume info into transaction
 * and commit the last one.
 */
static int capture_volume_info(reiser4_volume *vol)
{
	int ret;
	txn_atom *atom;
	txn_handle *th;

	if (volinfo_absent()) {
		ret = create_volinfo_nodes(vol);
		if (ret)
			return ret;
		ret = capture_volinfo_nodes(vol, 1);
	} else {
		ret = update_voltab_nodes(vol);
		if (ret)
			return ret;
		ret = capture_voltab_nodes(vol, 0);
	}
	if (ret)
		return ret;
	/*
	 * write volinfo to disk
	 */
	th = get_current_context()->trans;
	atom = get_current_atom_locked();
	assert("edward-1988", atom != NULL);
	spin_lock_txnh(th);
	ret = force_commit_atom(th);

	release_volinfo_nodes(vol);
	return ret;
}

static int load_volume_asym(reiser4_subvol *subv)
{
	if (subv->id != METADATA_SUBVOL_ID)
		/*
		 * System configuration of assymetric LV
		 * is stored only in meta-data subvolume
		 */
		return 0;
	return load_volume_info(subv);
}

/*
 * Init volume system info, which has been already loaded
 * diring disk formats inialization of subvolumes (components).
 */
static int init_volume_asym(reiser4_volume *vol)
{
	if (!REISER4_PLANB_KEY_ALLOCATION) {
		warning("edward-2161",
			"Asymmetric LV requires Plan-B key allocation scheme");
		return RETERR(-EINVAL);
	}
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
	assert("edward-2150", current_aid_subvols() != NULL);
	assert("edward-2151", current_aid_subvols()[index] != NULL);
	assert("edward-2152", current_aid_subvols()[index][0] != NULL);

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

static u64 blocks_free_at(void *buckets, u64 index)
{
	return current_aid_subvols()[index][0]->blocks_free;
}

static u64 data_blocks_occ_at(void *buckets, u64 idx)
{
	reiser4_subvol *subv;

	assert("edward-2072", buckets == current_aid_subvols());

	subv = ((reiser4_subvol ***)buckets)[idx][0];

	if (is_meta_brick(subv)) {
		/*
		 * In asymmetric LV we don't keep a track of busy
		 * data blocks on the meta-data brick. However,
		 * we can calculate it by the portion of busy data
		 * blocks on the neighboring data brick, it the last
		 * one exists (if it doesn't exist, then LV is composed
		 * of only one brick and there is no need to know
		 * number of busy data blocks).
		 */
		u64 m;
		u64 dr;
		u64 dr_on_neighbor;
		u64 dr_occ_on_neighbor;

		assert("edward-2069", current_nr_origins() > 1);

		dr = cap_at_asym(buckets, idx);
		dr_on_neighbor = cap_at_asym(buckets, idx + 1);
		dr_occ_on_neighbor = data_blocks_occ_at(buckets, idx + 1);
		m = dr * dr_occ_on_neighbor;
		return div64_u64(m, dr_on_neighbor);
	} else
		/*
		 * data brick
		 */
		return cap_at_asym(buckets, idx) - blocks_free_at(buckets, idx);
}

/**
 * Calculate and return brick position in the AID.
 * @pos_in_vol: internal ID of the brick in the logical volume.
 */
static u64 get_pos_in_aid(u64 pos_in_vol)
{
	if (meta_brick_belongs_aid())
		return pos_in_vol;

	assert("edward-1928", pos_in_vol > 0);
	return pos_in_vol - 1;
}

static int expand_brick(reiser4_volume *vol, reiser4_subvol *this,
			u64 delta, int *need_balance)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	/*
	 * FIXME-EDWARD: Check that resulted capacity is not too large
	 */
	this->data_room += delta;

	if (num_aid_subvols(vol) == 1) {
		*need_balance = 0;
		return 0;
	}
	ret = dist_plug->v.inc(&vol->aid, get_pos_in_aid(this->id), 0);
	if (ret)
		/* roll back */
		this->data_room -= delta;
	return ret;
}

static int add_meta_brick(reiser4_volume *vol, reiser4_subvol *new)
{
	assert("edward-1820", is_meta_brick(new));
	/*
	 * We don't need to activate meta-data brick:
	 * it is always active in the mount session of the logical volume.
	 */
	new->flags |= (1 << SUBVOL_HAS_DATA_ROOM);

	return vol->dist_plug->v.inc(&vol->aid, 0, 1);
}

int add_data_brick(reiser4_volume *vol, reiser4_subvol *this)
{
	int ret = -ENOMEM;
	reiser4_aid *raid = &vol->aid;
	struct reiser4_subvol ***new;
	struct reiser4_subvol ***old = vol->subvols;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u64 pos_in_aid;

	assert("edward-1929", !is_meta_brick(this));
	/*
	 * Insert a new data brick at the very end.
	 */
	pos_in_vol = old_num_subvols;
	pos_in_aid = get_pos_in_aid(pos_in_vol);
	/*
	 * Set in-memory internal ID of the new volume member
	 */
	this->id = pos_in_vol;

	new = alloc_mirror_slots(1 + old_num_subvols);
	if (!new)
		return -ENOMEM;

	memcpy(new, old, sizeof(*new) * pos_in_vol);
	new[pos_in_vol] = alloc_mirror_slot(1);
	if (!new[pos_in_vol]) {
		free_mirror_slots(new);
		return -ENOMEM;
	}
	new[pos_in_vol][0] = this;
	memcpy(new + pos_in_vol + 1, old + pos_in_vol,
	       sizeof(*new) * (old_num_subvols - pos_in_vol));

	vol->subvols = new;
	atomic_inc(&vol->nr_origins);

	ret = vol->dist_plug->v.inc(raid, pos_in_aid, 1);
	if (ret) {
		/* roll back */
		vol->subvols = old;
		atomic_dec(&vol->nr_origins);
		free_mirror_slot(new[pos_in_vol]);
		free_mirror_slots(new);
		return ret;
	}
	free_mirror_slots(old);
	return 0;
}

static int add_brick(reiser4_volume *vol, reiser4_subvol *this)
{
	assert("edward-1959", vol != NULL);
	assert("edward-1960", this != NULL);
	assert("edward-1961", this->data_room != 0);

	if (is_meta_brick(this))
		return add_meta_brick(vol, this);
	else
		return add_data_brick(vol, this);
}

/**
 * Increase capacity of a specified brick
 * @id: internal ID of the brick
 */
static int expand_brick_asym(reiser4_volume *vol, reiser4_subvol *this,
			     u64 delta)
{
	int ret;
	int need_balance = 1;
	reiser4_aid *raid = &vol->aid;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();

	assert("edward-1824", raid != NULL);
	assert("edward-1825", dist_plug != NULL);

	ret = dist_plug->v.init(vol,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->aid_ops, raid);
	if (ret)
		return ret;
	ret = expand_brick(vol, this, delta, &need_balance);
	if (ret)
		goto out;
	if (need_balance) {
		ret = capture_volume_info(vol);
		if (ret)
			goto out;
		reiser4_volume_set_unbalanced(sb);
		ret = capture_brick_super(get_meta_subvol());
	}
 out:
	dist_plug->v.done(raid);
	return ret;
}

/**
 * Add a @new brick to asymmetric logical volume @vol
 */
static int add_brick_asym(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;
	reiser4_aid *raid = &vol->aid;
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-1931", dist_plug != NULL);

	if (new->data_room == 0) {
		warning("edward-1962", "Can't add brick of zero capacity");
		return -EINVAL;
	}
	if (brick_belongs_aid(new)) {
		warning("edward-1963", "Can't add brick to AID twice");
		return -EINVAL;
	}
	ret = dist_plug->v.init(vol,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->aid_ops, raid);
	if (ret)
		return ret;

	ret = add_brick(vol, new);
	if (ret) {
		dist_plug->v.done(raid);
		return ret;
	}
	/*
	 * if volinfo doesn't exist, then it will be created at this step
	 */
	ret = capture_volume_info(vol);
	dist_plug->v.done(raid);
	if (ret)
		return ret;
	/*
	 * set unbalanced status and propagate it to format super-block
	 */
	reiser4_volume_set_unbalanced(reiser4_get_current_sb());
	return capture_brick_super(get_meta_subvol());
}

static u64 get_busy_data_blocks_asym(void)
{
	u64 i;
	u64 ret = 0;
	reiser4_volume *vol = current_volume();

	txnmgr_force_commit_all(reiser4_get_current_sb(), 0);

	for (i = 0; i < num_aid_subvols(vol); i++)
		ret += data_blocks_occ_at(current_aid_subvols(), i);
	return ret;
}

static int shrink_brick(reiser4_volume *vol, reiser4_subvol *victim,
			u64 delta, int *need_balance)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	u64 nr_busy_data_blocks = get_busy_data_blocks_asym();
	/*
	 * FIXME-EDWARD: Check that resulted capacity is not too small
	 */
	victim->data_room -= delta;

	if (num_aid_subvols(vol) == 1) {
		*need_balance = 0;
		return 0;
	}
	ret = dist_plug->v.cfs(&vol->aid,
			       num_aid_subvols(vol),
			       current_aid_subvols(),
			       nr_busy_data_blocks);
	if (ret)
		goto error;
	ret = dist_plug->v.dec(&vol->aid,
			       get_pos_in_aid(victim->id), NULL);
	if (ret)
		goto error;
	return 0;
 error:
	victim->data_room += delta;
	return ret;
}

static int remove_meta_brick(reiser4_volume *vol)
{
	int ret;
	reiser4_subvol *mtd_subv = get_meta_subvol();
	distribution_plugin *dist_plug = vol->dist_plug;
	u64 nr_busy_data_blocks = get_busy_data_blocks_asym();

	assert("edward-1844", num_aid_subvols(vol) > 1);
	assert("edward-1826", meta_brick_belongs_aid());

	ret = dist_plug->v.cfs(&vol->aid,
			       num_aid_subvols(vol) - 1,
			       &current_aid_subvols()[1],
			       nr_busy_data_blocks);
	if (ret)
		return ret;
	ret = dist_plug->v.dec(&vol->aid, 0, mtd_subv);
	if (ret)
		return ret;

	clear_bit(SUBVOL_HAS_DATA_ROOM, &mtd_subv->flags);

	assert("edward-1827", !meta_brick_belongs_aid());
	return 0;
}

static int remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim,
			     void **newp)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	u64 nr_busy_data_blocks = get_busy_data_blocks_asym();

	struct reiser4_subvol ***new;
	struct reiser4_subvol ***old = vol->subvols;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u64 pos_in_aid;

	assert("edward-1842", num_aid_subvols(vol) > 1);

	if (victim->id != old_num_subvols - 1) {
		/*
		 * Currently we add/remove data bricks in
		 * stackable manner - temporal restriction
		 * added for simplicity
		 */
		warning("edward-2162", "Can't remove brick %llu: not LIFO",
			victim->id);
		return RETERR(-EINVAL);
	}
	pos_in_vol = victim->id;
	pos_in_aid = get_pos_in_aid(pos_in_vol);
	new = alloc_mirror_slots(old_num_subvols - 1);
	if (!new)
		return RETERR(-ENOMEM);

	memcpy(new, old, pos_in_vol * sizeof(*old));
	memcpy(new + pos_in_vol, old + pos_in_vol + 1,
	       sizeof(*new) * (old_num_subvols - pos_in_vol - 1));

	ret = dist_plug->v.cfs(&vol->aid, vol_nr_origins(vol),
			       new, nr_busy_data_blocks);
	if (ret)
		goto error;
	ret = dist_plug->v.dec(&vol->aid, pos_in_aid, victim);
	if (ret) {
		warning("edward-2146",
			"Failed to update distribution config (%d)", ret);
		goto error;
	}
	*newp = new;
	return 0;
 error:
	free_mirror_slots(new);
	return ret;
}

/*
 * Remove a brick from AID
 */
static int remove_brick(reiser4_volume *vol, reiser4_subvol *victim,
			void **newp)
{
	if (num_aid_subvols(vol) == 1) {
		warning("edward-1941",
			"Can't remove the single brick from AID");
		return -EINVAL;
	}
	if (is_meta_brick(victim))
		return remove_meta_brick(vol);
	else
		return remove_data_brick(vol, victim, newp);
}

static int remove_or_shrink_brick(reiser4_volume *vol, reiser4_subvol *victim,
				  u64 delta)
{
	int ret;
	void *new_slots = NULL;
	int need_balance = 1;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();

	assert("edward-1830", vol != NULL);
	assert("edward-1846", dist_plug != NULL);
	assert("edward-1944", dist_plug->h.id != TRIV_DISTRIB_ID);
	/*
	 * make sure there is enough space on the rest
	 * of LV to perform remove or shrink operation.
	 * FIXME: this estimation can be not enough in
	 * the case of intensive clients IO activity
	 * diring rebalancing.
	 */
	if (delta > victim->data_room) {
		warning("edward-2153",
			"Can't shrink brick by %llu (its capacity is %llu)",
			delta, victim->data_room);
		return -EINVAL;
	}
	ret = dist_plug->v.init(vol,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->aid_ops, &vol->aid);
	if (ret)
		return ret;

	if (delta && delta < victim->data_room)
		ret = shrink_brick(vol, victim, delta, &need_balance);
	else
		ret = remove_brick(vol, victim, &new_slots);
	if (ret)
		goto out;
	if (need_balance) {
		time_t start;

		ret = capture_volume_info(vol);
		if (ret)
			goto out;
		/*
		 * set unbalanced status and propagate it to format
		 * super-block
		 */
		reiser4_volume_set_unbalanced(sb);
		ret = capture_brick_super(get_meta_subvol());
		if (ret)
			goto out;
		printk("reiser4 (%s): Brick %s has been removed. Started balancing...\n",
		       sb->s_id, victim->name);
		start = get_seconds();
		ret = balance_volume_asym(sb);
		if (ret) {
			warning("edward-2139",
				"%s: Balancing aborted (%d)", sb->s_id, ret);
			goto out;
		}
		printk("reiser4 (%s): Balancing completed in %lu seconds.\n",
		       sb->s_id, get_seconds() - start);
	}
	if (new_slots) {
		reiser4_subvol ***old_slots;
		/*
		 * Replace the matrix of bricks with a new one,
		 * which doesn't contain @victim.
		 * Before this, it is absolutely necessarily to
		 * commit everything to make sure that there is
		 * no pending IOs addressed to the @victim we are
		 * about to remove.
		 */
		txnmgr_force_commit_all(sb, 1);

		old_slots = vol->subvols;
		vol->subvols = new_slots;
		atomic_dec(&vol->nr_origins);

		free_mirror_slot(old_slots[vol_nr_origins(vol)]);
		free_mirror_slots(old_slots);
	}
 out:
	dist_plug->v.done(&vol->aid);
	return ret;
}

static int remove_brick_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	return remove_or_shrink_brick(vol, victim, 0);
}

static int shrink_brick_asym(reiser4_volume *vol, reiser4_subvol *victim,
			     u64 delta)
{
	return remove_or_shrink_brick(vol, victim, delta);
}

static u64 meta_subvol_id_simple(void)
{
	return METADATA_SUBVOL_ID;
}

static u64 data_subvol_id_calc_simple(oid_t oid, loff_t offset, void *tab)
{
	return METADATA_SUBVOL_ID;
}

static int shrink_brick_simple(reiser4_volume *vol, reiser4_subvol *this,
			       u64 delta)
{
	warning("", "shrink operation is undefined for simple volumes");
	return -EINVAL;
}

static int remove_brick_simple(reiser4_volume *vol, reiser4_subvol *this)
{
	warning("", "remove_brick operation is undefined for simple volumes");
	return -EINVAL;
}

static int expand_brick_simple(reiser4_volume *vol, reiser4_subvol *this,
			       u64 delta)
{
	warning("", "expand operation is undefined for simple volumes");
	return -EINVAL;
}

static int add_brick_simple(reiser4_volume *vol, reiser4_subvol *new)
{
	warning("", "add_brick operation is undefined for simple volumes");
	return -EINVAL;
}

static int balance_volume_simple(struct super_block *sb)
{
	warning("", "balance operation is undefined for simple volumes");
	return -EINVAL;
}

static inline u32 get_seed(oid_t oid, reiser4_volume *vol)
{
	u32 seed;

	put_unaligned(cpu_to_le64(oid), &oid);

	seed = murmur3_x86_32((const char *)&oid, sizeof(oid), ~0);
	seed = murmur3_x86_32(vol->uuid, 16, seed);
	return seed;
}

static u64 data_subvol_id_calc_asym(oid_t oid, loff_t offset, void *tab)
{
	reiser4_volume *vol;
	distribution_plugin *dist_plug;
	u64 stripe_idx;

	vol = current_volume();
	dist_plug = current_dist_plug();

	if (vol->stripe_bits) {
		stripe_idx = offset >> vol->stripe_bits;
		put_unaligned(cpu_to_le64(stripe_idx), &stripe_idx);
	} else
		stripe_idx = 0;

	return dist_plug->r.lookup(&vol->aid,
				   (const char *)&stripe_idx,
				   sizeof(stripe_idx), get_seed(oid, vol), tab);
}

u64 get_meta_subvol_id(void)
{
	return current_vol_plug()->meta_subvol_id();
}

reiser4_subvol *get_meta_subvol(void)
{
	return current_origin(get_meta_subvol_id());
}

reiser4_subvol *super_meta_subvol(struct super_block *super)
{
	return super_origin(super, super_vol_plug(super)->meta_subvol_id());
}

u64 data_subvol_id_find_simple(const coord_t *coord)
{
	return METADATA_SUBVOL_ID;
}

u64 data_subvol_id_find_asym(const coord_t *coord)
{
	assert("edward-1957", coord != NULL);

	switch(item_id_by_coord(coord)) {
	case NODE_POINTER_ID:
	case EXTENT40_POINTER_ID:
		return METADATA_SUBVOL_ID;
	case EXTENT41_POINTER_ID:
		return find_data_subvol_extent(coord);
	default:
		impossible("edward-2018", "Bad item ID");
		return METADATA_SUBVOL_ID;
	}
}

struct reiser4_iterate_context {
	reiser4_key curr;
	reiser4_key next;
};

/**
 * Check if @coord is an extent item.
 * If yes, then store its key as "current" in the context
 * and return 0 to terminate iteration
 */
static int iter_find_start(reiser4_tree *tree, coord_t *coord,
			   lock_handle *lh, void *arg)
{
	int ret;
	struct reiser4_iterate_context *ictx = arg;

	assert("edward-2121", ictx != NULL);

	ret = zload(coord->node);
	if (ret)
		return ret;

	if (!item_is_extent(coord)) {
		assert("edward-1878", item_is_internal(coord));
		/* continue iteration */
		zrelse(coord->node);
		return 1;
	}
	item_key_by_coord(coord, &ictx->curr);
	zrelse(coord->node);
	return 0;
}

/**
 * Check if @coord is an extent item of file, which doesn't
 * own "current" key in the iteration context. If so, then
 * store its key as "next" in the context and return 0 to
 * terminate iteration.
 */
static int iter_find_next(reiser4_tree *tree, coord_t *coord,
			  lock_handle *lh, void *arg)
{
	int ret;
	struct reiser4_iterate_context *ictx = arg;

	assert("edward-1879", ictx != NULL);

	ret = zload(coord->node);
	if (ret)
		return ret;
	if (!item_is_extent(coord)) {
		assert("edward-1880", item_is_internal(coord));
		/* continue iteration */
		zrelse(coord->node);
		return 1;
	}
	item_key_by_coord(coord, &ictx->next);
	zrelse(coord->node);

	if (get_key_objectid(&ictx->next) ==
	    get_key_objectid(&ictx->curr))
		/*
		 * found chunk of body of same file,
		 * continue iteration
		 */
		return 1;
	return 0;
}

/**
 * Walk from left to right along the twig level of the storage tree
 * and for every found regular file (inode) relocate its data blocks.
 *
 * Stat-data (on-disk inodes) are located on leaf level, nevertheless
 * we scan twig level, recovering stat-data from extent items. Simply
 * because scanning twig level is ~1000 times faster.
 *
 * When scanning twig level we obviously miss empty files (i.e. files
 * without bodies), so every time when performing ->write(), etc.
 * regular operations we need to check volume status. If balancing is
 * in proggress, then we update inode's distribution table before
 * build_body_key() calculation.
 *
 * NOTE: correctness of this balancing procedure (i.e. a guarantee
 * that all files will be processed) is provided by our single stupid
 * objectid allocator. If you want to add another one, then please
 * prove the correctness, or write another balancing which works for
 * that new allocator correctly.
 *
 * @sb: super-block of the volume to be balanced;
 * @force: force to run balancing on volume marked as balanced.
 *
 * FIXME: use hint/seal to not traverse tree every time when locking
 * position determned by the hint ("current" key in the iterate context).
 */
int balance_volume_asym(struct super_block *super)
{
	int ret;
	coord_t coord;
	lock_handle lh;
	reiser4_key start_key;
	struct reiser4_iterate_context ictx;
	reiser4_volume *vol = super_volume(super);
	/*
	 * Set a start key (key of the leftmost object on the
	 * TWIG level) to scan from.
	 * FIXME: This is a hack. Implement find_start_key() to
	 * find the leftmost object on the TWIG level instead.
	 */
	reiser4_key_init(&start_key);
	set_key_locality(&start_key, 41 /* FORMAT40_ROOT_LOCALITY */);
	set_key_type(&start_key, KEY_SD_MINOR);
	set_key_objectid(&start_key, 42 /* FORMAT40_ROOT_OBJECTID */);

	memset(&ictx, 0, sizeof(ictx));

	assert("edward-1881", super != NULL);

	if (!reiser4_volume_is_unbalanced(super))
		return 0;

	init_lh(&lh);
	/*
	 * Prepare start position: find leftmost item on the twig level.
	 * For meta-data brick of format40 such item always exists, even
	 * in the case of empty volume
	 */
	ret = coord_by_key(meta_subvol_tree(), &start_key,
			   &coord, &lh, ZNODE_READ_LOCK,
			   FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL,
			   CBK_UNIQUE, NULL /* read-ahead info */);
	if (IS_CBKERR(ret)) {
		warning("edward-2154", "cbk error when balancing (%d)", ret);
		done_lh(&lh);
		goto error;
	}
	assert("edward-2160", coord.node->level == TWIG_LEVEL);

	coord.item_pos = 0;
	coord.unit_pos = 0;
	coord.between = AT_UNIT;
	/*
	 * find leftmost extent on the twig level
	 */
	ret = reiser4_iterate_tree(meta_subvol_tree(), &coord, &lh,
				   iter_find_start, &ictx, ZNODE_READ_LOCK, 0);
	done_lh(&lh);
	if (ret < 0) {
		if (ret == -E_NO_NEIGHBOR)
			/* volume doesn't contain data blocks */
			goto done;
		goto error;
	}
	while (1) {
		int terminate = 0;
		reiser4_key found;
		reiser4_key sdkey;
		struct inode *inode;
		/*
		 * look for an object found in previous iteration
		 */
		ret = coord_by_key(meta_subvol_tree(), &ictx.curr,
				   &coord, &lh, ZNODE_READ_LOCK,
				   FIND_EXACT,
				   TWIG_LEVEL, TWIG_LEVEL,
				   CBK_UNIQUE, NULL /* read-ahead info */);
		if (IS_CBKERR(ret)) {
			done_lh(&lh);
			warning("edward-1886",
				"cbk error when balancing (%d)", ret);
			goto error;
		}
		ret = zload(coord.node);
		if (ret) {
			done_lh(&lh);
			goto error;
		}
		item_key_by_coord(&coord, &found);
		zrelse(coord.node);

		if (!keyeq(&found, &ictx.curr)) {
			/*
			 * object found at previous iteration is absent
			 * (truncated by concurrent process), thus current
			 * position is an item with key <= @ictx.curr,
			 * that is, we found an object, which was already
			 * processed, so we just need to find next extent,
			 * reset &ictx.curr and proceed
			 */
			ret = reiser4_iterate_tree(meta_subvol_tree(),
						   &coord, &lh,
						   iter_find_start, &ictx,
						   ZNODE_READ_LOCK, 0);
			if (ret < 0) {
				done_lh(&lh);
				if (ret == -E_NO_NEIGHBOR)
					break;
				goto error;
			}
		}
		/*
		 * find leftmost extent of the next file and store
		 * its key as "next" in the iteration context as a
		 * hint for next iteration
		 */
		assert("edward-1887",
		       WITH_DATA(coord.node, coord_is_existing_item(&coord)));

		ret = reiser4_iterate_tree(meta_subvol_tree(), &coord,
					   &lh, iter_find_next,
					   &ictx, ZNODE_READ_LOCK, 0);
		done_lh(&lh);

		if (ret == -E_NO_NEIGHBOR)
			/*
			 * next extent not found
			 */
			terminate = 1;
		else if (ret < 0)
			goto error;
		/*
		 * construct stat-data key from the "current" key of iteration
		 * context and read the inode.
		 * We don't know actual ordering component of stat-data key,
		 * so we set a maximal one to make sure that search procedure
		 * will find it correctly
		 */
		sdkey = ictx.curr;
		set_key_ordering(&sdkey, KEY_ORDERING_MASK /* max ordering */);
		set_key_type(&sdkey, KEY_SD_MINOR);
		set_key_offset(&sdkey, 0);

		inode = reiser4_iget(super, &sdkey, FIND_MAX_NOT_MORE_THAN, 0);

		if (!IS_ERR(inode) && inode_file_plugin(inode)->balance) {
			reiser4_iget_complete(inode);
			/*
			 * Relocate data blocks of this file
			 */
			ret = inode_file_plugin(inode)->balance(inode);
			iput(inode);
			if (ret) {
				warning("edward-1889",
				      "Inode %lli: data migration failed (%d)",
				      (unsigned long long)get_inode_oid(inode),
				      ret);
				/* FSCK? */
				goto error;
			}
		}
		if (terminate)
			break;
		ictx.curr = ictx.next;
	}
 done:
	/* update system table */
	vol->dist_plug->r.update(&vol->aid);
	return 0;
 error:
	warning("edward-2155", "Failed to balance volume %s", super->s_id);
	return ret;
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
		.meta_subvol_id = meta_subvol_id_simple,
		.data_subvol_id_calc = data_subvol_id_calc_simple,
		.data_subvol_id_find = data_subvol_id_find_simple,
		.load_volume = NULL,
		.done_volume = NULL,
		.init_volume = NULL,
		.expand_brick = expand_brick_simple,
		.add_brick = add_brick_simple,
		.shrink_brick = shrink_brick_simple,
		.remove_brick = remove_brick_simple,
		.balance_volume = balance_volume_simple,
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
		.meta_subvol_id = meta_subvol_id_simple,
		.data_subvol_id_calc = data_subvol_id_calc_asym,
		.data_subvol_id_find = data_subvol_id_find_asym,
		.load_volume = load_volume_asym,
		.done_volume = done_volume_asym,
		.init_volume = init_volume_asym,
		.expand_brick = expand_brick_asym,
		.add_brick = add_brick_asym,
		.shrink_brick = shrink_brick_asym,
		.remove_brick = remove_brick_asym,
		.balance_volume = balance_volume_asym,
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
