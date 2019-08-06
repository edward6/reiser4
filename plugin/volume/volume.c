/*
  Copyright (c) 2016-2019 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "../../debug.h"
#include "../../super.h"
#include "../../inode.h"
#include "../../plugin/item/brick_symbol.h"
#include "volume.h"

/**
 * Implementations of simple and asymmetric logical volumes.
 * Asymmetric Logical Volume is a table of pointers to bricks
 * (subvolumes). Its first column represents meta-data brick
 * and its optional replicas. Other columns represent data
 * bricks with replicas. Data bricks contain only unformatted
 * blocks. Meta-data brick contain blocks of all types.
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
static int __remove_data_brick(reiser4_volume *vol,
			       reiser4_subvol *subv, u32 *pos_in_dsa);

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
 * Allocate and initialize an array of abstract buckets for an
 * asymmetric volume.
 * The notion of abstract bucket encapsulates an original brick
 * (without replicas). That array should include only DSA members.
 */
static bucket_t *create_buckets(void)
{
	reiser4_volume *vol = current_volume();
	lv_conf *conf = vol->conf;
	bucket_t *ret;
	u32 i, j, off;
	u32 nr_buckets = num_dsa_subvols(vol);

	off = meta_brick_belongs_dsa() ? 0 : 1;

	ret = kmalloc(nr_buckets * sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return NULL;

	for (i = 0, j = 0; i < conf->nr_mslots - off; i++) {
		if (conf->mslots[i + off] == NULL)
			continue;
		ret[j] = conf->mslots[i + off][0];
		/*
		 * set index in DSA
		 */
		conf->mslots[i + off][0]->dsa_idx = j;
		j++;
	}
#if REISER4_DEBUG
	assert("edward-2194", j == nr_buckets);
	for (i = 0; i < nr_buckets; i++) {
		assert("edward-2181", ret[i] != NULL);
		assert("edward-2195",
		       ((reiser4_subvol *)ret[i])->dsa_idx == i);
	}
#endif
	return (bucket_t *)ret;
}

static void free_buckets(bucket_t *vec)
{
	assert("edward-2233", vec != NULL);
	kfree(vec);
}

/**
 * Allocate and initialize a new array of abstract buckets,
 * which doesn't contain a bucket @this at position @pos in
 * the old array @vec. Return the new array.
 */
static bucket_t *remove_bucket(bucket_t *vec, u32 numb, u32 pos)
{
	bucket_t *new;

	assert("edward-2338", pos < numb);

	new = kmalloc((numb - 1) * sizeof(*new), GFP_KERNEL);
	if (new) {
		int i;
		/*
		 * indexes of all buckets at the right to @pos
		 * get decremented
		 */
		for (i = pos + 1; i < numb; i++) {
			assert("edward-2196",
			       ((reiser4_subvol *)(vec[i]))->dsa_idx == i);
			((reiser4_subvol *)(vec[i]))->dsa_idx --;
		}
		memcpy(new, vec, pos * (sizeof(*new)));
		memcpy(new + pos, vec + pos + 1,
		       (numb - pos - 1) * sizeof(*new));
	}
	return new;
}

static bucket_t *insert_bucket(bucket_t *vec, bucket_t this, u32 numb, u32 pos)
{
	bucket_t *new;

	assert("edward-2339", pos <= numb);

	new = kmalloc((numb + 1) * sizeof(*new), GFP_KERNEL);
	if (new) {
		u32 i;
		/*
		 * indexes of all buckets at @pos and at the right to @pos
		 * get incremented
		 */
		for (i = pos; i < numb; i++) {
			assert("edward-2340",
			       ((reiser4_subvol *)(vec[i]))->dsa_idx == i);
			((reiser4_subvol *)(vec[i]))->dsa_idx ++;
		}
		/*
		 * new bucket gets index @pos
		 */
		((reiser4_subvol *)this)->dsa_idx = pos;

		memcpy(new, vec, pos * (sizeof(*new)));
		new[pos] = this;
		memcpy(new + pos + 1, vec + pos, (numb - pos) * sizeof(*new));
	}
	return new;
}

static u32 id2idx(u64 id)
{
	return current_origin(id)->dsa_idx;
}

static u64 idx2id(u32 idx)
{
	bucket_t *vec = current_buckets();

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

void release_volinfo_nodes(reiser4_volinfo *vinfo, int dealloc)
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

static void done_volume_asym(reiser4_volume *vol)
{
	/*
	 * release set of abstract buckets
	 */
	if (vol->buckets) {
		free_buckets(vol->buckets);
		vol->buckets = NULL;
	}
	release_volinfo_nodes(&vol->volinfo[CUR_VOL_CONF], 0);
	release_volinfo_nodes(&vol->volinfo[NEW_VOL_CONF], 0);
}

/**
 * Load system volume configutation from disk to memory.
 */
static int load_volume_config(reiser4_subvol *subv)
{
	int id = CUR_VOL_CONF;
	int ret;
	int i, j;
	u32 num_dsa_bricks;
	u64 packed_segments = 0;
	reiser4_volume *vol = super_volume(subv->super);
	reiser4_volinfo *vinfo = &vol->volinfo[id];
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc = subv->volmap_loc[id];
	u64 voltabs_needed;

	assert("edward-1984", subv->id == METADATA_SUBVOL_ID);
	assert("edward-2175", subv->volmap_loc[id] != 0);

	if (subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM))
		num_dsa_bricks = vol_nr_origins(vol);
	else {
		assert("edward-1985", vol_nr_origins(vol) > 1);
		num_dsa_bricks = vol_nr_origins(vol) - 1;
	}
	if (dist_plug->r.init) {
		ret = dist_plug->r.init(&vol->dcx, &vol->conf->tab,
					vol->num_sgs_bits);
		if (ret)
			return ret;
	}
	vinfo->num_volmaps = num_volmap_nodes(vol, vol->num_sgs_bits);
	vinfo->num_voltabs = num_voltab_nodes(vol, vol->num_sgs_bits);
	voltabs_needed = vinfo->num_voltabs;

	vinfo->volmap_nodes =
		kzalloc((vinfo->num_volmaps + vinfo->num_voltabs) *
			sizeof(*vinfo->volmap_nodes), GFP_KERNEL);

	if (!vinfo->volmap_nodes)
		return -ENOMEM;

	vinfo->voltab_nodes = vinfo->volmap_nodes + vinfo->num_volmaps;

	for (i = 0; i < vinfo->num_volmaps; i++) {
		struct volmap *volmap;

		assert("edward-1819", volmap_loc != 0);

		vinfo->volmap_nodes[i] =
			reiser4_alloc_volinfo_head(&volmap_loc, subv);
		if (!vinfo->volmap_nodes[i]) {
			ret = -ENOMEM;
			goto unpin;
		}
		ret = jload(vinfo->volmap_nodes[i]);
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

			voltab_loc = volmap_get_entry_blk(volmap, j);
			assert("edward-1986", voltab_loc != 0);

			vinfo->voltab_nodes[j] =
				reiser4_alloc_volinfo_head(&voltab_loc,
							   subv);
			if (!vinfo->voltab_nodes[j]) {
				ret = -ENOMEM;
				goto unpin;
			}
			ret = jload(vinfo->voltab_nodes[j]);
			if (ret)
				goto unpin;

			dist_plug->v.unpack(&vol->dcx, vol->conf->tab,
					    jdata(vinfo->voltab_nodes[j]),
					    packed_segments,
					    segments_per_block(vol));
			jrelse(vinfo->voltab_nodes[j]);

			packed_segments += segments_per_block(vol);
		}
		volmap_loc = get_next_volmap_addr(volmap);
		jrelse(vinfo->volmap_nodes[i]);
	}
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
 * Release old on-disk volume configuration and make the new
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

	return 0;
}

/**
 * Create and pin volinfo nodes, allocate disk addresses for them,
 * and pack in-memory volume system information to those nodes
 */
int create_volume_config(reiser4_volume *vol, int id)
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
			dist_plug->v.pack(&vol->dcx,
					  jdata(vinfo->voltab_nodes[j]),
					  packed_segments,
					  segments_per_block(vol));
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

	ret = create_volume_config(vol, NEW_VOL_CONF);
	if (ret)
		return ret;
	ret = capture_volume_config(vol, NEW_VOL_CONF);
	if (ret)
		goto error;
	return 0;
 error:
	release_volinfo_nodes(&vol->volinfo[NEW_VOL_CONF],
			      1 /* release disk addresses */);
	return ret;
}

/*
 * This is called at mount time
 */
static int load_volume_asym(reiser4_subvol *subv)
{
	if (subv->id != METADATA_SUBVOL_ID)
		/*
		 * configuration of asymmetric volumes
		 * is stored only on meta-data brick
		 */
		return 0;
	if (subv->volmap_loc[CUR_VOL_CONF] == 0)
		/*
		 * volume configuration is absent on disk
		 */
		return 0;

	return load_volume_config(subv);
}

/*
 * Init volume system info, which has been already loaded
 * diring disk formats inialization of subvolumes (components).
 */
static int init_volume_asym(struct super_block *sb, reiser4_volume *vol)
{
	int ret;
	u32 subv_id;
	lv_conf *cur_conf;
	u32 nr_victims = 0;
	u32 pos_in_dsa;

	if (!REISER4_PLANB_KEY_ALLOCATION) {
		warning("edward-2161",
			"Asymmetric LV requires Plan-B key allocation scheme");
		return RETERR(-EINVAL);
	}
	assert("edward-2341", vol->buckets == NULL);
	/*
	 * Create an abstract set of buckets for this volume
	 */
	vol->buckets = create_buckets();
	if (!vol->buckets)
		return -ENOMEM;

	if (!reiser4_volume_is_unbalanced(sb))
		return 0;
	/*
	 * Rebalancing has to be completed on this volume
	 */
	assert("edward-2250", current_volume() == vol);
	/*
	 * Check for uncompleted brick removal that possibly
	 * was started in previous mount session.
	 */
	assert("edward-2244", vol->new_conf == NULL);
	cur_conf = vol->conf;

	for_each_mslot(cur_conf, subv_id) {
		reiser4_subvol *subv;

		if (!conf_mslot_at(cur_conf, subv_id))
			continue;
		subv = conf_origin(cur_conf, subv_id);
		if (subvol_is_set(subv, SUBVOL_TO_BE_REMOVED)) {
			vol->victim = subv;
			nr_victims ++;
		}
	}
	if (nr_victims > 1) {
		warning("edward-2246", "Too many bricks (%u) to be removed",
			nr_victims);
		return -EIO;
	} else if (nr_victims == 0)
		goto out;

	assert("edward-2251", vol->victim != NULL);
	/*
	 * vol->victim is not a meta-data brick, as when removing a
	 * meta-data brick we spawn only one checkpoint (similar to
	 * case of adding a brick). It is a speciality of asymmetric
	 * volumes.
	 */
	assert("edward-2252", !is_meta_brick(vol->victim));
	/*
	 * new config will be created here
	 */
	ret = __remove_data_brick(vol, vol->victim, &pos_in_dsa);
	if (ret)
		return ret;

	reiser4_volume_set_incomplete_op(sb);
 out:
	printk("reiser4 (%s): Volume is unbalanced. Please run volume.reiser4 -b\n",
	       sb->s_id);
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
	lv_conf *conf = current_lv_conf();

	for_each_data_mslot(conf, subv_id)
		if (conf->mslots[subv_id])
			return conf->mslots[subv_id];
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
	lv_conf *conf = vol->conf;

	for (i = 0, j = 0; i < conf->nr_mslots; i++) {
		if (!conf->mslots[i])
			continue;
		if (conf->mslots[i][0] == subv)
			return j;
		j++;
	}
	assert("edward-2197", i == conf->nr_mslots);
	assert("edward-2198", j == vol_nr_origins(vol));
	return j;
}

int brick_belongs_volume(reiser4_volume *vol, reiser4_subvol *subv)
{
	return get_pos_in_vol(vol, subv) < vol_nr_origins(vol);
}

/**
 * Calculate and return brick position in the DSA.
 * @pos_in_vol: internal ID of the brick in the logical volume.
 */
static u64 get_pos_in_dsa(u64 pos_in_vol)
{
	if (meta_brick_belongs_dsa())
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

	if (num_dsa_subvols(vol) == 1) {
		*need_balance = 0;
		return 0;
	}
	ret = dist_plug->v.inc(&vol->dcx, vol->conf->tab,
			       get_pos_in_dsa(this->id), NULL);
	if (ret)
		/* roll back */
		this->data_room -= delta;
	return ret;
}

/**
 * Create a config, which is similar to @old except the pointer
 * to distribution table (which is NULL for the clone).
 */
static lv_conf *clone_lv_conf(lv_conf *old)
{
	lv_conf *new;

	new = alloc_lv_conf(old->nr_mslots);
	if (new) {
		memcpy(new->mslots, old->mslots,
		       sizeof(slot_t) * old->nr_mslots);
	}
	return new;
}

static int add_meta_brick(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;
	bucket_t *new_vec;
	bucket_t *old_vec;

	assert("edward-1820", is_meta_brick(new));
	/*
	 * We don't need to activate meta-data brick:
	 * it is always active in the mount session of the logical volume.
	 *
	 * Number of bricks and slots in the logical volume remains the same.
	 *
	 * insert @new at the first place in the set of abstract buckets
	 */
	new_vec = insert_bucket(vol->buckets, new, num_dsa_subvols(vol),
				METADATA_SUBVOL_ID /* position to insert */);
	if (!new_vec)
		return -ENOMEM;
	old_vec = vol->buckets;
	vol->buckets = new_vec;
	free_buckets(old_vec);
	/*
	 * Create new distribution table.
	 */
	ret = vol->dist_plug->v.inc(&vol->dcx, vol->conf->tab,
				    METADATA_SUBVOL_ID /* pos */, new);
	if (ret)
		return ret;
	new->flags |= (1 << SUBVOL_HAS_DATA_ROOM);
	/*
	 * Clone in-memory volume config
	 */
	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL)
		return -ENOMEM;
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
	lv_conf *conf = current_lv_conf();

	for_each_data_mslot(conf, subv_id)
		if (conf->mslots[subv_id] == NULL)
			return subv_id;

	assert("edward-2183", conf->nr_mslots == current_nr_origins());
	return conf->nr_mslots;
}

int add_data_brick(reiser4_volume *vol, reiser4_subvol *this)
{
	int ret;
	reiser4_dcx *rdcx = &vol->dcx;
	lv_conf *old_conf = vol->conf;
	u64 old_nr_mslots = old_conf->nr_mslots;
	u64 pos_in_vol;
	u64 pos_in_dsa;
	slot_t new_slot;
	bucket_t *new_vec;
	bucket_t *old_vec;

	assert("edward-1929", !is_meta_brick(this));

	pos_in_vol = find_first_empty_slot_off();
	pos_in_dsa = get_pos_in_dsa(pos_in_vol);
	/*
	 * insert @this to the set of abstract buckets
	 */
	new_vec = insert_bucket(vol->buckets, this,
				num_dsa_subvols(vol), pos_in_dsa);
	if (!new_vec)
		return -ENOMEM;
	old_vec = vol->buckets;
	vol->buckets = new_vec;
	free_buckets(old_vec);
	/*
	 * Assign internal ID for the new brick
	 */
	this->id = pos_in_vol;
	/*
	 * Create new distribution table
	 */
	ret = vol->dist_plug->v.inc(rdcx, vol->conf->tab,
				    pos_in_dsa, this);
	if (ret)
		return ret;
	/*
	 * Create new in-memory volume config
	 */
	new_slot = alloc_mslot(1 + this->num_replicas);
	if (!new_slot)
		return -ENOMEM;

	((mirror_t *)new_slot)[this->mirror_id] = this;

	if (pos_in_vol == old_nr_mslots)
		/*
		 * There is no free slots in the old config -
		 * create a new one with a larger number of slots
		 */
		vol->new_conf = alloc_lv_conf(1 + old_nr_mslots);
	else
		vol->new_conf = alloc_lv_conf(old_nr_mslots);
	if (!vol->new_conf) {
		free_mslot(new_slot);
		return -ENOMEM;
	}
	memcpy(vol->new_conf->mslots, old_conf->mslots,
	       sizeof(slot_t) * old_nr_mslots);
	vol->new_conf->mslots[pos_in_vol] = new_slot;

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
	reiser4_dcx *rdcx = &vol->dcx;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();

	assert("edward-1824", rdcx != NULL);
	assert("edward-1825", dist_plug != NULL);

	ret = dist_plug->v.init(&vol->conf->tab,
				num_dsa_subvols(vol),
				vol->num_sgs_bits, rdcx);
	if (ret)
		return ret;

	ret = expand_brick(vol, this, delta, &need_balance);
	dist_plug->v.done(rdcx);
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
	lv_conf *old_conf = vol->conf;
	lv_conf *new_conf;

	assert("edward-1931", dist_plug != NULL);
	assert("edward-2262", vol->conf != NULL);
	assert("edward-2239", vol->new_conf == NULL);

	if (new->data_room == 0) {
		warning("edward-1962", "Can't add brick of zero capacity");
		return -EINVAL;
	}
	if (brick_belongs_dsa(vol, new)) {
		warning("edward-1963", "Can't add brick to DSA twice");
		return -EINVAL;
	}
	/*
	 * We allow to add meta-data bricks without any other conditions.
	 * In contrast, any data brick to add has to be empty.
	 */
	if (new != get_meta_subvol() &&
	    reiser4_subvol_used_blocks(new) >
	    reiser4_subvol_min_blocks_used(new)) {
		warning("edward-2334", "Can't add not empty data brick %s",
			new->name);
		return -EINVAL;
	}
	/* reserve space on meta-data subvolume for brick symbol insertion */
	grab_space_enable();
	ret = reiser4_grab_space(estimate_one_insert_into_item(
				 meta_subvol_tree()),
				 BA_CAN_COMMIT, get_meta_subvol());
	if (ret)
		return ret;
	ret = dist_plug->v.init(&vol->conf->tab,
				num_dsa_subvols(vol),
				vol->num_sgs_bits,
				&vol->dcx);
	if (ret)
		return ret;
	/*
	 * Create new in-memory volume config
	 */
	if (new == get_meta_subvol())
		ret = add_meta_brick(vol, new);
	else
		ret = add_data_brick(vol, new);

	new_conf = vol->new_conf;
	assert("edward-2240", new_conf != NULL);

	dist_plug->v.done(&vol->dcx);
	if (ret)
		return ret;
	ret = make_volume_config(vol);
	if (ret)
		goto error;
	ret = update_volume_config(vol);
	if (ret)
		goto error;
	if (new != get_meta_subvol()) {
		/* add a record about @new to the volume */
		ret = brick_symbol_add(new);
		if (ret)
			goto error;
	}
	dist_plug->r.replace(&vol->dcx, &new_conf->tab);

	reiser4_volume_set_unbalanced(reiser4_get_current_sb());
	/*
	 * Now publish the new config
	 */
	assert("edward-2346",
	       WRITE_DIST_LOCK != NULL && WRITE_DIST_UNLOCK != NULL);

	WRITE_DIST_LOCK(NULL);
	rcu_assign_pointer(vol->conf, new_conf);
	WRITE_DIST_UNLOCK(NULL);

	synchronize_rcu();
	free_lv_conf(old_conf);
	vol->new_conf = NULL;
	return 0;
 error:
	free_lv_conf(new_conf);
	return ret;
}

static u64 space_occupied(void)
{
	u64 ret = 0;
	u64 subv_id;
	lv_conf *conf = current_lv_conf();

	txnmgr_force_commit_all(reiser4_get_current_sb(), 0);

	for_each_mslot(conf, subv_id) {
		if (!conf->mslots[subv_id])
			continue;
		ret += space_occupied_at(conf->mslots[subv_id]);
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

	if (num_dsa_subvols(vol) == 1) {
		*need_balance = 0;
		return 0;
	}
	ret = dist_plug->v.dec(&vol->dcx, vol->conf->tab,
			       get_pos_in_dsa(victim->id), NULL);
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
	bucket_t *new_vec;
	bucket_t *old_vec;

	assert("edward-1844", num_dsa_subvols(vol) > 1);

	if (!meta_brick_belongs_dsa()) {
		warning("edward-2331",
			"Metadata brick doesn't belong to DSA. Can't remove.");
		return -EINVAL;
	}
	/*
	 * remove meta-data brick from the set of abstract buckets
	 */
	new_vec = remove_bucket(vol->buckets, num_dsa_subvols(vol),
				METADATA_SUBVOL_ID /* position in DSA */);
	if (!new_vec)
		return -ENOMEM;

	old_vec = vol->buckets;
	vol->buckets = new_vec;
	free_buckets(old_vec);

	ret = dist_plug->v.dec(&vol->dcx, vol->conf->tab,
			       METADATA_SUBVOL_ID, mtd_subv);
	if (ret)
		return ret;

	clear_bit(SUBVOL_HAS_DATA_ROOM, &mtd_subv->flags);
	assert("edward-1827", !meta_brick_belongs_dsa());
	/*
	 * Clone in-memory volume config
	 */
	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * Find rightmost non-empty slot different from the last one.
 * If not found, return 0. Otherwise return slot's offset + 1.
 */
static u32 get_new_nr_mslots(void)
{
	u32 i;
	lv_conf *conf = current_lv_conf();

	assert("edward-2208", conf->nr_mslots > 1);

	for (i = conf->nr_mslots - 2;; i--) {
		if (conf->mslots[i])
			return i + 1;
		if (i == 0)
			break;
	}
	return 0;
}

static int __remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim,
			       u32 *pos_in_dsa)
{
	lv_conf *old = vol->conf;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u32 new_nr_mslots;
	bucket_t *new_vec;
	bucket_t *old_vec;

	assert("edward-1842", num_dsa_subvols(vol) > 1);
	assert("edward-2253", vol->new_conf == NULL);

	pos_in_vol = get_pos_in_vol(vol, victim);
	assert("edward-2199", pos_in_vol < old_num_subvols);
	*pos_in_dsa = get_pos_in_dsa(pos_in_vol);

	if (pos_in_vol == old_num_subvols - 1) {
		/*
		 * removing the rightmost brick -
		 * config will be replaced with a new one
		 * with a smaller number of slots.
		 */
		new_nr_mslots = get_new_nr_mslots();
		BUG_ON(new_nr_mslots == 0);
	} else
		new_nr_mslots = old->nr_mslots;

	vol->new_conf = alloc_lv_conf(new_nr_mslots);
	if (!vol->new_conf)
		return RETERR(-ENOMEM);
	memcpy(vol->new_conf->mslots, old->mslots,
	       new_nr_mslots * sizeof(slot_t));

	if (pos_in_vol != old_num_subvols - 1) {
		/*
		 * In the new config mark respective slot as empty
		 */
		assert("edward-2241",
		       vol->new_conf->mslots[victim->id] != NULL);
		vol->new_conf->mslots[victim->id] = NULL;
	}
	/*
	 * remove @victim from the set of abstract buckets
	 */
	new_vec = remove_bucket(vol->buckets,
				num_dsa_subvols(vol), *pos_in_dsa);
	if (!new_vec)
		return -ENOMEM;

	old_vec = vol->buckets;
	vol->buckets = new_vec;
	free_buckets(old_vec);
	return 0;
}

static int remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	u32 pos_in_dsa;

	ret = __remove_data_brick(vol, victim, &pos_in_dsa);
	if (ret)
		return ret;
	ret = vol->dist_plug->v.dec(&vol->dcx, vol->conf->tab,
				    pos_in_dsa, victim);
	if (ret) {
		warning("edward-2146",
			"Failed to update distribution config (%d)", ret);
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return ret;
	}
	victim->flags |= (1 << SUBVOL_TO_BE_REMOVED);
	return 0;
}

static int remove_brick_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	lv_conf *tmp_conf;
	lv_conf *old_conf = vol->conf;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();
	u32 old_nr_dsa_bricks = num_dsa_subvols(vol);

	assert("edward-1830", vol != NULL);
	assert("edward-1846", dist_plug != NULL);

	if (old_nr_dsa_bricks == 1) {
		warning("edward-1941",
			"Can't remove the single brick from DSA");
		return -EINVAL;
	}
	ret = dist_plug->v.init(&vol->conf->tab,
				old_nr_dsa_bricks, vol->num_sgs_bits,
				&vol->dcx);
	if (ret)
		return ret;

	if (is_meta_brick(victim))
		ret = remove_meta_brick(vol);
	else
		ret = remove_data_brick(vol, victim);
	dist_plug->v.done(&vol->dcx);
	if (ret)
		return ret;
	assert("edward-2242", vol->new_conf != NULL);

	ret = make_volume_config(vol);
	if (ret) {
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return ret;
	}
	ret = update_volume_config(vol);
	if (ret) {
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return ret;
	}
	dist_plug->r.replace(&vol->dcx, &vol->new_conf->tab);

	if (!is_meta_brick(victim))
		/*
		 * put format super-block of data brick
		 * we want to remove to the transaction
		 */
		capture_brick_super(victim);
	/*
	 * set unbalanced status and put format super-block
	 * of meta-data brick to the transaction
	 */
	reiser4_volume_set_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret) {
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return ret;
	}
	/*
	 * Publish a temporal config with updated
	 * distribution table.
	 */
	tmp_conf = clone_lv_conf(old_conf);
	if (!tmp_conf) {
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return -ENOMEM;
	}
	tmp_conf->tab = vol->new_conf->tab;

	assert("edward-2348",
	       WRITE_DIST_LOCK != NULL && WRITE_DIST_UNLOCK != NULL);

	WRITE_DIST_LOCK(NULL);
	rcu_assign_pointer(vol->conf, tmp_conf);
	WRITE_DIST_UNLOCK(NULL);

	synchronize_rcu();
	free_lv_conf(old_conf);

	printk("reiser4 (%s): Brick %s has been removed.\n",
	       sb->s_id, victim->name);
	/*
	 * Re-balance the volume with the temporal config
	 */
	ret = balance_volume_asym(sb);
	/*
	 * If balancing was interrupted for some reasons (system
	 * crash, etc), then user just need to resume it by
	 * volume.reiser4 utility in the next mount session.
	 * The new config at vol->new_conf should be published
	 * only after successful re-balancing completion.
	 */
	if (ret) {
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
		return ret;
	}
	return remove_brick_tail_asym(vol, victim);
}

static int reserve_brick_symbol_del(void)
{
	reiser4_subvol *subv = get_meta_subvol();
	/*
	 * grab one block of meta-data brick to remove
	 * one item from a formatted node
	 */
	assert("edward-2303",
	       lock_stack_isclean(get_current_lock_stack()));
	grab_space_enable();
	return reiser4_grab_reserved(reiser4_get_current_sb(),
				     estimate_one_item_removal(&subv->tree),
				     BA_CAN_COMMIT, subv);
}

/**
 * Brick removal procedure completion. Publish new volume config.
 * Pre-condition: logical volume is fully balanced, but unbalanced
 * status is not yet cleared up.
 */
int remove_brick_tail_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	lv_conf *cur_conf = vol->conf;

	if (!is_meta_brick(victim)) {
		clear_bit(SUBVOL_TO_BE_REMOVED, &victim->flags);

		ret = capture_brick_super(victim);
		if (ret)
			return ret;
	}
	/*
	 * We are about to release @victim with replicas.
	 * Before this, it is absolutely necessarily to
	 * commit everything to make sure that there is
	 * no pending IOs addressed to the @victim and its
	 * replicas.
	 *
	 * During this commit @victim gets the last IO
	 * request as a member of the logical volume.
	 */
	txnmgr_force_commit_all(victim->super, 0);
	all_grabbed2free();
	reiser4_txn_restart_current();
	/*
	 * Publish final config with updated set of slots
	 */
	if (!is_meta_brick(victim)) {
		if (reiser4_subvol_used_blocks(victim) >
		    reiser4_subvol_min_blocks_used(victim)) {
			warning("edward-2335",
				"Can't remove data brick: not empty %s",
				victim->name);
			return -EINVAL;
		}
		/*
		 * remove a record about @victim from the volume
		 * and decrement number of bricks in the same
		 * transaction
		 */
		ret = reserve_brick_symbol_del();
		if (ret)
			return ret;
		ret = brick_symbol_del(victim);
		reiser4_release_reserved(reiser4_get_current_sb());
		if (ret)
			return ret;
		atomic_dec(&vol->nr_origins);
	}
	/*
	 * Publish final config with updated set of slots,
	 * which doesn't contain @victim.
	 * It doesn't change distribution policy, so we don't
	 * need to take a write lock on distribution here.
	 */
	rcu_assign_pointer(vol->conf, vol->new_conf);
	/*
	 * Release victim with replicas. It is safe,
	 * as at this point nobody is aware of them
	 */
	if (!is_meta_brick(victim))
		free_mslot_at(cur_conf, victim->id);

	synchronize_rcu();
	cur_conf->tab = NULL;
	free_lv_conf(cur_conf);

	vol->new_conf = NULL;
	return 0;
}

static int shrink_brick_asym(reiser4_volume *vol, reiser4_subvol *victim,
			     u64 delta)
{
	int ret;
	int need_balance = 1;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();

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

	ret = dist_plug->v.init(&vol->conf->tab,
				num_dsa_subvols(vol), vol->num_sgs_bits,
				&vol->dcx);
	if (ret)
		return ret;

	ret = shrink_brick(vol, victim, delta, &need_balance);

	dist_plug->v.done(&vol->dcx);
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

static u64 data_subvol_id_calc_simple(lv_conf *conf, const struct inode *inode,
				      loff_t offset)
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

static u64 data_subvol_id_calc_asym(lv_conf *conf, const struct inode *inode,
				    loff_t offset)
{
	u64 ret;
	reiser4_volume *vol = current_volume();

	assert("edward-2267", conf != NULL);

	if (!conf->tab) {
		/*
		 * the single data brick is always in the last slot
		 */
		assert("edward-2212",
		       conf_mslot_at(conf, conf_nr_mslots(conf) - 1) != NULL);

		ret = conf_origin(conf, conf->nr_mslots - 1)->id;
	} else {
		u64 stripe_idx;
		distribution_plugin *dist_plug = current_dist_plug();

		if (vol->stripe_bits) {
			stripe_idx = offset >> vol->stripe_bits;
			put_unaligned(cpu_to_le64(stripe_idx), &stripe_idx);
		} else
			stripe_idx = 0;

		ret = dist_plug->r.lookup(&vol->dcx, inode,
					  (const char *)&stripe_idx,
					  sizeof(stripe_idx),
					  get_seed(get_inode_oid(inode), vol),
					  conf->tab);
	}
	return ret;
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

int print_volume_simple(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	reiser4_volume *vol = super_volume(sb);

	args->u.vol.nr_bricks = 1;
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	args->u.vol.stripe_bits = vol->stripe_bits;
	args->u.vol.fs_flags = get_super_private(sb)->fs_flags;
	args->u.vol.nr_mslots = vol->conf->nr_mslots;
	args->u.vol.nr_volinfo_blocks = 0;
	return 0;
}

int print_brick_simple(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	reiser4_subvol *subv;
	reiser4_volume *vol = super_volume(sb);

	spin_lock_reiser4_super(get_super_private(sb));

	subv = vol->conf->mslots[0][0];
	strncpy(args->d.name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_room = subv->data_room;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.system_blocks = subv->min_blocks_used;
	args->u.brick.volinfo_addr = 0;

	spin_unlock_reiser4_super(get_super_private(sb));
	return 0;
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
	lv_conf *conf = vol->conf;
	reiser4_volinfo *vinfo = &vol->volinfo[CUR_VOL_CONF];

	args->u.vol.nr_bricks = meta_brick_belongs_dsa() ?
		vol_nr_origins(vol) : - vol_nr_origins(vol);
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	args->u.vol.stripe_bits = vol->stripe_bits;
	args->u.vol.fs_flags = get_super_private(sb)->fs_flags;
	args->u.vol.nr_mslots = conf->nr_mslots;
	args->u.vol.nr_volinfo_blocks = vinfo->num_volmaps + vinfo->num_voltabs;
	return 0;
}

int print_brick_asym(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	int ret = 0;
	u64 id;        /* internal ID */
	u64 brick_idx; /* index of the brick in logical volume */

	reiser4_volume *vol = super_volume(sb);
	lv_conf *conf = vol->conf;
	reiser4_subvol *subv;

	spin_lock_reiser4_super(get_super_private(sb));

	brick_idx = args->s.brick_idx;
	if (brick_idx >= vol_nr_origins(vol)) {
		ret = -EINVAL;
		goto out;
	}
	/* translate index in LV to brick ID */

	if (is_meta_brick_id(brick_idx))
		id = brick_idx;
	else if (meta_brick_belongs_dsa())
		id = vol->vol_plug->bucket_ops.idx2id(brick_idx);
	else
		id = vol->vol_plug->bucket_ops.idx2id(brick_idx - 1);

	assert("edward-2206", conf->mslots[id] != NULL);

	subv = conf->mslots[id][0];
	strncpy(args->d.name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_room = subv->data_room;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.system_blocks = subv->min_blocks_used;
	args->u.brick.volinfo_addr = subv->volmap_loc[CUR_VOL_CONF];
 out:
	spin_unlock_reiser4_super(get_super_private(sb));
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
 * without bodies). It shouldn't lead to any problems as balancing is
 * always performed _after_ installing a new distribution table (see
 * pre-condition below).
 *
 * NOTE: correctness of this balancing procedure (i.e. a guarantee
 * that all files will be processed) is provided by our single stupid
 * objectid allocator. If you want to add another allocator, then please
 * prove that it makes a friendship with the balancing procedure, or
 * write another one which works for that new allocator correctly.
 *
 * Pre-condition: a new volume configuration is installed.
 *
 * @super: super-block of the volume to be balanced;
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
		if (ret == -ENOENT || ret == -E_NO_NEIGHBOR)
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
		if (!coord_is_existing_item(&coord) ||
		    !keyeq(item_key_by_coord(&coord, &found), &ictx.curr)) {

			zrelse(coord.node);
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
				if (ret == -ENOENT || ret == -E_NO_NEIGHBOR)
					break;
				goto error;
			}
		} else
			zrelse(coord.node);
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

		if (ret == -ENOENT || ret == -E_NO_NEIGHBOR)
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
		 * will find it correctly. Also stat-data we are looking for
		 * can be killed by concurrent unlink(). In this case a "bad
		 * inode" is created.
		 */
		sdkey = ictx.curr;
		set_key_ordering(&sdkey, KEY_ORDERING_MASK /* max ordering */);
		set_key_type(&sdkey, KEY_SD_MINOR);
		set_key_offset(&sdkey, 0);

		inode = reiser4_iget(super, &sdkey, FIND_MAX_NOT_MORE_THAN, 0);

		if (!IS_ERR(inode) && inode_file_plugin(inode)->balance) {
			reiser4_iget_complete(inode);
			/*
			 * migrate data blocks of this file
			 */
			get_exclusive_access(unix_file_inode_data(inode));
			ret = inode_file_plugin(inode)->balance(inode);
			drop_exclusive_access(unix_file_inode_data(inode));

			iput(inode);
			if (ret) {
				warning("edward-1889",
				      "Inode %lli: data migration failed (%d)",
				      (unsigned long long)get_inode_oid(inode),
				      ret);
				goto error;
			}
		}
		if (terminate)
			break;
		ictx.curr = ictx.next;
	}
 done:
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
		.remove_brick_tail = NULL,
		.print_brick = print_brick_simple,
		.print_volume = print_volume_simple,
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
		.remove_brick_tail = remove_brick_tail_asym,
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
			.create_buckets = create_buckets,
			.free_buckets = free_buckets,
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
