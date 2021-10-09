/*
  Copyright (c) 2016-2020 Eduard O. Shishkin

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
 * Implementation of simple and asymmetric logical volumes.
 *
 * Simple Volume can consist of only one device. Operation of adding
 * a brick to such volume will fail. All reiser4 partitions with old
 * "format40" layout are simple volumes.
 *
 * Asymmetric Logical Volume can consist of any number of devices
 * formatted with "format41" layout, called bricks (or storage
 * subvolumes).
 * Mounted asymmetric volume is represented by a table of pointers to
 * bricks. Its first column represents meta-data brick with its
 * optional replicas. Other columns represent data bricks with
 * replicas. Data brick contains only unformatted blocks. Meta-data
 * brick contains blocks of all types. Asymmetric Logical Volume
 * contains at least one meta-data brick and any number of data bricks
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

static int balance_volume_asym(struct super_block *sb, u32 flags);

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

static bucket_t *reset_buckets(bucket_t *buckets)
{
	int i, j;
	reiser4_volume *vol = current_volume();
	lv_conf *conf = vol->conf;

	for (i = 0, j = 0; i < conf->nr_mslots; i++) {
		if (conf->mslots[i] == NULL)
			continue;
		if (!is_dsa_brick(conf_origin(conf, i)))
			continue;
		buckets[j] = conf_origin(conf, i);
		/*
		 * set index in DSA
		 */
		buckets[j]->dsa_idx = j;
		j++;
	}
	assert("edward-2194", j == num_dsa_subvols(vol));
	return buckets;
}

/**
 * Allocate and initialize an array of abstract buckets for an
 * asymmetric volume.
 * The notion of abstract bucket encapsulates an original brick
 * (without replicas). That array should include only DSA members.
 */
static bucket_t *create_buckets(void)
{
	bucket_t *ret;
	u32 nr_buckets = num_dsa_subvols(current_volume());

	ret = kmalloc(nr_buckets * sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return NULL;
	return reset_buckets(ret);
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
	check_buckets(vec, numb);

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
		check_buckets(new, numb - 1);
	}
	return new;
}

static bucket_t *insert_bucket(bucket_t *vec, bucket_t this, u32 numb, u32 pos)
{
	bucket_t *new;

	assert("edward-2339", pos <= numb);
	check_buckets(vec, numb);

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
		check_buckets(new, numb + 1);
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
static int load_volume_dconf(reiser4_subvol *subv)
{
	int id = CUR_VOL_CONF;
	int ret;
	int i, j;
	u64 packed_segments = 0;
	reiser4_volume *vol = super_volume(subv->super);
	reiser4_volinfo *vinfo = &vol->volinfo[id];
	distribution_plugin *dist_plug = vol->dist_plug;
	reiser4_block_nr volmap_loc = subv->volmap_loc[id];
	u64 voltabs_needed;

	assert("edward-1984", subv->id == METADATA_SUBVOL_ID);
	assert("edward-2175", subv->volmap_loc[id] != 0);

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
static int release_volume_dconf(reiser4_volume *vol, int id)
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
static int update_volume_dconf(reiser4_volume *vol)
{
	int ret;
	reiser4_subvol *mtd_subv = get_meta_subvol();

	ret = release_volume_dconf(vol, CUR_VOL_CONF);
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
static int create_volume_dconf(reiser4_volume *vol, int id)
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

static int capture_volume_dconf(reiser4_volume *vol, int id)
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
static int make_volume_dconf(reiser4_volume *vol)
{
	int ret;

	ret = create_volume_dconf(vol, NEW_VOL_CONF);
	if (ret)
		return ret;
	ret = capture_volume_dconf(vol, NEW_VOL_CONF);
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

	return load_volume_dconf(subv);
}

static u64 get_pos_in_vol(reiser4_volume *vol, reiser4_subvol *subv);
static int __remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim);
/*
 * Init volume system info, which has been already loaded
 * diring disk formats inialization of subvolumes (components).
 */
static int init_volume_asym(struct super_block *sb, reiser4_volume *vol)
{
	int ret;
	u32 subv_id;
	u32 nr_victims = 0;
	lv_conf *cur_conf = vol->conf;

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
	check_buckets(vol->buckets, num_dsa_subvols(current_volume()));

	if (reiser4_is_set(sb, REISER4_PROXY_ENABLED)) {
		/*
		 * set proxy subvolume
		 */
		for_each_mslot(cur_conf, subv_id) {
			reiser4_subvol *subv;

			if (!conf_mslot_at(cur_conf, subv_id))
				continue;
			subv = conf_origin(cur_conf, subv_id);
			if (subvol_is_set(subv, SUBVOL_IS_PROXY)) {
				vol->proxy = subv;
				break;
			}
		}
		assert("edward-2445", vol->proxy != NULL);
		/*
		 * start a proxy flushing kernel thread here
		 */
	}
	if (!reiser4_volume_has_incomplete_removal(sb)) {
		if (reiser4_volume_is_unbalanced(sb))
			warning("", "Volume (%s) is unbalanced", sb->s_id);
		return 0;
	}
	assert("edward-2250", current_volume() == vol);
	/*
	 * prepare the volume for removal completion
	 */
	assert("edward-2244", vol->new_conf == NULL);

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
		warning("edward-2246",
			"Too many bricks (%u) scheduled for removal",
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
	ret = __remove_data_brick(vol, vol->victim);
	if (ret)
		return ret;
	assert("edward-2432", vol->new_conf != NULL);
	assert("edward-2458", vol->new_conf->tab == NULL);

	if (!subvol_is_set(vol->victim, SUBVOL_IS_PROXY)) {
		bucket_t *new_vec;
		new_vec = remove_bucket(vol->buckets, num_dsa_subvols(vol),
					get_pos_in_dsa(vol->victim));
		if (!new_vec) {
			free_lv_conf(vol->new_conf);
			vol->new_conf = NULL;
			return -ENOMEM;
		}
		free_buckets(vol->buckets);
		vol->buckets = new_vec;
	} else
		/*
		 * Removing proxy brick was incomplete.
		 * Disable IO requests against him.
		 */
		reiser4_volume_clear_proxy_io(reiser4_get_current_sb());
	/*
	 * borrow distribution table from the existing config (which
	 * includes the old set of slots and the new distribution table)
	 */
	vol->new_conf->tab = vol->conf->tab;
	/*
	 * Now announce incomplete removal.
	 * Volume configuration will be updated by remove_brick_tail_asym()
	 * called by reiser4_finish_removal().
	 * FIXME: don't ask user to finish removel.
	 * Call reiser4_finish_removal() right here instead.
	 */
 out:
	warning("", "Please, complete brick %s removal on volume %s",
		vol->victim ? vol->victim->name : "Null",
		sb->s_id);
	return 0;
}

/**
 * Bucket operations.
 */

static const char *bucket_type_asym(void)
{
	return "Brick";
}

static char *bucket_name_asym(bucket_t this)
{
	return this->name;
}

static u64 cap_of(bucket_t this)
{
	return this->data_capacity;
}

static u64 cap_at_asym(bucket_t *buckets, u64 idx)
{
	return cap_of(buckets[idx]);
}

static u64 capr_at_asym(bucket_t *buckets, u64 idx)
{
	u64 ret = cap_of(buckets[idx]);
	ret -= (ret * 5)/100; /* deduct 5% reservation */
	return ret;
}

static void *apx_of_asym(bucket_t bucket)
{
	return bucket->apx;
}

static void *apx_at_asym(bucket_t *buckets, u64 index)
{
	return apx_of_asym(buckets[index]);
}

static void apx_set_at_asym(bucket_t *buckets, u64 idx, void *apx)
{
	buckets[idx]->apx = apx;
}

static u64 *apx_lenp_at_asym(bucket_t *buckets, u64 idx)
{
	return &buckets[idx]->apx_len;
}

static reiser4_subvol *origin_at(slot_t slot)
{
	return ((mirror_t *)slot)[0];
}

/**
 * Return number of busy data blocks, which are a subject
 * for distribution.
 * @slot represents data brick! This function can not be
 * applied to meta-data brick.
 */
static u64 data_blocks_occupied(bucket_t this)
{
	/*
	 * From the total block count on a device we need
	 * to subtract number of system blocks (from disk
	 * format specifications), which are always busy
	 * and are not a subject for distribution
	 */
	return this->block_count -
		this->min_blocks_used -
		this->blocks_free;
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

/**
 * current consumption of brick capacity
 */
static u64 cap_consump_brick_asym(bucket_t this)
{
	if (is_meta_brick(this)) {
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

		return div64_u64(cap_of(this) *
				 cap_consump_brick_asym(origin_at(neighbor)),
				 cap_of(origin_at(neighbor)));
	} else
		/* data brick */
		return data_blocks_occupied(this);
}

/**
 * Return ordered number of brick @subv in the array of all original
 * bricks of the logical volume
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

/**
 * Returns true, if volume @vol includes @brick in its configuration.
 * Pre-condition: the volume is read, or write locked
 */
int brick_belongs_volume(reiser4_volume *vol, reiser4_subvol *subv)
{
	return get_pos_in_vol(vol, subv) < vol_nr_origins(vol);
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

static int resize_brick(reiser4_volume *vol, reiser4_subvol *this,
			long long delta, int *need_balance)
{
	int ret;
	int(*dst_resize_fn)(reiser4_dcx *, const void *, u64, bucket_t);

	assert("edward-2393", delta != 0);

	this->data_capacity += delta;

	*need_balance = 1;
	if (num_dsa_subvols(vol) == 1 ||
	    (is_meta_brick(this) && !is_dsa_brick(this))) {
		*need_balance = 0;
		return 0;
	}
	if (delta > 0)
		dst_resize_fn = vol->dist_plug->v.inc;
	else
		dst_resize_fn = vol->dist_plug->v.dec;

	ret = dst_resize_fn(&vol->dcx, vol->conf->tab,
			    get_pos_in_dsa(this), NULL);
	if (ret)
		goto error;

	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	return 0;
 error:
	this->data_capacity -= delta;
	return ret;
}

static int __add_meta_brick(reiser4_volume *vol, reiser4_subvol *new)
{
	assert("edward-2433", is_meta_brick(new));
	/*
	 * Clone in-memory volume config
	 */
	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL)
		return RETERR(-ENOMEM);
	return 0;
}

static int add_meta_brick(reiser4_volume *vol, reiser4_subvol *new,
			  bucket_t **old_vec)
{
	int ret;
	bucket_t *new_vec;

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
		return RETERR(-ENOMEM);
	*old_vec = vol->buckets;
	vol->buckets = new_vec;
	/*
	 * Create new distribution table.
	 */
	ret = vol->dist_plug->v.inc(&vol->dcx, vol->conf->tab,
				    METADATA_SUBVOL_ID /* pos */, new);
	if (ret)
		goto error;
	/*
	 * finally, clone in-memory volume config
	 */
	ret =__add_meta_brick(vol, new);
	if (ret)
		goto error;
	new->flags |= (1 << SUBVOL_HAS_DATA_ROOM);
	return 0;
 error:
	vol->buckets = reset_buckets(*old_vec);
	free_buckets(new_vec);
	return ret;
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

/**
 * Create new in-memory volume config
 */
int __add_data_brick(reiser4_volume *vol, reiser4_subvol *this, u64 pos_in_vol)
{
	u64 old_nr_mslots = vol->conf->nr_mslots;
	slot_t new_slot;
	/*
	 * Assign internal ID for the new brick
	 */
	this->id = pos_in_vol;
	/*
	 * Create new in-memory volume config
	 */
	new_slot = alloc_mslot(1 + this->num_replicas);
	if (!new_slot)
		return RETERR(-ENOMEM);

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
		return RETERR(-ENOMEM);
	}
	memcpy(vol->new_conf->mslots, vol->conf->mslots,
	       sizeof(slot_t) * old_nr_mslots);
	vol->new_conf->mslots[pos_in_vol] = new_slot;

	atomic_inc(&vol->nr_origins);
	return 0;
}

/**
 * Find a respective position in DSA by mslot index
 */
u64 pos_in_dsa_by_mslot(u64 mslot_idx)
{
	u32 i,j;
	reiser4_volume *vol = current_volume();

	for (i = 0, j = 0; i < mslot_idx; i++) {
		if (!vol->conf->mslots[i])
			continue;
		if (!is_dsa_brick(conf_origin(vol->conf, i)))
			continue;
		j++;
	}
	return j;
}

int add_data_brick(reiser4_volume *vol, reiser4_subvol *this,
		   bucket_t **old_vec)
{
	int ret;
	u64 free_mslot_idx;
	u64 pos_in_dsa;
	bucket_t *new_vec;

	assert("edward-1929", !is_meta_brick(this));

	free_mslot_idx = find_first_empty_slot_off();
	pos_in_dsa = pos_in_dsa_by_mslot(free_mslot_idx);
	/*
	 * insert @this to the set of abstract buckets
	 */
	new_vec = insert_bucket(vol->buckets, this,
				num_dsa_subvols(vol), pos_in_dsa);
	if (!new_vec)
		return -ENOMEM;
	*old_vec = vol->buckets;
	vol->buckets = new_vec;

	/*
	 * create new in-memory volume config
	 */
	ret = __add_data_brick(vol, this, free_mslot_idx);
	if (ret)
		goto error;
	/*
	 * finally, create new distribution table
	 */
	return vol->dist_plug->v.inc(&vol->dcx, vol->conf->tab,
				     pos_in_dsa, this);
 error:
	vol->buckets = reset_buckets(*old_vec);
	free_buckets(new_vec);
	return ret;
}

static int resize_brick_asym(reiser4_volume *vol, reiser4_subvol *this,
			     long long delta, int *need_balance)
{
	int ret;
	struct super_block *sb = reiser4_get_current_sb();
	reiser4_dcx *rdcx = &vol->dcx;
	distribution_plugin *dist_plug = vol->dist_plug;
	lv_conf *old_conf = vol->conf;

	assert("edward-1824", vol != NULL);
	assert("edward-1825", dist_plug != NULL);

	if (is_proxy_brick(this)) {
		warning("edward-2447",
			"Can't resize proxy brick %s", this->name);
		return RETERR(-EINVAL);
	}
	ret = dist_plug->v.init(&vol->conf->tab,
				num_dsa_subvols(vol),
				vol->num_sgs_bits, rdcx);
	if (ret)
		return ret;

	ret = resize_brick(vol, this, delta, need_balance);
	dist_plug->v.done(rdcx);
	if (ret)
		return ret;
	if (!(*need_balance)) {
		ret = capture_brick_super(this);
		if (ret)
			goto error;
		printk("reiser4 (%s): Changed data capacity of brick %s.\n",
		       sb->s_id, this->name);
		return 0;
	}
	assert("edward-2394", vol->new_conf != NULL);

	ret = make_volume_dconf(vol);
	if (ret)
		goto error;
	ret = update_volume_dconf(vol);
	if (ret)
		goto error;
	dist_plug->r.replace(&vol->dcx, &vol->new_conf->tab);
	/*
	 * write unbalanced status and new data capacity to disk
	 */
	reiser4_volume_set_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	ret = capture_brick_super(this);
	if (ret)
		goto error;
	ret = force_commit_current_atom();
	if (ret)
		goto error;
	/*
	 * publish the new config
	 */
	rcu_assign_pointer(vol->conf, vol->new_conf);
	synchronize_rcu();
	free_lv_conf(old_conf);
	vol->new_conf = NULL;

	printk("reiser4 (%s): Changed data capacity of brick %s.\n",
	       sb->s_id, this->name);
	return 0;
 error:
	/*
	 * resize failed - it should be repeated in regular context
	 */
	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

static int add_proxy_asym(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;
	lv_conf *old_conf = vol->conf;
	struct super_block *sb = reiser4_get_current_sb();

	if (is_meta_brick(new) && (vol_nr_origins(vol) == 1)) {
		warning("edward-2434",
			"Single meta-data brick can not be proxy");
		return -EINVAL;
	}
	if (new == get_meta_subvol())
		ret = __add_meta_brick(vol, new);
	else
		ret = __add_data_brick(vol, new, find_first_empty_slot_off());
	if (ret)
		return ret;
	assert("edward-2436", vol->new_conf != NULL);
	assert("edward-2459", vol->new_conf->tab == NULL);

	if (new != get_meta_subvol()) {
		/*
		 * add a record about @new to the meta-data brick
		 */
		ret = reiser4_grab_space(estimate_one_insert_into_item(
						       meta_subvol_tree()),
					 BA_CAN_COMMIT, get_meta_subvol());
		if (ret)
			goto error;
		ret = brick_symbol_add(new);
		if (ret)
			goto error;
	}
	reiser4_volume_set_proxy_enabled(sb);
	reiser4_volume_set_proxy_io(sb);
	clear_bit(SUBVOL_HAS_DATA_ROOM, &new->flags);

	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	ret = capture_brick_super(new);
	if (ret)
		goto error;
	/*
	 * borrow distribution table from the old config
	 */
	vol->new_conf->tab = old_conf->tab;
	old_conf->tab = NULL;
	/*
	 * publish the new config
	 */
	rcu_assign_pointer(vol->conf, vol->new_conf);
	synchronize_rcu();
	free_lv_conf(old_conf);
	vol->new_conf = NULL;
	vol->proxy = new;
	/*
	 * after publishing the new config (not before!)
	 * write superblocks of meta-data brick and the proxy brick respectively
	 */
	force_commit_current_atom();

	/* FIXME: start a proxy flushing kernel thread here */

	printk("reiser4 (%s): Proxy brick %s has been added.",
	       sb->s_id, new->name);
	return 0;
 error:
	/* adding a proxy should be repeated in regular context */

	clear_bit(SUBVOL_IS_PROXY, &new->flags);
	if (!is_meta_brick(new))
		new->flags |= (1 << SUBVOL_HAS_DATA_ROOM);
	reiser4_volume_clear_proxy_enabled(sb);
	reiser4_volume_clear_proxy_io(sb);

	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

/**
 * Add a @new brick to asymmetric logical volume @vol
 */
static int add_brick_asym(reiser4_volume *vol, reiser4_subvol *new)
{
	int ret;
	distribution_plugin *dist_plug = vol->dist_plug;
	lv_conf *old_conf = vol->conf;
	struct super_block *sb = reiser4_get_current_sb();

	bucket_t *old_vec;
	bucket_t *new_vec;

	assert("edward-1931", dist_plug != NULL);
	assert("edward-2262", vol->conf != NULL);
	assert("edward-2239", vol->new_conf == NULL);

	if (new->data_capacity == 0) {
		warning("edward-1962", "Can't add brick of zero capacity");
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
	if (brick_belongs_volume(vol, new) && is_dsa_brick(new)) {
		/*
		 * brick already participate in regular data distribution
		 */
		warning("edward-1963", "Can't add brick to DSA twice");
		return -EINVAL;
	}
	if (subvol_is_set(new, SUBVOL_IS_PROXY))
		return add_proxy_asym(vol, new);

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
		ret = add_meta_brick(vol, new, &old_vec);
	else
		ret = add_data_brick(vol, new, &old_vec);

	dist_plug->v.done(&vol->dcx);
	if (ret)
		return ret;
	assert("edward-2240", vol->new_conf != NULL);
	assert("edward-2462", vol->new_conf->tab == NULL);

	ret = make_volume_dconf(vol);
	if (ret)
		goto error;
	ret = update_volume_dconf(vol);
	if (ret)
		goto error;
	if (new != get_meta_subvol()) {
		/* add a record about @new to the volume */
		ret = brick_symbol_add(new);
		if (ret)
			goto error;
	}
	dist_plug->r.replace(&vol->dcx, &vol->new_conf->tab);

	reiser4_volume_set_unbalanced(reiser4_get_current_sb());
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	ret = capture_brick_super(new);
	if (ret)
		goto error;
	/*
	 * Now publish the new config
	 */
	rcu_assign_pointer(vol->conf, vol->new_conf);
	synchronize_rcu();
	free_lv_conf(old_conf);
	vol->new_conf = NULL;
	free_buckets(old_vec);
	/*
	 * after publishing the new config (not before!)
	 * write superblocks of meta-data brick and the proxy brick respectively
	 */
	force_commit_current_atom();

	printk("reiser4 (%s): Brick %s has been added.", sb->s_id, new->name);
	return 0;
 error:
	/*
	 * adding a brick should be repeated in regular context
	 */
	if (is_meta_brick(new))
		clear_bit(SUBVOL_HAS_DATA_ROOM, &new->flags);
	reiser4_volume_clear_unbalanced(reiser4_get_current_sb());

	new_vec = vol->buckets;
	vol->buckets = reset_buckets(old_vec);
	free_buckets(new_vec);

	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

static u64 space_free_at_asym(bucket_t bucket)
{
	u64 ret;
	u64 reserved;

	reserved = (5 * bucket->block_count)/100; /* 5% */
	ret = bucket->blocks_free;
	return ret < reserved ? 0: ret - reserved;
}

static u64 cap_consump_asym(void)
{
	u64 ret = 0;
	u64 subv_id;
	lv_conf *conf = current_lv_conf();

	txnmgr_force_commit_all(reiser4_get_current_sb(), 0);

	for_each_mslot(conf, subv_id) {
		if (!conf->mslots[subv_id] ||
		    !is_dsa_brick(conf_origin(conf, subv_id)))
			continue;
		ret += cap_consump_brick_asym(origin_at(conf->mslots[subv_id]));
	}
	return ret;
}

/**
 * Check, if remaining bricks are able to accommodate all the data of the
 * brick to be removed
 */
static int check_remove(reiser4_volume *vol, bucket_t *new_vec,
			reiser4_subvol *victim)
{
	u64 free, min_req;

	assert("edward-2518", num_dsa_subvols(vol) > 1);

	if (num_dsa_subvols(vol) > 2)
		/* estimation will be made with distribution plugin */
		return 0;

	free = space_free_at_asym(new_vec[0]);
	min_req = cap_consump_brick_asym(victim);

	if (free < min_req){
		warning("edward-2517",
		 "Not enough free space (%llu) on brick %s, min required %llu",
			free, new_vec[0]->name, min_req);
		return -ENOSPC;
	}
	return 0;
}

static int __remove_meta_brick(reiser4_volume *vol)
{
	/*
	 * Clone in-memory volume config
	 */
	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL)
		return RETERR(-ENOMEM);
	return 0;
}

static int remove_meta_brick(reiser4_volume *vol, bucket_t **old_vec)
{
	int ret;
	reiser4_subvol *mtd_subv = get_meta_subvol();
	distribution_plugin *dist_plug = vol->dist_plug;
	bucket_t *new_vec;

	assert("edward-1844", num_dsa_subvols(vol) > 1);

	if (!is_dsa_brick(mtd_subv)) {
		warning("edward-2331",
			"Metadata brick doesn't belong to DSA. Can't remove.");
		return RETERR(-EINVAL);
	}
	/*
	 * remove meta-data brick from the set of abstract buckets
	 */
	new_vec = remove_bucket(vol->buckets, num_dsa_subvols(vol),
				METADATA_SUBVOL_ID /* position in DSA */);
	if (!new_vec)
		return RETERR(-ENOMEM);

	*old_vec = vol->buckets;
	vol->buckets = new_vec;

	ret = check_remove(vol, new_vec, mtd_subv);
	if (ret)
		goto error;
	ret = dist_plug->v.dec(&vol->dcx, vol->conf->tab,
			       METADATA_SUBVOL_ID, mtd_subv);
	if (ret)
		goto error;

	ret = __remove_meta_brick(vol);
	if (ret)
		goto error;

	clear_bit(SUBVOL_HAS_DATA_ROOM, &mtd_subv->flags);
	assert("edward-1827", !is_dsa_brick(mtd_subv));
	return 0;
 error:
	vol->buckets = reset_buckets(*old_vec);
	free_buckets(new_vec);
	return ret;
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

static int __remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim)
{
	lv_conf *old = vol->conf;
	u64 old_num_subvols = vol_nr_origins(vol);
	u64 pos_in_vol;
	u32 new_nr_mslots;

	assert("edward-2253", vol->new_conf == NULL);

	pos_in_vol = get_pos_in_vol(vol, victim);
	assert("edward-2199", pos_in_vol < old_num_subvols);

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
	return 0;
}

static int remove_data_brick(reiser4_volume *vol, reiser4_subvol *victim,
			     bucket_t **old_vec)
{
	int ret;
	u32 pos_in_dsa;
	bucket_t *new_vec;

	ret = __remove_data_brick(vol, victim);
	if (ret)
		return ret;

	pos_in_dsa = get_pos_in_dsa(victim);

	new_vec = remove_bucket(vol->buckets, num_dsa_subvols(vol), pos_in_dsa);
	if (!new_vec)
		return -ENOMEM;

	*old_vec = vol->buckets;
	vol->buckets = new_vec;

	ret = check_remove(vol, new_vec, victim);
	if (ret)
		goto error;
	ret = vol->dist_plug->v.dec(&vol->dcx, vol->conf->tab,
				    pos_in_dsa, victim);
	if (ret)
		goto error;
	victim->flags |= (1 << SUBVOL_TO_BE_REMOVED);
	return 0;
 error:
	/*
	 * release resources allocated by and
	 * roll back changes made by __remove_data_brick()
	 */
	vol->buckets = reset_buckets(*old_vec);
	free_buckets(new_vec);

	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

static int remove_proxy_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	struct super_block *sb = reiser4_get_current_sb();

	/*
	 * Prepare a new volume config with different set of bricks,
	 * not including the proxy brick, and the same distribution
	 * table
	 */
	if (is_meta_brick(victim))
		ret = __remove_meta_brick(vol);
	else
		ret = __remove_data_brick(vol, victim);
	if (ret)
		return ret;
	assert("edward-2437", vol->new_conf != NULL);
	assert("edward-2460", vol->new_conf->tab == NULL);
	/*
	 * borrow distribution table from the old config
	 */
	vol->new_conf->tab = vol->conf->tab;
	/*
	 * Disable IO requests against the proxy brick to be removed
	 */
	reiser4_volume_clear_proxy_io(sb);

	if (!is_meta_brick(victim))
		capture_brick_super(victim);
	/*
	 * set unbalanced status and put format super-block
	 * of meta-data brick to the transaction
	 */
	reiser4_volume_set_unbalanced(sb);
	reiser4_volume_set_incomplete_removal(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	/*
	 * write unbalanced and incomplete removal status to disk
	 */
	ret = force_commit_current_atom();
	if (ret)
		goto error;
	/*
	 * the volume will be balanced with the old distribution table -
	 * it will move all data from the proxy brick to other bricks
	 * of the volume
	 */
	return 0;
 error:
	/*
	 * proxy removal should be repeated in regular context
	 */
	reiser4_volume_clear_unbalanced(sb);
	reiser4_volume_clear_incomplete_removal(sb);

	reiser4_volume_set_proxy_enabled(sb);
	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

static int remove_brick_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	lv_conf *tmp_conf;
	lv_conf *old_conf = vol->conf;
	distribution_plugin *dist_plug = vol->dist_plug;
	struct super_block *sb = reiser4_get_current_sb();
	u32 old_nr_dsa_bricks = num_dsa_subvols(vol);
	bucket_t *old_vec;
	bucket_t *new_vec;

	assert("edward-1830", vol != NULL);
	assert("edward-1846", dist_plug != NULL);

	vol->victim = victim;

	if (subvol_is_set(victim, SUBVOL_IS_PROXY))
		return remove_proxy_asym(vol, victim);

	if (old_nr_dsa_bricks == 1) {
		warning("edward-1941",
			"Can't remove the single brick from DSA");
		return RETERR(-EINVAL);
	}
	ret = dist_plug->v.init(&vol->conf->tab,
				old_nr_dsa_bricks, vol->num_sgs_bits,
				&vol->dcx);
	if (ret)
		return ret;

	if (is_meta_brick(victim))
		ret = remove_meta_brick(vol, &old_vec);
	else
		ret = remove_data_brick(vol, victim, &old_vec);
	dist_plug->v.done(&vol->dcx);
	if (ret)
		return ret;
	assert("edward-2242", vol->new_conf != NULL);
	assert("edward-2461", vol->new_conf->tab == NULL);

	ret = make_volume_dconf(vol);
	if (ret)
		goto error;
	ret = update_volume_dconf(vol);
	if (ret)
		goto error;
	dist_plug->r.replace(&vol->dcx, &vol->new_conf->tab);
	/*
	 * Prepare a temporal config for balancing. This config has
	 * the same set of bricks, but updated distribution table
	 */
	tmp_conf = clone_lv_conf(old_conf);
	if (!tmp_conf) {
		ret = RETERR(-ENOMEM);
		goto error;
	}
	/* borrow distribution table from the new config */
	tmp_conf->tab = vol->new_conf->tab;

	if (!is_meta_brick(victim))
		capture_brick_super(victim);
	/*
	 * set unbalanced status and put format super-block
	 * of meta-data brick to the transaction
	 */
	reiser4_volume_set_unbalanced(sb);
	reiser4_volume_set_incomplete_removal(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret) {
		tmp_conf->tab = NULL;
		free_lv_conf(tmp_conf);
		goto error;
	}
	/*
	 * write unbalanced and incomplete removal status to disk
	 */
	ret = force_commit_current_atom();
	if (ret)
		goto error;
	/*
	 * New configuration is written to disk.
	 * From now on the brick removal operation can not be rolled
	 * back on error paths. Instead, it should be completed in a
	 * context of a special completion operation
	 */
	/*
	 * Publish the temporal config
	 */
	rcu_assign_pointer(vol->conf, tmp_conf);
	synchronize_rcu();
	free_lv_conf(old_conf);
	free_buckets(old_vec);
	/*
	 * From now on the file system doesn't allocate disk
	 * addresses on the brick to be removed
	 */
	printk("reiser4 (%s): Brick %s scheduled for removal.\n",
	       sb->s_id, victim->name);
	return 0;
 error:
	/*
	 * brick removal should be repeated in regular context
	 */
	reiser4_volume_clear_unbalanced(sb);
	reiser4_volume_clear_incomplete_removal(sb);
	if (is_meta_brick(victim))
		victim->flags |= (1 << SUBVOL_HAS_DATA_ROOM);

	new_vec = vol->buckets;
	vol->buckets = reset_buckets(old_vec);
	free_buckets(new_vec);

	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
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
 * Pre-condition: all data have been moved out of the brick to be removed
 * by the balancing procedure, and unbalanced status has been successfully
 * cleared up on disk
 */
int remove_brick_tail_asym(reiser4_volume *vol, reiser4_subvol *victim)
{
	int ret;
	int is_proxy = 0;
	lv_conf *cur_conf = vol->conf;

	if (!is_meta_brick(victim))
		clear_bit(SUBVOL_TO_BE_REMOVED, &victim->flags);

	if (subvol_is_set(victim, SUBVOL_IS_PROXY)) {
		is_proxy = 1;
		assert("edward-2448",
		       !subvol_is_set(victim, SUBVOL_HAS_DATA_ROOM));

		reiser4_volume_clear_proxy_enabled(reiser4_get_current_sb());
		clear_bit(SUBVOL_IS_PROXY, &victim->flags);
		if (!is_meta_brick(victim))
			victim->flags |= (1 << SUBVOL_HAS_DATA_ROOM);
	}
	ret = capture_brick_super(victim);
	if (ret)
		goto error;
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
			ret = RETERR(-EAGAIN);
			goto error;
		}
		/*
		 * remove a record about @victim from the volume
		 * and decrement number of bricks in the same
		 * transaction
		 */
		ret = reserve_brick_symbol_del();
		if (ret)
			goto error;
		ret = brick_symbol_del(victim);
		reiser4_release_reserved(reiser4_get_current_sb());
		if (ret)
			goto error;
		atomic_dec(&vol->nr_origins);
	}
	/*
	 * From now on we can not fail. Moreover, remove_brick_tail()
	 * must not be called for this brick once again.
	 */
	vol->victim = NULL;
	/*
	 * Publish final config with updated set of slots,
	 * which doesn't contain @victim
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
	if (is_proxy)
		vol->proxy = NULL;

	printk("reiser4 (%s): %s %s has been removed.\n",
	       victim->super->s_id, is_proxy ? "Proxy" : "Brick",
	       victim->name);
	return 0;
 error:
	/*
	 * brick removal should be completed in the context of
	 * a special removal completion operation
	 */
	victim->flags |= (1 << SUBVOL_TO_BE_REMOVED);
	if (is_proxy) {
		set_bit(SUBVOL_IS_PROXY, &victim->flags);
		reiser4_volume_set_proxy_enabled(reiser4_get_current_sb());
		if (!is_meta_brick(victim))
			clear_bit(SUBVOL_HAS_DATA_ROOM, &victim->flags);
	}
	return ret;
}

static int init_volume_simple(struct super_block *sb, reiser4_volume *vol)
{
	if (!REISER4_PLANA_KEY_ALLOCATION) {
		warning("edward-2376",
			"Simple volume requires Plan-A key allocation scheme");
		return RETERR(-EINVAL);
	}
	return 0;
}

static u64 meta_subvol_id_simple(void)
{
	return METADATA_SUBVOL_ID;
}

static u64 calc_brick_simple(lv_conf *conf, const struct inode *inode,
			     loff_t offset)
{
	return METADATA_SUBVOL_ID;
}

static int remove_brick_simple(reiser4_volume *vol, reiser4_subvol *this)
{
	warning("", "remove_brick operation is undefined for simple volumes");
	return -EINVAL;
}

static int resize_brick_simple(reiser4_volume *vol, reiser4_subvol *this,
			       long long delta, int *need_balance)
{
	warning("", "resize operation is undefined for simple volumes");
	return -EINVAL;
}

static int add_brick_simple(reiser4_volume *vol, reiser4_subvol *new)
{
	warning("", "add_brick operation is undefined for simple volumes");
	return -EINVAL;
}

static int balance_volume_simple(struct super_block *sb, u32 flags)
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

static u64 calc_brick_asym(lv_conf *conf, const struct inode *inode,
			   loff_t offset)
{
	assert("edward-2267", conf != NULL);

	if (!conf->tab) {
		/*
		 * DSA includes only one brick. It is either meta-data
		 * brick, or one of the next two bricks at the right
		 */
		assert("edward-2474", num_dsa_subvols(current_volume()) == 1);
		return meta_brick_belongs_dsa() ? METADATA_SUBVOL_ID :
			is_dsa_brick(conf_origin(conf,
						 METADATA_SUBVOL_ID + 1)) ?
			METADATA_SUBVOL_ID + 1 : METADATA_SUBVOL_ID + 2;
	} else {
		u64 stripe_idx;
		reiser4_volume *vol = current_volume();
		distribution_plugin *dist_plug = current_dist_plug();

		if (vol->stripe_bits) {
			stripe_idx = offset >> vol->stripe_bits;
			put_unaligned(cpu_to_le64(stripe_idx), &stripe_idx);
		} else
			stripe_idx = 0;

		return dist_plug->r.lookup(&vol->dcx, inode,
					   (const char *)&stripe_idx,
					   sizeof(stripe_idx),
					   get_seed(get_inode_oid(inode), vol),
					   conf->tab);
	}
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

u64 find_brick_simple(const coord_t *coord)
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
	args->u.brick.subv_flags = subv->flags;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_capacity = subv->data_capacity;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.system_blocks = subv->min_blocks_used;
	args->u.brick.volinfo_addr = 0;

	spin_unlock_reiser4_super(get_super_private(sb));
	return 0;
}

u64 find_brick_asym(const coord_t *coord)
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

/**
 * Convert ordered number @idx of brick in the logical volume
 * to its internal id
 */
static u32 brick_idx_to_id(reiser4_volume *vol, u32 idx)
{
	u32 i, j;
	/*
	 * return idx-th non-zero slot
	 */
	for (i = 0, j = 0; i < vol->conf->nr_mslots; i++) {
		if (vol->conf->mslots[i]) {
			if (j == idx)
				return i;
			else
				j ++;
		}
	}
	BUG_ON(1);
}

int print_volume_asym(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	reiser4_volume *vol = super_volume(sb);
	lv_conf *conf = vol->conf;
	reiser4_volinfo *vinfo = &vol->volinfo[CUR_VOL_CONF];

	args->u.vol.nr_bricks = vol_nr_origins(vol);
	args->u.vol.bricks_in_dsa = num_dsa_subvols(vol);
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	args->u.vol.stripe_bits = vol->stripe_bits;
	args->u.vol.nr_sgs_bits = vol->num_sgs_bits;
	args->u.vol.fs_flags = get_super_private(sb)->fs_flags;
	args->u.vol.nr_mslots = conf->nr_mslots;
	args->u.vol.nr_volinfo_blocks = vinfo->num_volmaps + vinfo->num_voltabs;
	return 0;
}

int print_brick_asym(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	int ret = 0;
	u32 id; /* internal ID */
	u64 brick_idx; /* ordered number of the brick in the logical volume */

	reiser4_volume *vol = super_volume(sb);
	lv_conf *conf = vol->conf;
	reiser4_subvol *subv;

	spin_lock_reiser4_super(get_super_private(sb));

	brick_idx = args->s.brick_idx;
	if (brick_idx >= vol_nr_origins(vol)) {
		ret = -EINVAL;
		goto out;
	}
	id = brick_idx_to_id(vol, brick_idx);
	assert("edward-2446", conf->mslots[id] != NULL);

	subv = conf->mslots[id][0];
	strncpy(args->d.name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.subv_flags = subv->flags;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_capacity = subv->data_capacity;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.system_blocks = subv->min_blocks_used;
	args->u.brick.volinfo_addr = subv->volmap_loc[CUR_VOL_CONF];
 out:
	spin_unlock_reiser4_super(get_super_private(sb));
	return ret;
}

static int scale_volume_asym(struct super_block *sb, unsigned factor_bits)
{
	int ret;
	reiser4_volume *vol = super_volume(sb);
	lv_conf *old_conf = vol->conf;
	distribution_plugin *dist_plug = vol->dist_plug;

	ret = dist_plug->v.init(&vol->conf->tab, num_dsa_subvols(vol),
				vol->num_sgs_bits, &vol->dcx);
	if (ret)
		return ret;
	ret = dist_plug->v.spl(&vol->dcx, vol->conf->tab, factor_bits);
	dist_plug->v.done(&vol->dcx);
	if (ret)
		return ret;
	vol->num_sgs_bits += factor_bits;

	vol->new_conf = clone_lv_conf(vol->conf);
	if (vol->new_conf == NULL) {
		ret = RETERR(-ENOMEM);
		goto error;
	}
	ret = make_volume_dconf(vol);
	if (ret)
		goto error;
	ret = update_volume_dconf(vol);
	if (ret)
		goto error;

	dist_plug->r.replace(&vol->dcx, &vol->new_conf->tab);

	reiser4_volume_set_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	/*
	 * write unbalanced status to disk
	 */
	ret = force_commit_current_atom();
	if (ret)
		goto error;
	/*
	 * Now publish the new config
	 */
	rcu_assign_pointer(vol->conf, vol->new_conf);
	synchronize_rcu();
	free_lv_conf(old_conf);
	vol->new_conf = NULL;
	return 0;
 error:
	/*
	 * the scale operation should be repeated in regular context
	 */
	vol->num_sgs_bits -= factor_bits;
	free_lv_conf(vol->new_conf);
	vol->new_conf = NULL;
	return ret;
}

static struct migration_context *alloc_migration_context(void)
{
	return reiser4_vmalloc(MIGRATION_CONTEXT_SIZE);
}

static void free_migration_context(struct migration_context *mctx)
{
	vfree(mctx);
}

void reset_migration_context(struct migration_context *mctx)
{
	memset(mctx, 0, MIGRATION_CONTEXT_SIZE);
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
 * Migrate all data blocks of a regular file in asymmetric logical volume
 */
static int migrate_file_asym(struct inode *inode, u64 dst_idx)
{
	reiser4_volume *vol = super_volume(inode->i_sb);
	struct migration_context *mctx;
	u64 to_write = 0;
	u64 dst_id;
	int ret;

	if (inode_file_plugin(inode)->migrate == NULL)
		return 0;
	if (dst_idx >= vol_nr_origins(vol))
		return RETERR(-EINVAL);
	mctx = alloc_migration_context();
	if (!mctx)
		return RETERR(-ENOMEM);

	dst_id = brick_idx_to_id(vol, dst_idx);

	ret = inode_file_plugin(inode)->migrate(inode, mctx,
						&to_write, &dst_id);

	free_migration_context(mctx);
	if (to_write)
		force_commit_current_atom();
	return ret;
}


static inline int file_is_migratable(struct inode *inode,
				     struct super_block *sb, u32 flags)
{
	if (inode_file_plugin(inode)->migrate == NULL)
		return 0;
	if (flags & VBF_MIGRATE_ALL)
		return 1;
	return !reiser4_inode_get_flag(inode, REISER4_FILE_IMMOBILE);
}

int inode_clr_immobile(struct inode *inode);

/**
 * Balance an asymmetric logical volume. See description of the method
 * in plugin.h
 *
 * @super: super-block of the volume to be balanced;
 *
 * Implementation details:
 *
 * Walk from left to right along the twig level of the storage tree
 * and for every found regular file's inode migrate its data blocks.
 *
 * Stat-data (on-disk inodes) are located on leaf level, nevertheless
 * we scan twig level, recovering stat-data from extent items. Simply
 * because scanning twig level is ~1000 times faster (thanks to Hans,
 * who had insisted on EOTTL at the time).
 *
 * When scanning twig level we obviously miss empty files (i.e. files
 * without bodies). It doesn't lead to any problems, as there is nothing
 * to migrate for those files.
 *
 * FIXME: use hint/seal to not traverse tree every time when searching
 * for a position by "current" key of the iteration context.
 */
int balance_volume_asym(struct super_block *super, u32 flags)
{
	int ret;
	coord_t coord;
	lock_handle lh;
	reiser4_key start_key;
	struct reiser4_iterate_context ictx;
	struct migration_context *mctx;
	u64 to_write = 0;
	time64_t start;
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
	mctx = alloc_migration_context();
	if (!mctx)
		return -ENOMEM;
	assert("edward-1881", super != NULL);

	printk("reiser4 (%s): Started balancing...\n", super->s_id);
	start = ktime_get_seconds();

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
		u64 to_write_iter = 0;
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
		if (IS_ERR(inode))
			/*
			 * file was removed
			 */
			goto next;
		if (file_is_migratable(inode, super, flags)) {
			reiser4_iget_complete(inode);
			/*
			 * migrate data blocks of this file
			 */
			ret = inode_file_plugin(inode)->migrate(inode, mctx,
							  &to_write_iter, NULL);
			if (ret) {
				iput(inode);
				warning("edward-1889",
				      "Inode %lli: data migration failed (%d)",
				      (unsigned long long)get_inode_oid(inode),
				      ret);
				goto error;
			}
			if (flags & VBF_CLR_IMMOBILE) {
				ret = inode_clr_immobile(inode);
				if (ret)
					warning("edward-2472",
			      "Inode %lli: failed to clear immobile status(%d)",
			      (unsigned long long)get_inode_oid(inode),
			      ret);
			}
			to_write += to_write_iter;
		}
		iput(inode);
		if (to_write >= MIGR_LARGE_CHUNK_PAGES) {
			txnmgr_force_commit_all(super, 0);
			to_write = 0;
		}
	next:
		if (terminate)
			break;
		ictx.curr = ictx.next;
	}
 done:
	if (to_write)
		txnmgr_force_commit_all(super, 0);
	printk("reiser4 (%s): Balancing completed in %lld seconds.\n",
	       super->s_id, ktime_get_seconds() - start);
	free_migration_context(mctx);
	return 0;
 error:
	warning("edward-2155", "%s: Balancing aborted (%d).", super->s_id, ret);
	free_migration_context(mctx);
	return ret == -E_DEADLOCK ? -EAGAIN : ret;
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
		.calc_brick = calc_brick_simple,
		.find_brick = find_brick_simple,
		.load_volume = NULL,
		.done_volume = NULL,
		.init_volume = init_volume_simple,
		.resize_brick = resize_brick_simple,
		.add_brick = add_brick_simple,
		.remove_brick = remove_brick_simple,
		.remove_brick_tail = NULL,
		.print_brick = print_brick_simple,
		.print_volume = print_volume_simple,
		.balance_volume = balance_volume_simple,
		.bucket_ops = {
			.cap_at = NULL,
			.apx_of = NULL,
			.apx_at = NULL,
			.apx_set_at = NULL,
			.apx_lenp_at = NULL
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
		.calc_brick = calc_brick_asym,
		.find_brick = find_brick_asym,
		.load_volume = load_volume_asym,
		.done_volume = done_volume_asym,
		.init_volume = init_volume_asym,
		.resize_brick = resize_brick_asym,
		.add_brick = add_brick_asym,
		.remove_brick = remove_brick_asym,
		.remove_brick_tail = remove_brick_tail_asym,
		.print_brick = print_brick_asym,
		.print_volume = print_volume_asym,
		.scale_volume = scale_volume_asym,
		.migrate_file = migrate_file_asym,
		.balance_volume = balance_volume_asym,
		.bucket_ops = {
			.bucket_type = bucket_type_asym,
			.bucket_name = bucket_name_asym,
			.cap_at = cap_at_asym,
			.capr_at = capr_at_asym,
			.apx_of = apx_of_asym,
			.apx_at = apx_at_asym,
			.apx_set_at = apx_set_at_asym,
			.apx_lenp_at = apx_lenp_at_asym,
			.idx2id = idx2id,
			.id2idx = id2idx,
			.cap_consump = cap_consump_asym
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
