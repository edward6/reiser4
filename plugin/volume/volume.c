/*
  Copyright (c) 2016-2018 Eduard O. Shishkin

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
reiser4_subvol *find_meta_brick_by_id(reiser4_volume *vol)
{
	struct reiser4_subvol *subv;

	list_for_each_entry(subv, &vol->subvols_list, list)
		if (is_meta_brick_id(subv->id))
			return subv;
	return NULL;
}

/**
 * Construct an array of abstract buckets by logical volume @vol.
 * The notion of bucket encapsulates an original brick (without
 * replicas). That array should include only DSA members.
 */
static bucket_t *create_buckets(reiser4_volume *vol, u32 nr_buckets)
{
	bucket_t *ret;
	u32 i, j, off;
#if REISER4_DEBUG
	u32 nr_dsa_bricks = num_aid_subvols(vol);

	assert("edward-2180",
	       nr_buckets == nr_dsa_bricks ||
	       nr_buckets == nr_dsa_bricks + 1);
#endif
	off = meta_brick_belongs_aid() ? 0 : 1;

	ret = kzalloc(nr_buckets * sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return NULL;

	for (i = 0, j = 0; i < vol->nr_slots - off; i++) {
		if (vol->subvols[i + off] == NULL)
			continue;
		ret[j] = vol->subvols[i + off][0];
		/*
		 * set index in DSA
		 */
		vol->subvols[i + off][0]->dsa_idx = j;
		j++;
	}
#if REISER4_DEBUG
	assert("edward-2194", j == nr_dsa_bricks);
	for (i = 0; i < nr_dsa_bricks; i++) {
		assert("edward-2181", ret[i] != NULL);
		assert("edward-2195",
		       ((reiser4_subvol *)ret[i])->dsa_idx == i);
	}
#endif
	return (bucket_t *)ret;
}

/**
 * Remove an abstract bucket at position @pos from the array
 * @vec of @numb buckets
 */
static void remove_bucket(bucket_t *vec, u32 numb, u32 pos)
{
	u32 i;
	/*
	 * indexes of all buckets at the right to @pos got decremented
	 */
	for (i = pos + 1; i < numb; i++) {
		assert("edward-2196",
		       ((reiser4_subvol *)(vec[i]))->dsa_idx != 0);
		((reiser4_subvol *)(vec[i]))->dsa_idx --;
	}
	memmove(vec + pos, vec + pos + 1,
		(numb - pos - 1) * sizeof(*vec));
	vec[numb - 1] = NULL;
}

/**
 * Insert an abstract bucket @this at position @pos in the array
 * @vec of @numb buckets
 */
static void insert_bucket(bucket_t *vec, bucket_t this, u32 numb, u32 pos)
{
	u32 i;
	/*
	 * indexes of all buckets at @pos and at the right to @pos
	 * got incremented
	 */
	for (i = pos; i < numb; i++)
		((reiser4_subvol *)(vec[i]))->dsa_idx ++;

	memmove(vec + pos + 1, vec + pos,
		(numb - pos) * sizeof(*vec));
	vec[pos] = this;
}

static u32 id2idx(u64 id)
{
	assert("edward-2202", current_subvols() != NULL);
	assert("edward-2203", current_subvols()[id]!= NULL);

	return current_subvols()[id][0]->dsa_idx;
}

static u64 idx2id(u32 idx)
{
	reiser4_volume *vol = current_volume();
	bucket_t *vec = vol->dist_plug->v.get_buckets(&vol->aid);

	return ((reiser4_subvol *)(vec[idx]))->id;
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

static void release_volinfo_nodes(reiser4_volinfo *vinfo, int dealloc)
{
	u64 i;

	if (vinfo->volmap_nodes == NULL)
		return;

	for (i = 0; i < vinfo->num_volmaps + vinfo->num_voltabs; i++) {
		struct jnode *node = vinfo->volmap_nodes[i];
		if (node) {
			if (dealloc)
				reiser4_dealloc_block(jnode_get_block(node),
						0, BA_FORMATTED | BA_PERMANENT,
						get_meta_subvol());
			reiser4_drop_volinfo_head(node);
			vinfo->volmap_nodes[i] = NULL;
		}
	}
	kfree(vinfo->volmap_nodes);
	vinfo->volmap_nodes = NULL;
	vinfo->voltab_nodes = NULL;
}

static void done_volume_asym(reiser4_subvol *subv)
{
	int i;
	reiser4_volume *vol = super_volume(subv->super);

	for (i = 0; i < 2; i++)
		release_volinfo_nodes(&vol->volinfo[i], 0);
}

/**
 * Load system volume configutation to memory.
 * @id is either 0 (to load current config), or 1 (to load new
 * config for unbalanced volumes).
 */
static int load_volume_config(reiser4_subvol *subv, int id)
{
	int ret;
	int i, j;
	u32 num_aid_bricks;
	u64 packed_segments = 0;
	reiser4_volume *vol = super_volume(subv->super);
	reiser4_volinfo *vinfo = &vol->volinfo[id];
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc = subv->volmap_loc[id];
	u64 voltabs_needed;

	assert("edward-1984", subv->id == METADATA_SUBVOL_ID);
	assert("edward-2175", subv->volmap_loc[id] != 0);

	if (subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM))
		num_aid_bricks = vol_nr_origins(vol);
	else {
		assert("edward-1985", vol_nr_origins(vol) > 1);
		num_aid_bricks = vol_nr_origins(vol) - 1;
	}
	if (dist_plug->r.init) {
		ret = dist_plug->r.init(&vol->aid,
					num_aid_bricks,
					vol->num_sgs_bits, id);
		if (ret)
			return ret;
	}
	vinfo->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vinfo->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);
	voltabs_needed = vinfo->num_voltabs;

	vinfo->volmap_nodes =
		kzalloc((vinfo->num_volmaps + vinfo->num_voltabs) *
			sizeof(*vinfo->volmap_nodes), GFP_KERNEL);

	if (!vinfo->volmap_nodes) {
		dist_plug->r.done(&vol->aid);
		return -ENOMEM;
	}
	vinfo->voltab_nodes = vinfo->volmap_nodes + vinfo->num_volmaps;

	for (i = 0; i < vinfo->num_volmaps; i++) {
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		vinfo->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, subv);
		if (!vinfo->volmap_nodes[i]) {
			dist_plug->r.done(&vol->aid);
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(vinfo->volmap_nodes[i]);
		if (ret) {
			dist_plug->r.done(&vol->aid);
			goto unpin;
		}
		volmap = (struct volmap *)jdata(vinfo->volmap_nodes[i]);
		/*
		 * load all voltabs pointed by current volmap
		 */
		for (j = 0;
		     j < voltab_nodes_per_block() && voltabs_needed;
		     j++, voltabs_needed --) {

			reiser4_block_nr voltab_loc;

			voltab_loc = volmap_get_entry_blk(volmap, j);
			assert("edward-1986", voltab_loc != 0);

			vinfo->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   subv);
			if (!vinfo->voltab_nodes[j]) {
				ret = -ENOMEM;
				dist_plug->r.done(&vol->aid);
				goto unpin;
			}
			ret = jload(vinfo->voltab_nodes[j]);
			if (ret) {
				dist_plug->r.done(&vol->aid);
				goto unpin;
			}
			dist_plug->v.unpack(&vol->aid,
					    jdata(vinfo->voltab_nodes[j]),
					    packed_segments,
					    segments_per_block(vol),
					    id);
			jrelse(vinfo->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		volmap_loc = get_next_volmap_addr(volmap);
		jrelse(vinfo->volmap_nodes[i]);
	}
	vinfo->volinfo_gen = subv->volinfo_gen + id;
 unpin:
	release_volinfo_nodes(vinfo, 0);
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

static int dealloc_volinfo_block(reiser4_block_nr *block, reiser4_subvol *subv)
{
	return reiser4_dealloc_block(block, BLOCK_NOT_COUNTED, BA_DEFER, subv);
}

/**
 * Release disk addresses occupied by volume configuration
 */
static int release_volume_config(reiser4_volume *vol, int id)
{
	int ret;
	int i, j;
	reiser4_subvol *mtd_subv = get_meta_subvol();
	reiser4_block_nr volmap_loc = mtd_subv->volmap_loc[id];
	reiser4_volinfo *vinfo = &vol->volinfo[id];
	u64 voltabs_needed;

	if (volmap_loc == 0)
		/* nothing to release */
		return 0;
	/*
	 * FIXME: this is a hack to make sure that atom exists
	 */
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;

	voltabs_needed = vinfo->num_voltabs;

	for (i = 0; i < vinfo->num_volmaps; i++) {
		jnode *node;
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		node = reiser4_alloc_volinfo_head(&volmap_loc, mtd_subv);
		if (!node)
			return -ENOMEM;

		ret = jload(node);
		if (ret) {
			reiser4_drop_volinfo_head(node);
			return ret;
		}
		volmap = (struct volmap *)jdata(node);
		/*
		 * deallocate all voltab blocks pointed out by current volmap
		 */
		for (j = 0;
		     j < voltab_nodes_per_block() && voltabs_needed;
		     j++, voltabs_needed --) {

			reiser4_block_nr voltab_loc;

			voltab_loc = volmap_get_entry_blk(volmap, j);
			assert("edward-1987", voltab_loc != 0);
			dealloc_volinfo_block(&voltab_loc, get_meta_subvol());
		}
		dealloc_volinfo_block(&volmap_loc, get_meta_subvol());
		volmap_loc = get_next_volmap_addr(volmap);
		jrelse(node);
		reiser4_drop_volinfo_head(node);
	}
	return 0;
}

/**
 * Release old volume configuration and make new volume
 * configuration as "current one".
 */
static int update_volume_config(reiser4_volume *vol)
{
	int ret;
	reiser4_subvol *mtd_subv = get_meta_subvol();

	ret = release_volume_config(vol, CUR_VOL_CONF);
	if (ret)
		return ret;

	vol->volinfo[CUR_VOL_CONF] = vol->volinfo[NEW_VOL_CONF];
	memset(&vol->volinfo[NEW_VOL_CONF], 0, sizeof(reiser4_volinfo));

	mtd_subv->volmap_loc[CUR_VOL_CONF] = mtd_subv->volmap_loc[NEW_VOL_CONF];
	mtd_subv->volmap_loc[NEW_VOL_CONF] = 0;
	if (mtd_subv->volmap_loc[CUR_VOL_CONF] != 0)
		mtd_subv->volinfo_gen ++;
	else
		/* absent config - reset generation counter */
		mtd_subv->volinfo_gen = 0;
	assert("edward-2176",
	       mtd_subv->volinfo_gen == vol->volinfo[CUR_VOL_CONF].volinfo_gen);

	vol->dist_plug->r.update(&vol->aid);
	return 0;
}

/**
 * Create and pin volinfo nodes, allocate disk addresses for them,
 * and pack in-memory volume system information to those nodes
 */
noinline int create_volume_config(reiser4_volume *vol, int id)
{
	int ret;
	int i, j;
	u64 packed_segments = 0;
	reiser4_subvol *meta_subv = get_meta_subvol();
	reiser4_volinfo *vinfo = &vol->volinfo[id];

	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc;
	u64 voltabs_needed;

	assert("edward-2177", meta_subv->volmap_loc[id] == 0);

	ret = reiser4_create_atom();
	if (ret)
		return ret;
	/*
	 * allocate disk address of the first volmap block
	 */
	ret = alloc_volinfo_block(&volmap_loc, meta_subv);
	if (ret)
		return ret;
	/*
	 * set location of the first block of volume config
	 */
	meta_subv->volmap_loc[id] = volmap_loc;

	vinfo->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vinfo->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);
	voltabs_needed = vinfo->num_voltabs;

	vinfo->volmap_nodes =
		kzalloc((vinfo->num_volmaps + vinfo->num_voltabs) *
			sizeof(void *), GFP_KERNEL);

	if (!vinfo->volmap_nodes) {
		/*
		 * release disk address which was just allocated
		 */
		reiser4_dealloc_block(&volmap_loc, 0,
				      BA_FORMATTED | BA_PERMANENT, meta_subv);
		meta_subv->volmap_loc[id] = 0;
		return -ENOMEM;
	}
	vinfo->voltab_nodes = vinfo->volmap_nodes + vinfo->num_volmaps;

	for (i = 0; i < vinfo->num_volmaps; i++) {
		struct volmap *volmap;

		vinfo->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, meta_subv);
		if (!vinfo->volmap_nodes[i]) {
			reiser4_dealloc_block(&volmap_loc, 0,
					      BA_FORMATTED | BA_PERMANENT,
					      meta_subv);
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jinit_new(vinfo->volmap_nodes[i], GFP_KERNEL);
		if (ret)
			goto unpin;
		volmap = (struct volmap *)jdata(vinfo->volmap_nodes[i]);
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

			vinfo->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   meta_subv);
			if (!vinfo->voltab_nodes[j]) {
				reiser4_dealloc_block(&voltab_loc, 0,
					        BA_FORMATTED | BA_PERMANENT,
					        meta_subv);
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jinit_new(vinfo->voltab_nodes[j],
					GFP_KERNEL);
			if (ret)
				goto unpin;
			dist_plug->v.pack(&vol->aid,
					  jdata(vinfo->voltab_nodes[j]),
					  packed_segments,
					  segments_per_block(vol),
					  id);
			jrelse(vinfo->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		if (i == vinfo->num_volmaps - 1)
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
		jrelse(vinfo->volmap_nodes[i]);
	}
	vinfo->volinfo_gen = meta_subv->volinfo_gen + id;
	return 0;
 unpin:
	release_volinfo_nodes(vinfo, 1 /* release disk addresses */);
	meta_subv->volmap_loc[id] = 0;
	return ret;
}

/*
 * Capture an array of jnodes, make them dirty and mark as relocate
 */
static int capture_array_nodes(jnode **start, u64 count)
{
	u64 i;
	int ret;

	for (i = 0; i < count; i++) {
		jnode *node;
		node = start[i];
		set_page_dirty_notag(jnode_page(node));

		spin_lock_jnode(node);
		/*
		 * volinfo nodes are always written to new location
		 */
		jnode_set_reloc(node);
		ret = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(ret != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	return 0;
}

static int capture_volume_config(reiser4_volume *vol, int id)
{
	int ret;
	reiser4_volinfo *vinfo = &vol->volinfo[id];
	/*
	 * Capture format superblock of meta-data brick with
	 * updated location of the first volmap block.
	 */
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	return capture_array_nodes(vinfo->volmap_nodes,
				   vinfo->num_volmaps + vinfo->num_voltabs);
}

/**
 * Create volume configuration, put it into transaction
 * and commit the last one.
 */
static int make_volume_config(reiser4_volume *vol)
{
	int ret;
	txn_atom *atom;
	txn_handle *th;
	int id = volinfo_absent() ? CUR_VOL_CONF : NEW_VOL_CONF;

	ret = create_volume_config(vol, id);
	if (ret)
		return ret;
	ret = capture_volume_config(vol, id);
	if (ret)
		goto error;
	/*
	 * write volinfo to disk
	 */
	th = get_current_context()->trans;
	atom = get_current_atom_locked();
	assert("edward-1988", atom != NULL);
	spin_lock_txnh(th);
	ret = force_commit_atom(th);
	if (ret)
		goto error;
	release_volinfo_nodes(&vol->volinfo[id], 0);
	return 0;
 error:
	release_volinfo_nodes(&vol->volinfo[id], 1);
	return ret;
}

/*
 * This is called at mount time
 */
static int load_volume_asym(reiser4_subvol *subv)
{
	int ret;
	reiser4_volume *vol = super_volume(subv->super);

	if (subv->id != METADATA_SUBVOL_ID)
		/*
		 * system configuration of assymetric LV
		 * is stored only in meta-data subvolume
		 */
		return 0;
	if (subv->volmap_loc[CUR_VOL_CONF] == 0)
		/* configuration is absent */
		return 0;

	ret = load_volume_config(subv, CUR_VOL_CONF);
	if (ret)
		return ret;

	if (subv->volmap_loc[NEW_VOL_CONF] == 0) {
		assert("edward-2178",
		       !reiser4_volume_is_unbalanced(subv->super));
		return 0;
	}
	assert("edward-2179",
	       reiser4_volume_is_unbalanced(subv->super));
	/*
	 * FIXME-EDWARD: complete data migration of partially
	 * migrated files (if any).
	 */
	printk("reiser4 (%s): Volume is unbalanced. Run volume.reiser4 -b",
	       subv->super->s_id);

	ret = load_volume_config(subv, NEW_VOL_CONF);
	if (ret)
		release_volinfo_nodes(&vol->volinfo[CUR_VOL_CONF], 0);
	return ret;
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

/**
 * Bucket operations.
 * The following methods translate bucket_t to mirror_t
 */
static u64 cap_at_asym(bucket_t *buckets, u64 idx)
{
	return ((mirror_t *)buckets)[idx]->data_room;
}

static void *fib_of_asym(bucket_t bucket)
{
	return ((mirror_t)bucket)->fiber;
}

static void *fib_at_asym(bucket_t *buckets, u64 index)
{
	return fib_of_asym(buckets[index]);
}

static void fib_set_at_asym(bucket_t *buckets, u64 idx, void *fiber)
{
	((mirror_t *)buckets)[idx]->fiber = fiber;
}

static u64 *fib_lenp_at_asym(bucket_t *buckets, u64 idx)
{
	return &((mirror_t *)buckets)[idx]->fiber_len;
}

static reiser4_subvol *origin_at(slot_t slot)
{
	return ((mirror_t *)slot)[0];
}

static u64 blocks_free_at(slot_t slot)
{
	return origin_at(slot)->blocks_free;
}

static u64 capacity_at(slot_t slot)
{
	return origin_at(slot)->data_room;
}

/**
 * Return first non-empty data slot.
 * If such slot not found, then return NULL
 */
static slot_t find_first_nonempty_data_slot(void)
{
	u32 subv_id;
	struct super_block *sb = reiser4_get_current_sb();

	for_each_data_vslot(subv_id)
		if (super_mirrors(sb, subv_id))
			return super_mirrors(sb, subv_id);
	return NULL;
}

static u64 space_occupied_at(slot_t slot)
{
	if (is_meta_brick(origin_at(slot))) {
		slot_t neighbor;
		/*
		 * In asymmetric LV we don't track a number of busy
		 * data blocks on the meta-data brick. However, we can
		 * calculate it approximately by the portion of busy
		 * data blocks on the neighboring data brick. The last
		 * one has to exist, because there is no need to know
		 * number of data blocks occupied in asymmetric logical
		 * volume consisting of a single meta-data brick (and,
		 * hence, to call this function).
		 */
		assert("edward-2069", current_nr_origins() > 1);

		neighbor = find_first_nonempty_data_slot();
		BUG_ON(neighbor == NULL);

		return div64_u64(capacity_at(slot) *
				 space_occupied_at(neighbor),
				 capacity_at(neighbor));
	} else
		/* data brick */
		return capacity_at(slot) - blocks_free_at(slot);
}

/**
 * Return position of @subv in logical volume @vol (it is not
 * necessarily coincides with @subv's internal ID).
 */
static u64 get_pos_in_vol(reiser4_volume *vol, reiser4_subvol *subv)
{
	u64 i, j;

	for (i = 0, j = 0; i < vol->nr_slots; i++) {
		if (!vol->subvols[i])
			continue;
		if (vol->subvols[i][0] == subv)
			return j;
		j++;
	}
	assert("edward-2197", i == vol->nr_slots);
	assert("edward-2198", j == vol_nr_origins(vol));
	return j;
}

int brick_belongs_volume(reiser4_volume *vol, reiser4_subvol *subv)
{
	return get_pos_in_vol(vol, subv) < vol_nr_origins(vol);
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
	ret = dist_plug->v.inc(&vol->aid, get_pos_in_aid(this->id), NULL);
	if (ret)
		/* roll back */
		this->data_room -= delta;
	return ret;
}

static int add_meta_brick(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;

	assert("edward-1820", is_meta_brick(new));
	/*
	 * We don't need to activate meta-data brick:
	 * it is always active in the mount session of the logical volume.
	 */
	ret = vol->dist_plug->v.inc(&vol->aid,
				    METADATA_SUBVOL_ID /* pos */, new);
	if (ret)
		return ret;
	new->flags |= (1 << SUBVOL_HAS_DATA_ROOM);
	return 0;
}

/**
 * Find first empty slot in the array of volume's slots and
 * return its offset in that array. If all slots are busy,
 * then return number of slots.
 */
static u32 find_first_empty_slot_off(void)
{
	u32 subv_id;
	struct super_block *sb = reiser4_get_current_sb();

	for_each_data_vslot(subv_id)
		if (super_mirrors(sb, subv_id) == NULL)
			return subv_id;

	assert("edward-2183",
	       current_volume()->nr_slots == current_nr_origins());
	return current_nr_origins();
}

int add_data_brick(reiser4_volume *vol, reiser4_subvol *this)
{
	int ret;
	reiser4_aid *raid = &vol->aid;
	slot_t *new_set = NULL;
	slot_t *old_set = vol->subvols;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u64 pos_in_aid;
	slot_t new_slot;

	assert("edward-1929", !is_meta_brick(this));

	pos_in_vol = find_first_empty_slot_off();
	pos_in_aid = get_pos_in_aid(pos_in_vol);
	/*
	 * Assign internal ID for the new brick
	 */
	this->id = pos_in_vol;
	ret = vol->dist_plug->v.inc(raid, pos_in_aid, this);
	if (ret)
		return ret;
	/*
	 * Allocate array of mirrors and set the new brick to that set
	 */
	new_slot = alloc_one_mirror_slot(1 + this->num_replicas);
	if (!new_slot)
		return -ENOMEM;
	((mirror_t *)new_slot)[this->mirror_id] = this;

	if (pos_in_vol == vol->nr_slots) {
		/*
		 * there is no free slot -
		 * the set of slots will be replaced with a larger one
		 */
		new_set = alloc_mirror_slots(1 + old_num_subvols);
		if (!new_set) {
			kfree(new_slot);
			return -ENOMEM;
		}
		memcpy(new_set, old_set, sizeof(*new_set) * old_num_subvols);

		new_set[pos_in_vol] = new_slot;
		vol->nr_slots ++;
		vol->subvols = new_set;
		free_mirror_slots(old_set);
	} else {
		/* use existing slot */
		assert("edward-2184", old_set[pos_in_vol] == NULL);
		old_set[pos_in_vol] = new_slot;
	}
	atomic_inc(&vol->nr_origins);
	return 0;
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
	bucket_t *buckets;

	assert("edward-1824", raid != NULL);
	assert("edward-1825", dist_plug != NULL);

	buckets = create_buckets(vol, num_aid_subvols(vol));
	if (!buckets)
		return -ENOMEM;

	ret = dist_plug->v.init(buckets,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->bucket_ops, raid);
	if (ret) {
		kfree(buckets);
		return ret;
	}
	ret = expand_brick(vol, this, delta, &need_balance);
	dist_plug->v.done(raid);
	if (ret)
		return ret;
	if (need_balance) {
		ret = make_volume_config(vol);
		if (ret)
			return ret;
		ret = capture_brick_super(this);
		if (ret)
			return ret;
		reiser4_volume_set_unbalanced(sb);
		ret = capture_brick_super(get_meta_subvol());
	} else
		ret = capture_brick_super(this);
	if (ret)
		return ret;

	reiser4_txn_restart_current();
	return 0;
}

/**
 * Add a @new brick to asymmetric logical volume @vol
 */
static int add_brick_asym(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	bucket_t *buckets;

	assert("edward-1931", dist_plug != NULL);

	if (new->data_room == 0) {
		warning("edward-1962", "Can't add brick of zero capacity");
		return -EINVAL;
	}
	if (brick_belongs_aid(vol, new)) {
		warning("edward-1963", "Can't add brick to AID twice");
		return -EINVAL;
	}
	buckets = create_buckets(vol, num_aid_subvols(vol) + 1);
	if (!buckets)
		return -ENOMEM;
	ret = dist_plug->v.init(buckets, num_aid_subvols(vol),
				vol->num_sgs_bits,
				&vol->vol_plug->bucket_ops, &vol->aid);
	if (ret)
		return ret;

	if (new == get_meta_subvol())
		ret = add_meta_brick(vol, new);
	else
		ret = add_data_brick(vol, new);
	dist_plug->v.done(&vol->aid);
	if (ret)
		return ret;
	ret = make_volume_config(vol);
	if (ret)
		return ret;
	ret = capture_brick_super(new);
	if (ret)
		return ret;

	reiser4_volume_set_unbalanced(reiser4_get_current_sb());
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	reiser4_txn_restart_current();
	return 0;
}

static u64 space_occupied(void)
{
	u64 ret = 0;
	u64 subv_id;
	struct super_block *sb = reiser4_get_current_sb();

	txnmgr_force_commit_all(sb, 0);

	for_each_vslot(subv_id) {
		if (super_mirrors(sb, subv_id) == NULL)
			continue;
		ret += space_occupied_at(super_mirrors(sb, subv_id));
	}
	return ret;
}

static int shrink_brick(reiser4_volume *vol, reiser4_subvol *victim,
			u64 delta, int *need_balance)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;

	assert("edward-2191", victim->data_room > delta);
	/*
	 * FIXME-EDWARD: Check that resulted capacity is not too small
	 */
	victim->data_room -= delta;

	if (num_aid_subvols(vol) == 1) {
		*need_balance = 0;
		return 0;
	}
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

	assert("edward-1844", num_aid_subvols(vol) > 1);
	assert("edward-1826", meta_brick_belongs_aid());

	ret = dist_plug->v.dec(&vol->aid, METADATA_SUBVOL_ID, mtd_subv);
	if (ret)
		return ret;

	clear_bit(SUBVOL_HAS_DATA_ROOM, &mtd_subv->flags);

	assert("edward-1827", !meta_brick_belongs_aid());
	return 0;
}

/**
 * Find rightmost non-empty slot different from the last one.
 * If not found, return 0. Otherwise return slot's offset + 1.
 */
static u32 get_new_nr_slots(void)
{
	u32 i;
	reiser4_volume *vol = current_volume();

	assert("edward-2208", vol->nr_slots > 1);

	for (i = vol->nr_slots - 2;; i--) {
		if (vol->subvols[i])
			return i + 1;
		if (i == 0)
			break;
	}
	return 0;
}

static int remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim,
			     slot_t **newp, u32 *new_nr_slots)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	slot_t *new = NULL;
	slot_t *old = vol->subvols;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u64 pos_in_aid;

	assert("edward-1842", num_aid_subvols(vol) > 1);

	pos_in_vol = get_pos_in_vol(vol, victim);
	assert("edward-2199", pos_in_vol < old_num_subvols);
	pos_in_aid = get_pos_in_aid(pos_in_vol);

	if (pos_in_vol == old_num_subvols - 1) {
		/*
		 * removing the rightmost brick -
		 * the set of slots will be replaced with a smaller one.
		 */
		*new_nr_slots = get_new_nr_slots();
		BUG_ON(*new_nr_slots == 0);
		new = alloc_mirror_slots(*new_nr_slots);
		if (!new)
			return RETERR(-ENOMEM);
		memcpy(new, old, *new_nr_slots * sizeof(*old));
	}
	ret = dist_plug->v.dec(&vol->aid, pos_in_aid, victim);
	if (ret) {
		warning("edward-2146",
			"Failed to update distribution config (%d)", ret);
		goto error;
	}
	if (new)
		*newp = new;
	return 0;
 error:
	if (new)
		free_mirror_slots(new);
	return ret;
}

static int remove_brick_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	slot_t *new = NULL;
	slot_t *old = vol->subvols;
	u32 new_nr_slots = 0;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();
	bucket_t *buckets;
	u32 old_nr_dsa_bricks = num_aid_subvols(vol);

	assert("edward-1830", vol != NULL);
	assert("edward-1846", dist_plug != NULL);

	if (old_nr_dsa_bricks == 1) {
		warning("edward-1941",
			"Can't remove the single brick from AID");
		return -EINVAL;
	}
	buckets = create_buckets(vol, old_nr_dsa_bricks);
	if (!buckets)
		return -ENOMEM;
	ret = dist_plug->v.init(buckets, old_nr_dsa_bricks,
				vol->num_sgs_bits,
				&vol->vol_plug->bucket_ops, &vol->aid);
	if (ret)
		return ret;

	if (is_meta_brick(victim))
		ret = remove_meta_brick(vol);
	else
		ret = remove_data_brick(vol, victim, &new, &new_nr_slots);
	dist_plug->v.done(&vol->aid);
	if (ret)
		return ret;
	if (old_nr_dsa_bricks > 2) {
		ret = make_volume_config(vol);
		if (ret) {
			free_mirror_slots(new);
			return ret;
		}
	}
	/*
	 * set unbalanced status and write it to disk
	 */
	reiser4_volume_set_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret) {
		free_mirror_slots(new);
		return ret;
	}
	reiser4_txn_restart_current();
	printk("reiser4 (%s): Brick %s has been removed.\n",
	       sb->s_id, victim->name);

	ret = balance_volume_asym(sb);
	/*
	 * New volume config has been stored on disk.
	 * If balancing was interrupted for some reasons (system
	 * crash, etc), then user just need to resume it by
	 * volume.reiser4 utility in the next mount session.
	 */
	if (ret) {
		free_mirror_slots(new);
		return ret;
	}
	if (is_meta_brick(victim))
		return 0;
	/*
	 * We are about to release @victim with replicas.
	 * Before this, it is absolutely necessarily to
	 * commit everything to make sure that there is
	 * no pending IOs addressed to the @victim.
	 */
	txnmgr_force_commit_all(sb, 1);
	/*
	 * It is safe to release victim with replicas, as
	 * now nobody is aware about them
	 */
	free_mirror_slot_at(vol, victim->id);
	atomic_dec(&vol->nr_origins);
	if (new) {
		/* replace slots array with a new one of
		 * smaller size
		 */
		assert("edward-2209", new_nr_slots != 0);

		vol->subvols = new;
		free_mirror_slots(old);
		vol->nr_slots = new_nr_slots;
	} else
		((mirror_t *)old)[victim->id] = NULL;
	return 0;
}

static int shrink_brick_asym(reiser4_volume *vol, reiser4_subvol *victim,
			     u64 delta)
{
	int ret;
	int need_balance = 1;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();
	bucket_t *buckets;

	assert("edward-2185", vol != NULL);
	assert("edward-2186", dist_plug != NULL);
	assert("edward-2187", dist_plug->h.id != TRIV_DISTRIB_ID);

	if (delta > victim->data_room) {
		warning("edward-2153",
			"Can't shrink brick by %llu (its capacity is %llu)",
			delta, victim->data_room);
		return -EINVAL;
	} else if (delta == victim->data_room)
		return remove_brick_asym(vol, victim);

	buckets = create_buckets(vol, num_aid_subvols(vol));
	if (!buckets)
		return -ENOMEM;

	ret = dist_plug->v.init(buckets,
				num_aid_subvols(vol), vol->num_sgs_bits,
				&vol->vol_plug->bucket_ops, &vol->aid);
	if (ret)
		return ret;

	ret = shrink_brick(vol, victim, delta, &need_balance);

	dist_plug->v.done(&vol->aid);
	if (ret)
		return ret;

	if (need_balance) {
		ret = make_volume_config(vol);
		if (ret)
			return ret;
	}
	ret = capture_brick_super(victim);
	if (ret)
		return ret;
	if (!need_balance)
		return 0;
	/*
	 * set unbalanced status and store it on disk
	 */
	reiser4_volume_set_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	reiser4_txn_restart_current();

	printk("reiser4 (%s): Brick %s has been shrunk.\n",
	       sb->s_id, victim->name);
	return 0;
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

	if (!tab) {
		assert("edward-2204", num_aid_subvols(vol) == 1);
		/*
		 * the single data brick is always in the last slot
		 */
		assert("edward-2212",
		       current_subvols()[vol->nr_slots - 1] != NULL);
		return current_subvols()[vol->nr_slots - 1][0]->id;
	}
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
	reiser4_key key;
	assert("edward-1957", coord != NULL);

	switch(item_id_by_coord(coord)) {
	case NODE_POINTER_ID:
	case EXTENT40_POINTER_ID:
		return METADATA_SUBVOL_ID;
	case EXTENT41_POINTER_ID:
		return get_key_ordering(item_key_by_coord(coord, &key));
	default:
		impossible("edward-2018", "Bad item ID");
		return METADATA_SUBVOL_ID;
	}
}

int print_volume_asym(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	reiser4_volume *vol = super_volume(sb);
	reiser4_volinfo *vinfo = &vol->volinfo[CUR_VOL_CONF];

	args->u.vol.nr_bricks = meta_brick_belongs_aid() ?
		vol_nr_origins(vol) : - vol_nr_origins(vol);
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	args->u.vol.fs_flags = get_super_private(sb)->fs_flags;
	args->u.vol.nr_slots = vol->nr_slots;
	args->u.vol.nr_volinfo_blocks = vinfo->num_volmaps + vinfo->num_voltabs;
	return 0;
}

int print_brick_asym(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	int ret = 0;
	u64 id;      /* internal ID */
	u64 idx_lv;  /* index in logical volume */

	reiser4_volume *vol = super_volume(sb);
	reiser4_subvol *subv;
	bucket_t *buckets;

	buckets = create_buckets(vol, num_aid_subvols(vol));
	if (!buckets)
		return -ENOMEM;

	ret = vol->dist_plug->v.init(buckets,
				     num_aid_subvols(vol), vol->num_sgs_bits,
				     &vol->vol_plug->bucket_ops, &vol->aid);
	if (ret) {
		kfree(buckets);
		return ret;
	}
	spin_lock_reiser4_super(get_super_private(sb));

	idx_lv = args->s.idx_lv;
	if (idx_lv >= vol_nr_origins(vol)) {
		ret = -EINVAL;
		goto out;
	}
	/* translate index in LV to brick ID */

	if (is_meta_brick_id(idx_lv))
		id = idx_lv;
	else if (meta_brick_belongs_aid())
		id = vol->vol_plug->bucket_ops.idx2id(idx_lv);
	else
		id = vol->vol_plug->bucket_ops.idx2id(idx_lv - 1);

	assert("edward-2206", vol->subvols[id] != NULL);

	subv = vol->subvols[id][0];
	strncpy(args->d.name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_room = subv->data_room;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.volinfo_addr = subv->volmap_loc[CUR_VOL_CONF];
 out:
	spin_unlock_reiser4_super(get_super_private(sb));
	/*
	 * this will release buckets
	 */
	vol->dist_plug->v.done(&vol->aid);
	return ret;
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
 * regular operations we need to check volume status.
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
	time_t start;
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
	printk("reiser4 (%s): Started balancing...\n", super->s_id);
	start = get_seconds();

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
	ret = update_volume_config(vol);
	if (ret)
		goto error;
	printk("reiser4 (%s): Balancing completed in %lu seconds.\n",
	       super->s_id, get_seconds() - start);
	return 0;
 error:
	warning("edward-2155", "%s: Balancing aborted (%d).", super->s_id, ret);
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
		.bucket_ops = {
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
		.print_brick = print_brick_asym,
		.print_volume = print_volume_asym,
		.balance_volume = balance_volume_asym,
		.bucket_ops = {
			.cap_at = cap_at_asym,
			.fib_of = fib_of_asym,
			.fib_at = fib_at_asym,
			.fib_set_at = fib_set_at_asym,
			.fib_lenp_at = fib_lenp_at_asym,
			.idx2id = idx2id,
			.id2idx = id2idx,
			.insert_bucket = insert_bucket,
			.remove_bucket = remove_bucket,
			.space_occupied = space_occupied
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
