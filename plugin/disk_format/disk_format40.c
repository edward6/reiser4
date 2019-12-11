/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../../dformat.h"
#include "../../key.h"
#include "../node/node.h"
#include "../space/space_allocator.h"
#include "disk_format40.h"
#include "../plugin.h"
#include "../../txnmgr.h"
#include "../../jnode.h"
#include "../../tree.h"
#include "../../super.h"
#include "../../plugin/volume/volume.h"
#include "../../wander.h"
#include "../../inode.h"
#include "../../ktxnmgrd.h"
#include "../../status_flags.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

/*
 * Methods of standard disk layout for simple volumes (i.e. volumes
 * associated with a single physical, or logical (RAID, LVM) device.
 */

/*
 * Amount of free blocks needed to perform release_format40 when fs gets
 * mounted RW:
 * 1 for SB,
 * 1 for non-leaves in overwrite set,
 * 2 for tx header & tx record
 */
#define RELEASE_RESERVED 4

/*
 * This flag indicates that backup should be updated by fsck
 */
#define FORMAT40_UPDATE_BACKUP (1 << 31)

/*
 * Functions to access fields of format40_disk_super_block
 */
static __u64 get_format40_block_count(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->block_count));
}

static __u64 get_format40_free_blocks(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->free_blocks));
}

static __u64 get_format40_root_block(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->root_block));
}

static __u16 get_format40_tree_height(const format40_disk_super_block * sb)
{
	return le16_to_cpu(get_unaligned(&sb->tree_height));
}

static __u64 get_format40_file_count(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->file_count));
}

static __u64 get_format40_oid(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->oid));
}

static __u32 get_format40_mkfs_id(const format40_disk_super_block * sb)
{
	return le32_to_cpu(get_unaligned(&sb->mkfs_id));
}

static __u32 get_format40_node_plugin_id(const format40_disk_super_block * sb)
{
	return le32_to_cpu(get_unaligned(&sb->node_pid));
}

static __u64 get_format40_flags(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->flags));
}

static __u64 get_format40_origin_id(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->origin_id));
}

static __u64 get_format40_nr_origins(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->nr_origins));
}

static int get_format40_num_sgs_bits(const format40_disk_super_block * sb)
{
	return sb->num_sgs_bits;
}

static __u64 get_format40_data_capacity(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->data_capacity));
}

static __u64 get_format40_volinfo_loc(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->volinfo_loc));
}

static __u64 get_format40_nr_mslots(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->nr_mslots));
}

static __u64 get_format40_min_occup(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->min_occup));
}

static __u32 format40_get_minor_version_nr(const format40_disk_super_block * sb)
{
	return le32_to_cpu(get_unaligned(&sb->version)) &
		~FORMAT40_UPDATE_BACKUP;
}

static int update_backup_version(const format40_disk_super_block * sb)
{
	return (le32_to_cpu(get_unaligned(&sb->version)) &
		FORMAT40_UPDATE_BACKUP);
}

static int update_disk_version_minor(const format40_disk_super_block * sb)
{
	return format40_get_minor_version_nr(sb) < get_release_number_minor();
}

static int incomplete_compatibility(const format40_disk_super_block * sb)
{
	return format40_get_minor_version_nr(sb) > get_release_number_minor();
}

static int get_sb_format_jnode(reiser4_subvol *subv)
{
	int ret;
	jnode *sb_jnode;

	sb_jnode = reiser4_alloc_io_head(&subv->loc_super, subv);

	ret = jload(sb_jnode);

	if (ret) {
		reiser4_drop_io_head(sb_jnode);
		return ret;
	}
	pin_jnode_data(sb_jnode);
	jrelse(sb_jnode);

	subv->sb_jnode = sb_jnode;

	return 0;
}

static void put_sb_format_jnode(reiser4_subvol *subv)
{
	if (subv->sb_jnode) {
		unpin_jnode_data(subv->sb_jnode);
		reiser4_drop_io_head(subv->sb_jnode);
		subv->sb_jnode = NULL;
	}
}

typedef enum format40_init_stage {
	NONE_DONE = 0,
	CONSULT_DISKMAP,
	FIND_A_SUPER,
	INIT_JOURNAL_INFO,
	INIT_STATUS,
	JOURNAL_REPLAY,
	READ_SUPER,
	KEY_CHECK,
	INIT_OID,
	INIT_TREE,
	JOURNAL_RECOVER,
	INIT_SA,
	INIT_JNODE,
	INIT_SYSTAB,
	ALL_DONE
} format40_init_stage;

static int check_key_format(const format40_disk_super_block *sb_copy)
{
	if (!equi(REISER4_LARGE_KEY,
		  get_format40_flags(sb_copy) & (1 << FORMAT40_LARGE_KEYS))) {
		warning("nikita-3228", "Key format mismatch. "
			"Only %s keys are supported.",
			REISER4_LARGE_KEY ? "large" : "small");
		return RETERR(-EINVAL);
	}
	if (!equi(REISER4_PLANB_KEY_ALLOCATION,
	     get_format40_flags(sb_copy) & (1 << FORMAT40_PLANB_KEY_ALLOC))) {
		warning("edward-2311", "Key allocation scheme mismatch. "
			"Only %s key allocation is supported.",
			REISER4_PLANB_KEY_ALLOCATION ? "Plan-B" : "Plan-A");
		return RETERR(-EINVAL);
	}
	return 0;
}

/**
 * Read on-disk system parameters, which define volume configuration.
 * Perform sanity check.
 */
int read_check_volume_params(reiser4_subvol *subv,
			     format40_disk_super_block *sb_format)
{
	reiser4_volume *vol;

	if (subvol_is_set(subv, SUBVOL_IS_ORPHAN)) {
		/*
		 * Don't check parameters of new brick
		 * as they are invalid (to be set later).
		 * Set invalid brick ID to not confuse
		 * the new brick with meta-data brick
		 */
		subv->id = INVALID_SUBVOL_ID;
		return 0;
	}
	vol = super_volume(subv->super);

	if (is_meta_brick_id(subv->id)) {
		u32 nr_mslots;
		u32 nr_origins;

		nr_origins = get_format40_nr_origins(sb_format);
		if (nr_origins == 0)
			/*
			 * This is a subvolume of format 4.0.Y
			 * We handle this special case for backward
			 * compatibility - guess number of subvolumes
			 */
			nr_origins = 1;
		atomic_set(&vol->nr_origins, nr_origins);
		vol->num_sgs_bits = get_format40_num_sgs_bits(sb_format);

		nr_mslots = get_format40_nr_mslots(sb_format);
		if (nr_mslots == 0) {
			/* ditto - guess number of mslots */
			assert("edward-2228", nr_origins == 1);
			nr_mslots = 1;
		}
		if (!vol->conf) {
			vol->conf = alloc_lv_conf(nr_mslots);
			if (!vol->conf)
				return -ENOMEM;
		} else if (vol->conf != NULL && nr_mslots > 1) {
			/*
			 * This it temporary config created
			 * for meta-data brick activation.
			 * Replace it with actual one.
			 */
			lv_conf *new_conf;
			int meta_subv_id;

			meta_subv_id = vol->vol_plug->meta_subvol_id();

			assert("edward-2304", vol->conf->nr_mslots == 1);
			assert("edward-2305",
			       vol->conf->mslots[meta_subv_id][1] != NULL);
			assert("edward-2306", vol->conf->tab == NULL);

			new_conf = alloc_lv_conf(nr_mslots);
			if (!new_conf)
				return -ENOMEM;
			/*
			 * copy actual info from temporary config
			 * to the new one
			 */
			new_conf->mslots[meta_subv_id] =
				vol->conf->mslots[meta_subv_id];
			free_lv_conf(vol->conf);
			vol->conf = new_conf;
		}
	}
	assert("edward-2307", vol->conf != NULL);
	if (subv->id >= vol->conf->nr_mslots) {
		warning("edward-2308",
	             "brick %s (ID %llu) is inappropriate: too few mslots (%llu)",
			subv->name, subv->id, vol->conf->nr_mslots);
		return -EINVAL;
	}
	return 0;
}

/**
 * Find disk format super block at specified location. Note that it
 * may be not the most recent version in the case of calling before
 * journal replay. In this case the caller should have a guarantee
 * that needed data are really actual.
 * Perform checks and initialisations in accordance with format40
 * specifications.
 *
 * Pre-condition: @super contains valid block size
 */
static int find_format40(reiser4_subvol *subv,
			 format40_disk_super_block *disk_sb)
{
	int ret;
	struct page *page;
	reiser4_volume *vol;

	assert("edward-1788", subv != NULL);
	assert("edward-1789", subv->super != NULL);

	vol = super_volume(subv->super);

	page = read_cache_page_gfp(subv->bdev->bd_inode->i_mapping,
				   subv->loc_super,
				   GFP_NOFS);
	if (IS_ERR_OR_NULL(page))
		return RETERR(-EIO);

	memcpy(disk_sb, kmap(page), sizeof (*disk_sb));
	kunmap(page);
	put_page(page);
	if (strncmp(disk_sb->magic, FORMAT40_MAGIC, sizeof(FORMAT40_MAGIC)))
		/*
		 * there is no reiser4 on this device
		 */
		return RETERR(-EINVAL);
	ret = read_check_volume_params(subv, disk_sb);
	if (ret)
		return RETERR(-EINVAL);
	reiser4_subvol_set_block_count(subv,
				       get_format40_block_count(disk_sb));
	reiser4_subvol_set_free_blocks(subv,
				       get_format40_free_blocks(disk_sb));
	/*
	 * Set number of used blocks. The number of used blocks is stored
	 * neither in on-disk super block nor in the journal footer blocks.
	 * Instead we maintain it along with actual values of total blocks
	 * and free block counters in the in-memory subvolume header
	 */
	reiser4_subvol_set_used_blocks(subv,
				       reiser4_subvol_block_count(subv) -
				       reiser4_subvol_free_blocks(subv));
	return 0;
}

int extract_subvol_id_format40(struct block_device *bdev, u64 *subv_id)
{
	*subv_id = 0;
	return 0;
}

/**
 * Read disk forat super-block, retrieve internal subvolume ID
 * and store it in @subv_id
 */
int extract_subvol_id_format41(struct block_device *bdev, u64 *subv_id)
{
	struct page *page;
	format40_disk_super_block *format_sb;

	page = read_cache_page_gfp(bdev->bd_inode->i_mapping,
				   FORMAT40_OFFSET >> PAGE_SHIFT,
				   GFP_NOFS);
	if (IS_ERR_OR_NULL(page))
		return RETERR(-EIO);

	format_sb = kmap(page);
	if (strncmp(format_sb->magic,
		    FORMAT40_MAGIC, sizeof(FORMAT40_MAGIC))) {
		/*
		 * format40 not found
		 */
		kunmap(page);
		put_page(page);
		return RETERR(-EINVAL);
	}
	*subv_id = get_format40_origin_id(format_sb);
	kunmap(page);
	put_page(page);
	return 0;
}

/**
 * Initialize in-memory subvolume header.
 * Pre-condition: we are sure that subvolume is managed by expected
 * disk format plugin(that is, format superblock with correct magic
 * was found).
 */
static int try_init_format(struct super_block *super,
			   format40_init_stage *stage,
			   reiser4_subvol *subv, int major_version_nr)
{
	int result;
	format40_disk_super_block sb_format;
	tree_level height;
	reiser4_block_nr root_block;
	node_plugin *nplug;
	u64 extended_status;
	reiser4_volume *vol;

	assert("vs-475", super != NULL);
	assert("vs-474", get_super_private(super) != NULL);
	assert("edward-1790", get_super_private(super)->vol != NULL);
	assert("edward-1791", !is_replica(subv));

	vol = get_super_private(super)->vol;

	*stage = NONE_DONE;

	subv->jloc.footer = FORMAT40_JOURNAL_FOOTER_BLOCKNR;
	subv->jloc.header = FORMAT40_JOURNAL_HEADER_BLOCKNR;
	subv->loc_super   = FORMAT40_OFFSET / subv->super->s_blocksize;

	result = reiser4_init_journal_info(subv);
	if (result)
		return result;
	*stage = INIT_JOURNAL_INFO;

	result = reiser4_status_init(subv, FORMAT40_STATUS_BLOCKNR);
	if (result != 0 && result != -EINVAL)
		/*
		 * -EINVAL means there is no magic, so probably just old fs
		 */
		return result;
	*stage = INIT_STATUS;

	result = reiser4_status_query(subv, NULL, &extended_status);
	if (result == REISER4_STATUS_MOUNT_WARN)
		warning("vpf-1363", "Mounting %s with errors.",
			super->s_id);

	if (result == REISER4_STATUS_MOUNT_RO) {
		warning("vpf-1364", "Mounting %s with fatal errors. "
			"Forcing read-only mount.", super->s_id);
		super->s_flags |= SB_RDONLY;
	}
	if (has_replicas(subv) &&
	    extended_status == REISER4_ESTATUS_MIRRORS_NOT_SYNCED) {
		warning("edward-1792",
			"Mounting %s with not synced mirrors. "
			"Forcing read-only mount.", super->s_id);
		super->s_flags |= SB_RDONLY;
	}
	/*
	 * Start form journal replay to make sure we are dealing
	 * with actual (most recent) data. All replicas will get
	 * respective update.
	 */
	result = reiser4_journal_replay(subv);
	if (result)
		return result;
	*stage = JOURNAL_REPLAY;
	/*
	 * Now read the most recent version of format superblock
	 * after journal replay
	 */
	result = find_format40(subv, &sb_format);
	if (result)
		return result;
	*stage = READ_SUPER;

	printk("reiser4 (%s): found disk format 4.%u.%u.\n",
	       super->s_id,
	       major_version_nr,
	       format40_get_minor_version_nr(&sb_format));
	if (incomplete_compatibility(&sb_format))
		printk("reiser4 (%s): format version number (4.%u.%u) is "
		       "greater than release number (4.%u.%u) of reiser4 "
		       "kernel module. Some objects of the subvolume can "
		       "be inaccessible.\n",
		       super->s_id,
		       major_version_nr,
		       format40_get_minor_version_nr(&sb_format),
		       get_release_number_major(),
		       get_release_number_minor());
	/*
	 * make sure that key format of kernel and filesystem match
	 */
	result = check_key_format(&sb_format);
	if (result)
		return result;

	*stage = KEY_CHECK;

	if (get_format40_flags(&sb_format) & (1 << FORMAT40_HAS_DATA_ROOM))
		subv->flags |= (1 << SUBVOL_HAS_DATA_ROOM);

	if (get_format40_flags(&sb_format) & (1 << FORMAT40_TO_BE_REMOVED))
		subv->flags |= (1 << SUBVOL_TO_BE_REMOVED);

	if (is_meta_brick_id(subv->id)) {
		result = oid_init_allocator(super,
					    get_format40_file_count(&sb_format),
					    get_format40_oid(&sb_format));
		if (result)
			return result;

		if (get_format40_flags(&sb_format) & (1 << FORMAT40_UNBALANCED_VOLUME))
			reiser4_volume_set_unbalanced(super);
	}
	*stage = INIT_OID;

	root_block = get_format40_root_block(&sb_format);
	height = get_format40_tree_height(&sb_format);
	nplug = node_plugin_by_id(get_format40_node_plugin_id(&sb_format));
	/*
	 * initialize storage tree.
	 */
	result = reiser4_subvol_init_tree(subv, &root_block, height, nplug);
	if (result)
		return result;
	*stage = INIT_TREE;
	/*
	 * set private subvolume parameters
	 */
	subv->mkfs_id = get_format40_mkfs_id(&sb_format);
	subv->version = format40_get_minor_version_nr(&sb_format);
	subv->blocks_free_committed = subv->blocks_free;

	subv->flush.relocate_threshold = FLUSH_RELOCATE_THRESHOLD;
	subv->flush.relocate_distance = FLUSH_RELOCATE_DISTANCE;
	subv->flush.written_threshold = FLUSH_WRITTEN_THRESHOLD;
	subv->flush.scan_maxnodes = FLUSH_SCAN_MAXNODES;

	if (update_backup_version(&sb_format))
		printk("reiser4: %s: use 'fsck.reiser4 --fix' "
		       "to complete disk format upgrade.\n", super->s_id);
	/*
	 * all formatted nodes in a subvolume managed by format40
	 * are of one plugin
	 */
	subv->flags |= (1 << SUBVOL_ONE_NODE_PLUGIN);
	/*
	 * Recover sb data which were logged separately from sb block
	 * NOTE-NIKITA: reiser4_journal_recover_sb_data() calls
	 * oid_init_allocator() and reiser4_set_free_blocks() with new
	 * data. What's the reason to call them above?
	 */
	result = reiser4_journal_recover_sb_data(super, subv);
	if (result)
		return result;
	*stage = JOURNAL_RECOVER;
	/*
	 * recover_sb_data() sets actual data of free blocks,
	 * So we need to update the number of used blocks.
	 */
	reiser4_subvol_set_used_blocks(subv,
				       reiser4_subvol_block_count(subv) -
				       reiser4_subvol_free_blocks(subv));
	reiser4_subvol_set_min_blocks_used(subv,
				       get_format40_min_occup(&sb_format));
	/*
	 * init disk space allocator
	 */
	result = sa_init_allocator(&subv->space_allocator, super, subv, NULL);
	if (result)
		return result;
	*stage = INIT_SA;

	result = get_sb_format_jnode(subv);
	if (result)
		return result;
	*stage = INIT_JNODE;

	reiser4_subvol_set_data_capacity(subv,
				get_format40_data_capacity(&sb_format));
	/*
	 * load addresses of volume configs
	 */
	subv->volmap_loc[CUR_VOL_CONF] = get_format40_volinfo_loc(&sb_format);

	if (vol->vol_plug->load_volume) {
		result = vol->vol_plug->load_volume(subv);
		if (result)
			return result;
	}
	*stage = ALL_DONE;

	printk("reiser4 (%s): using %s.\n", subv->name,
	       txmod_plugin_by_id(subv->txmod)->h.desc);
	return 0;
}

static int init_format_generic(struct super_block *s,
			       reiser4_subvol *subv, int version)
{
	int result;
	format40_init_stage stage;
	reiser4_volume *vol;

	vol = get_super_private(s)->vol;

	result = try_init_format(s, &stage, subv, version);
	switch (stage) {
	case ALL_DONE:
		assert("nikita-3458", result == 0);
		break;
	case INIT_SYSTAB:
	case INIT_JNODE:
		put_sb_format_jnode(subv);
		/* fall through */
	case INIT_SA:
		sa_destroy_allocator(reiser4_get_space_allocator(subv),
				     s, subv);
		/* fall through */
	case JOURNAL_RECOVER:
	case INIT_TREE:
		reiser4_done_tree(&subv->tree);
		/* fall through */
	case INIT_OID:
	case KEY_CHECK:
	case READ_SUPER:
		if (!sb_rdonly(s) &&
		    reiser4_subvol_free_blocks(subv) < RELEASE_RESERVED)
			result = RETERR(-ENOSPC);
		/* fall through */
	case JOURNAL_REPLAY:
	case INIT_STATUS:
		reiser4_status_finish(subv);
		/* fall through */
	case INIT_JOURNAL_INFO:
		reiser4_done_journal_info(subv);
		/* fall through */
	case NONE_DONE:
		break;
	default:
		impossible("nikita-3457", "init stage: %i", stage);
	}
	return result;
}

int init_format_format40(struct super_block *s, reiser4_subvol *subv)
{
	return init_format_generic(s, subv, 0 /* version */);
}

int init_format_format41(struct super_block *s, reiser4_subvol *subv)
{
	return init_format_generic(s, subv, 1 /* version */);
}

static void pack_format40_super(const struct super_block *s,
				reiser4_subvol *subv, char *data)
{
	format40_disk_super_block *format_sb =
		(format40_disk_super_block *) data;
	reiser4_volume *vol = super_volume(s);
	lv_conf *conf = vol->conf;
	u64 format_flags = get_format40_flags(format_sb);

	assert("zam-591", data != NULL);

	put_unaligned(cpu_to_le64(reiser4_subvol_free_committed_blocks(subv)),
		      &format_sb->free_blocks);

	put_unaligned(cpu_to_le64(subv->tree.root_block),
		      &format_sb->root_block);

	put_unaligned(cpu_to_le64(oid_next(s)), &format_sb->oid);

	put_unaligned(cpu_to_le64(oids_used(s)), &format_sb->file_count);

	put_unaligned(cpu_to_le16(subv->tree.height), &format_sb->tree_height);

	put_unaligned(cpu_to_le64(subv->id), &format_sb->origin_id);

	put_unaligned(cpu_to_le64(subv->data_capacity), &format_sb->data_capacity);

	if (update_disk_version_minor(format_sb)) {
		__u32 version = PLUGIN_LIBRARY_VERSION | FORMAT40_UPDATE_BACKUP;

		put_unaligned(cpu_to_le32(version), &format_sb->version);
	}
	if (subv->flags & (1 << SUBVOL_TO_BE_REMOVED))
		format_flags |= (1 << FORMAT40_TO_BE_REMOVED);
	else
		format_flags &= ~(1 << FORMAT40_TO_BE_REMOVED);

	if (is_meta_brick(subv)) {
		if (reiser4_volume_is_unbalanced(s))
			format_flags |= (1 << FORMAT40_UNBALANCED_VOLUME);
		else
			format_flags &= ~(1 << FORMAT40_UNBALANCED_VOLUME);

		if (subv->flags & (1 << SUBVOL_HAS_DATA_ROOM))
			format_flags |= (1 << FORMAT40_HAS_DATA_ROOM);
		else
			format_flags &= ~(1 << FORMAT40_HAS_DATA_ROOM);

		put_unaligned(cpu_to_le64(vol_nr_origins(vol)), &format_sb->nr_origins);

		put_unaligned(cpu_to_le64(conf->nr_mslots), &format_sb->nr_mslots);

		put_unaligned(cpu_to_le64(subv->volmap_loc[CUR_VOL_CONF]), &format_sb->volinfo_loc);

		put_unaligned(vol->num_sgs_bits, &format_sb->num_sgs_bits);
	}
	put_unaligned(cpu_to_le64(format_flags), &format_sb->flags);
}

/**
 * ->log_super() method of disk_format40 plugin.
 * Return a jnode which should be added to a transaction when the super block
 * gets logged
 */
jnode *log_super_format40(struct super_block *super, reiser4_subvol *subv)
{
	jload(subv->sb_jnode);
	pack_format40_super(super, subv, jdata(subv->sb_jnode));
	jrelse(subv->sb_jnode);

	return subv->sb_jnode;
}

/**
 * ->release() method of disk_format40 plugin
 */
int release_format40(struct super_block *s, reiser4_subvol *subv)
{
	sa_destroy_allocator(&subv->space_allocator, s, subv);
	reiser4_done_journal_info(subv);
	put_sb_format_jnode(subv);

	rcu_barrier();
	reiser4_done_tree(&subv->tree);
	/*
	 * call finish_rcu(), because some znode
	 * were "released" in reiser4_done_tree()
	 */
	rcu_barrier();

	return 0;
}

#define FORMAT40_ROOT_LOCALITY 41
#define FORMAT40_ROOT_OBJECTID 42

/**
 * ->root_dir_key() method of disk_format40 plugin
 */
const reiser4_key *root_dir_key_format40(const struct super_block *super
					 UNUSED_ARG)
{
	static const reiser4_key FORMAT40_ROOT_DIR_KEY = {
		.el = {
			__constant_cpu_to_le64((FORMAT40_ROOT_LOCALITY << 4) |
					       KEY_SD_MINOR),
#if REISER4_LARGE_KEY
			ON_LARGE_KEY(0ull,)
#endif
			__constant_cpu_to_le64(FORMAT40_ROOT_OBJECTID),
			0ull
		}
	};
	return &FORMAT40_ROOT_DIR_KEY;
}

/**
 * ->check_open() method of disk_format40 plugin
 * Check the opened object for validness.
 * For now it checks for the valid oid & locality only,
 * can be improved later and it its work may depend on
 * the mount options
 */
int check_open_format40(const struct inode *object)
{
	oid_t max, oid;

	max = oid_next(object->i_sb) - 1;
	/*
	 * Check the oid
	 */
	oid = get_inode_oid(object);
	if (oid > max) {
		warning("vpf-1360", "The object with the oid %llu "
			"greater then the max used oid %llu found.",
			(unsigned long long)oid, (unsigned long long)max);

		return RETERR(-EIO);
	}
	/*
	 * Check the locality
	 */
	oid = reiser4_inode_data(object)->locality_id;
	if (oid > max) {
		warning("vpf-1361", "The object with the locality %llu "
			"greater then the max used oid %llu found.",
			(unsigned long long)oid, (unsigned long long)max);

		return RETERR(-EIO);
	}
	return 0;
}

int version_update_format40(struct super_block *super, reiser4_subvol *subv)
{
	int ret;
	lock_handle lh;

	if (sb_rdonly(super) || subv->version >= get_release_number_minor())
		return 0;

	printk("reiser4 (%s): upgrading disk format to 4.0.%u.\n",
	       subv->name, get_release_number_minor());
	printk("reiser4 (%s): use 'fsck.reiser4 --fix' "
	       "to complete disk format upgrade.\n", subv->name);
	/*
	 * Mark the uber znode dirty to call ->log_super() on write_logs
	 */
	init_lh(&lh);
	ret = get_uber_znode(&subv->tree, ZNODE_WRITE_LOCK,
			     ZNODE_LOCK_HIPRI, &lh);
	if (ret) {
		BUG_ON(ret > 0);
		return ret;
	}
	znode_make_dirty(lh.node);
	done_lh(&lh);
	/*
	 * Backup blocks stuff in fsck makes me queasy - Edward.
	 */
	return 1;
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
