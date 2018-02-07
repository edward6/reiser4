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
#include "../../ondisk_fiber.h"
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

static __u64 get_format40_num_origins(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->num_origins));
}

static int get_format40_num_sgs_bits(const format40_disk_super_block * sb)
{
	return sb->num_sgs_bits;
}

static __u64 get_format40_data_room(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->data_room));
}

static __u64 get_format40_volinfo_loc(const format40_disk_super_block * sb)
{
	return le64_to_cpu(get_unaligned(&sb->volinfo_loc));
}

static __u32 get_format40_version(const format40_disk_super_block * sb)
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
	return (get_format40_version(sb) < get_release_number_minor());
}

static int incomplete_compatibility(const format40_disk_super_block * sb)
{
	return (get_format40_version(sb) > get_release_number_minor());
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

static format40_disk_super_block *copy_sb(struct page *page)
{
	format40_disk_super_block *sb_copy;

	sb_copy = kmalloc(sizeof(format40_disk_super_block),
			  reiser4_ctx_gfp_mask_get());
	if (sb_copy == NULL)
		return ERR_PTR(RETERR(-ENOMEM));

	memcpy(sb_copy, kmap(page), sizeof(*sb_copy));
	kunmap(page);
	return sb_copy;
}

static int check_key_format(const format40_disk_super_block *sb_copy)
{
	if (!equi(REISER4_LARGE_KEY,
		  get_format40_flags(sb_copy) & (1 << FORMAT40_LARGE_KEYS))) {
		warning("nikita-3228", "Key format mismatch. "
			"Only %s keys are supported.",
			REISER4_LARGE_KEY ? "large" : "small");
		return RETERR(-EINVAL);
	}
	return 0;
}

int check_set_core_params(reiser4_subvol *subv,
			  format40_disk_super_block *sb_format)
{
	reiser4_volume *vol;
	u32 ondisk_num_origins;
	const char *what_is_wrong;

	if (subvol_is_set(subv, SUBVOL_IS_NEW)) {
		subv->id = INVALID_SUBVOL_ID; /* actual ID will be set
						 by volume operation */
		return 0;
	}
	vol = super_volume(subv->super);
	ondisk_num_origins = get_format40_num_origins(sb_format);

	if (ondisk_num_origins == 0)
		/*
		 * This is a subvolume of format 4.0.Y
		 * We handle this special case for backward compatibility:
		 * guess number of subvolumes
		 */
		ondisk_num_origins = 1;

	if (vol->num_origins == 0)
		vol->num_origins = ondisk_num_origins;
	else if (vol->num_origins != ondisk_num_origins) {
		what_is_wrong = "different numbers of original subvolumes";
		goto error;
	}
	if (vol->num_sgs_bits == 0)
		vol->num_sgs_bits = get_format40_num_sgs_bits(sb_format);
	else if (vol->num_sgs_bits != get_format40_num_sgs_bits(sb_format)) {
		what_is_wrong = "different numbers of hash space segments";
		goto error;
	}
	subv->id = get_format40_origin_id(sb_format);
	if (subv->id >= vol->num_origins) {
		what_is_wrong = "that subvolume ID is too large";
		goto error;
	}
	return 0;
 error:
	warning("edward-1787", "%s: Found %s. Fsck?",
		subv->super->s_id, what_is_wrong);
	return -EINVAL;
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
struct page *find_format_format40(reiser4_subvol *subv)
{
	int ret;
	struct page *page;
	format40_disk_super_block *disk_sb;
	reiser4_volume *vol;

	assert("edward-1788", subv != NULL);
	assert("edward-1789", subv->super != NULL);

	vol = super_volume(subv->super);

	subv->jloc.footer = FORMAT40_JOURNAL_FOOTER_BLOCKNR;
	subv->jloc.header = FORMAT40_JOURNAL_HEADER_BLOCKNR;
	subv->loc_super   = FORMAT40_OFFSET / subv->super->s_blocksize;

	page = read_cache_page_gfp(subv->bdev->bd_inode->i_mapping,
				   subv->loc_super,
				   GFP_NOFS);
	if (IS_ERR_OR_NULL(page))
		return ERR_PTR(RETERR(-EIO));

	disk_sb = kmap(page);
	if (strncmp(disk_sb->magic, FORMAT40_MAGIC, sizeof(FORMAT40_MAGIC))) {
		/*
		 * there is no reiser4 on this device
		 */
		kunmap(page);
		put_page(page);
		return ERR_PTR(RETERR(-EINVAL));
	}
	ret = check_set_core_params(subv, disk_sb);
	if (ret) {
		kunmap(page);
		put_page(page);
		return ERR_PTR(RETERR(-EINVAL));
	}
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
	kunmap(page);
	return page;
}

/**
 * Initialize in-memory subvolume header.
 * Pre-condition: we are sure that subvolume is of format40
 * (i.e. format superblock with correct magic was found).
 */
int try_init_format40(struct super_block *super,
		      format40_init_stage *stage, reiser4_subvol *subv)
{
	int result;
	struct page *page;
	format40_disk_super_block *sb_format;
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
		super->s_flags |= MS_RDONLY;
	}
	if (has_replicas(subv) &&
	    extended_status == REISER4_ESTATUS_MIRRORS_NOT_SYNCED) {
		warning("edward-1792",
			"Mounting %s with not synced mirrors. "
			"Forcing read-only mount.", super->s_id);
		super->s_flags |= MS_RDONLY;
	}

	result = reiser4_journal_replay(subv);
	if (result)
		return result;
	*stage = JOURNAL_REPLAY;
	/*
	 * Now read the most recent version of format superblock
	 * after journal replay
	 */
	page = find_format_format40(subv);
	if (IS_ERR(page))
		return PTR_ERR(page);
	*stage = READ_SUPER;
	/*
	 * allocate and make a copy of format40_disk_super_block
	 */
	sb_format = copy_sb(page);
	put_page(page);

	if (IS_ERR(sb_format))
		return PTR_ERR(sb_format);
	printk("reiser4 (%s): found disk format 4.0.%u.\n",
	       super->s_id,
	       get_format40_version(sb_format));
	if (incomplete_compatibility(sb_format))
		printk("reiser4 (%s): format version number (4.0.%u) is "
		       "greater than release number (4.%u.%u) of reiser4 "
		       "kernel module. Some objects of the subvolume can "
		       "be inaccessible.\n",
		       super->s_id,
		       get_format40_version(sb_format),
		       get_release_number_major(),
		       get_release_number_minor());
	/*
	 * make sure that key format of kernel and filesystem match
	 */
	result = check_key_format(sb_format);
	if (result) {
		kfree(sb_format);
		return result;
	}
	*stage = KEY_CHECK;

	if (get_format40_flags(sb_format) & (1 << FORMAT40_HAS_DATA_ROOM))
		subv->flags |= (1 << SUBVOL_HAS_DATA_ROOM);

	result = oid_init_allocator(super, get_format40_file_count(sb_format),
				    get_format40_oid(sb_format));
	if (result) {
		kfree(sb_format);
		return result;
	}
	*stage = INIT_OID;

	root_block = get_format40_root_block(sb_format);
	height = get_format40_tree_height(sb_format);
	nplug = node_plugin_by_id(get_format40_node_plugin_id(sb_format));
	/*
	 * initialize storage tree.
	 */
	result = reiser4_subvol_init_tree(super, subv,
					  &root_block, height, nplug);
	if (result) {
		kfree(sb_format);
		return result;
	}
	*stage = INIT_TREE;
	/*
	 * set private subvolume parameters
	 */
	subv->mkfs_id = get_format40_mkfs_id(sb_format);
	subv->version = get_format40_version(sb_format);
	subv->blocks_free_committed = subv->blocks_free;

	subv->flush.relocate_threshold = FLUSH_RELOCATE_THRESHOLD;
	subv->flush.relocate_distance = FLUSH_RELOCATE_DISTANCE;
	subv->flush.written_threshold = FLUSH_WRITTEN_THRESHOLD;
	subv->flush.scan_maxnodes = FLUSH_SCAN_MAXNODES;

	if (update_backup_version(sb_format))
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
	if (result != 0) {
		kfree(sb_format);
		return result;
	}
	*stage = JOURNAL_RECOVER;
	/*
	 * recover_sb_data() sets actual data of free blocks,
	 * So we need to update the number of used blocks.
	 */
	reiser4_subvol_set_used_blocks(subv,
				       reiser4_subvol_block_count(subv) -
				       reiser4_subvol_free_blocks(subv));
#if REISER4_DEBUG
	subv->min_blocks_used = 16 /* reserved area */  +
		2 /* super blocks */  +
		2 /* journal footer and header */ ;
#endif
	/*
	 * init disk space allocator
	 */
	result = sa_init_allocator(&subv->space_allocator, super, subv, NULL);
	if (result) {
		kfree(sb_format);
		return result;
	}
	*stage = INIT_SA;

	result = get_sb_format_jnode(subv);
	if (result) {
		kfree(sb_format);
		return result;
	}
	*stage = INIT_JNODE;

	reiser4_subvol_set_data_room(subv, get_format40_data_room(sb_format));
	/*
	 * load volume system information, or its part, which was stored
	 * on that subvolume
	 */
	subv->volmap_loc = get_format40_volinfo_loc(sb_format);
	if (vol->vol_plug->load_volume)
		result = vol->vol_plug->load_volume(subv);
	kfree(sb_format);
	if (result)
		return result;
	*stage = ALL_DONE;

	printk("reiser4 (%s): using %s.\n", subv->name,
	       txmod_plugin_by_id(subv->txmod)->h.desc);
	return 0;
}

/**
 * ->init_format() method of disk_format40 plugin
 */
int init_format_format40(struct super_block *s, reiser4_subvol *subv)
{
	int result;
	format40_init_stage stage;
	reiser4_volume *vol;

	vol = get_super_private(s)->vol;

	result = try_init_format40(s, &stage, subv);
	switch (stage) {
	case ALL_DONE:
		assert("nikita-3458", result == 0);
		break;
	case INIT_SYSTAB:
		if (vol->vol_plug->done_volume)
			vol->vol_plug->done_volume(subv);
	case INIT_JNODE:
		put_sb_format_jnode(subv);
	case INIT_SA:
		sa_destroy_allocator(reiser4_get_space_allocator(subv),
				     s, subv);
	case JOURNAL_RECOVER:
	case INIT_TREE:
		reiser4_done_tree(&subv->tree);
	case INIT_OID:
	case KEY_CHECK:
	case READ_SUPER:
	case JOURNAL_REPLAY:
	case INIT_STATUS:
		reiser4_status_finish(subv);
	case INIT_JOURNAL_INFO:
		reiser4_done_journal_info(subv);
	case NONE_DONE:
		break;
	default:
		impossible("nikita-3457", "init stage: %i", stage);
	}
	if (!rofs_super(s) &&
	    reiser4_subvol_free_blocks(subv) < RELEASE_RESERVED)
		return RETERR(-ENOSPC);
	return result;
}

static void pack_format40_super(const struct super_block *s,
				reiser4_subvol *subv, char *data)
{
	format40_disk_super_block *format_sb =
		(format40_disk_super_block *) data;
	reiser4_volume *vol = super_volume(s);

	assert("zam-591", data != NULL);

	put_unaligned(cpu_to_le64(reiser4_subvol_free_committed_blocks(subv)),
		      &format_sb->free_blocks);

	put_unaligned(cpu_to_le64(subv->tree.root_block),
		      &format_sb->root_block);

	put_unaligned(cpu_to_le64(oid_next(s)), &format_sb->oid);

	put_unaligned(cpu_to_le64(oids_used(s)), &format_sb->file_count);

	put_unaligned(cpu_to_le16(subv->tree.height), &format_sb->tree_height);

	put_unaligned(cpu_to_le64(subv->volmap_loc), &format_sb->volinfo_loc);

	put_unaligned(cpu_to_le64(vol->num_origins), &format_sb->num_origins);

	put_unaligned(cpu_to_le64(subv->id), &format_sb->origin_id);

	put_unaligned(cpu_to_le64(subv->data_room), &format_sb->data_room);

	if (update_disk_version_minor(format_sb)) {
		__u32 version = PLUGIN_LIBRARY_VERSION | FORMAT40_UPDATE_BACKUP;

		put_unaligned(cpu_to_le32(version), &format_sb->version);
	}
	if (reiser4_volume_is_unbalanced(s)) {
		u64 flags = get_format40_flags(format_sb) |
			(1 << FORMAT40_UNBALANCED_VOLUME);
		put_unaligned(cpu_to_le64(flags), &format_sb->flags);
	}
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
	int ret;
	reiser4_volume *vol = super_volume(s);

	if (!rofs_super(s) &&
	    !get_current_context()->init_vol_failed) {

		ret = capture_brick_super(subv);
		if (ret != 0)
			warning("vs-898",
				"Failed to capture superblock (%d)", ret);

		ret = txnmgr_force_commit_all(s, 1);
		if (ret != 0)
			warning("jmacd-74438", "txn_force failed: %d", ret);

		all_grabbed2free();
	}
	sa_destroy_allocator(&subv->space_allocator, s, subv);
	if (vol->vol_plug->done_volume)
		vol->vol_plug->done_volume(subv);
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

/**
 * ->version_update() method of disk_format40 plugin
 * Upgrade minor disk format version number
 */
int version_update_format40(struct super_block *super, reiser4_subvol *subv)
{
	txn_handle * trans;
	lock_handle lh;
	txn_atom *atom;
	int ret;

	if (subv->id != METADATA_SUBVOL_ID)
		return 0;

	if (super->s_flags & MS_RDONLY ||
	    subv->version >= get_release_number_minor())
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
	if (ret != 0)
		return ret;

	znode_make_dirty(lh.node);
	done_lh(&lh);

	/* NOTE-EDWARD: Backup blocks stuff in fsck makes queasy */

	/* Force write_logs immediately. */
	trans = get_current_context()->trans;
	atom = get_current_atom_locked();
	assert("vpf-1906", atom != NULL);

	spin_lock_txnh(trans);
	return force_commit_atom(trans);
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
