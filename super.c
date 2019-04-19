/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Super-block manipulations. */

#include "debug.h"
#include "dformat.h"
#include "key.h"
#include "plugin/security/perm.h"
#include "plugin/space/space_allocator.h"
#include "plugin/plugin.h"
#include "tree.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"
#include "reiser4.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

static __u64 reserved_for_gid(const struct super_block *sb, gid_t gid);
static __u64 reserved_for_uid(const struct super_block *sb, uid_t uid);
static __u64 reserved_for_root(const struct super_block *subv);

/* Return reiser4-specific part of super block */
reiser4_super_info_data *get_super_private_nocheck(const struct super_block *super)
{
	return (reiser4_super_info_data *) super->s_fs_info;
}

/* Return reiser4 fstype: value that is returned in ->f_type field by statfs()
 */
long reiser4_statfs_type(const struct super_block *super UNUSED_ARG)
{
	assert("nikita-448", super != NULL);
	assert("nikita-449", is_reiser4_super(super));
	return (long)REISER4_SUPER_MAGIC;
}

/* functions to read/modify fields of reiser4_super_info_data */

/* get number of blocks in subvolume */
__u64 reiser4_subvol_block_count(const reiser4_subvol *subv)
{
	assert("vs-494", subv != NULL);
	return subv->block_count;
}

/**
 * Scan mslots and collect statistics from each subvolume of a logical volume
 */
u64 reiser4_collect_super_stat(const struct super_block *sb,
			       u64 (*subvol_get_stat)(const reiser4_subvol *))
{
	u64 slot;
	u64 result = 0;

	for (slot = 0;; slot++) {
		u64 cnt;
		lv_conf *conf;
		reiser4_subvol *subv;

		rcu_read_lock();
		conf = super_conf(sb);

		if (slot >= conf->nr_mslots) {
			rcu_read_unlock();
			break;
		}
		if (!conf_mslot_at(conf, slot)) {
			rcu_read_unlock();
			continue;
		}
		subv = conf_origin(conf, slot);
		assert("edward-2272", subv != NULL);

		cnt = subvol_get_stat(subv);
		rcu_read_unlock();

		result += cnt;
	}
	return result;
}

/* get number of blocks in logical volume */
__u64 reiser4_volume_block_count(const struct super_block *super)
{
	return reiser4_collect_super_stat(super,
					  reiser4_subvol_block_count);
}

/*
 * Set number of blocks and reserved space for a logical volume.
 * Pre-condition: @nr is total number of blocks of all its subvolumes.
 */
void reiser4_subvol_set_block_count(reiser4_subvol *subv, __u64 nr)
{
	assert("vs-501", subv != NULL);

	subv->block_count = nr;
	/*
	 * The proper calculation of the reserved space counter (%5 of device
	 * block counter) we need a 64 bit division which is missing in Linux
	 * on i386 platform. Because we do not need a precise calculation here
	 * we can replace a div64 operation by this combination of
	 * multiplication and shift: 51. / (2^10) == .0498 .
	 * FIXME: this is a bug. It comes up only for very small filesystems
	 * which probably are never used. Nevertheless, it is a bug. Number of
	 * reserved blocks must be not less than maximal number of blocks which
	 * get grabbed with BA_RESERVED.
	 */
	subv->blocks_reserved = ((nr * 51) >> 10);
}

__u64 reiser4_subvol_blocks_reserved(const reiser4_subvol *subv)
{
	return subv->blocks_reserved;
}

__u64 reiser4_volume_blocks_reserved(const struct super_block *super)
{
	return reiser4_collect_super_stat(super,
					  reiser4_subvol_blocks_reserved);
}

/* amount of blocks used (allocated for data or meta-data) in subvolume */
__u64 reiser4_subvol_used_blocks(const reiser4_subvol *subv)
{
	assert("nikita-452", subv != NULL);
	return subv->blocks_used;
}

/* set number of blocks used */
void reiser4_subvol_set_used_blocks(reiser4_subvol *subv, __u64 nr)
{
	assert("vs-503", subv != NULL);
	subv->blocks_used = nr;
}

__u64 reiser4_subvol_min_blocks_used(const reiser4_subvol *subv)
{
	assert("edward-2332", subv != NULL);
	return subv->min_blocks_used;
}

void reiser4_subvol_set_min_blocks_used(reiser4_subvol *subv, __u64 nr)
{
	assert("edward-2333", subv != NULL);
	subv->min_blocks_used = nr;
}

/* amount of free blocks in subvolume */
__u64 reiser4_subvol_free_blocks(const reiser4_subvol *subv)
{
	assert("nikita-454", subv != NULL);
	return subv->blocks_free;
}

/* set number of free blocks */
void reiser4_subvol_set_free_blocks(reiser4_subvol *subv, __u64 nr)
{
	assert("vs-505", subv != NULL);
	subv->blocks_free = nr;
}

__u64 reiser4_subvol_data_room(reiser4_subvol *subv)
{
	assert("edward-1796", subv != NULL);
	assert("edward-1839",
	       subv->data_room <= reiser4_subvol_block_count(subv));
	return subv->data_room;
}

void reiser4_subvol_set_data_room(reiser4_subvol *subv, __u64 value)
{
	assert("edward-1797", subv != NULL);
	subv->data_room = value;
}

/* amount of free blocks in logical volume */
__u64 reiser4_volume_free_blocks(const struct super_block *super)
{
	return reiser4_collect_super_stat(super,
					  reiser4_subvol_free_blocks);
}

/* get mkfs unique identifier */
__u32 reiser4_mkfs_id(const struct super_block *super, __u32 subv_id)
{
	assert("vpf-221", super != NULL);
	return super_origin(super, subv_id)->mkfs_id;
}

/* amount of free blocks */
__u64 reiser4_subvol_free_committed_blocks(const reiser4_subvol *subv)
{
	return subv->blocks_free_committed;
}

/**
 * amount of blocks reserved for @uid and @gid in a volume
 */
long reiser4_volume_reserved4user(const struct super_block *sb,
				  uid_t uid, /* user id */
				  gid_t gid  /* group id */)
{
	long reserved = 0;

	assert("nikita-456", sb != NULL);

	if (REISER4_SUPPORT_GID_SPACE_RESERVATION)
		reserved += reserved_for_gid(sb, gid);
	if (REISER4_SUPPORT_UID_SPACE_RESERVATION)
		reserved += reserved_for_uid(sb, uid);
	if (REISER4_SUPPORT_ROOT_SPACE_RESERVATION && (uid == 0))
		reserved += reserved_for_root(sb);
	return reserved;
}

/* get/set value of/to grabbed blocks counter */
__u64 reiser4_subvol_grabbed_blocks(const reiser4_subvol *subv)
{
	assert("zam-512", subv != NULL);

	return subv->blocks_grabbed;
}

__u64 reiser4_subvol_flush_reserved(const reiser4_subvol *subv)
{
	assert("vpf-285", subv != NULL);

	return subv->blocks_flush_reserved;
}

/* get/set value of/to counter of fake allocated formatted blocks */
__u64 reiser4_subvol_fake_allocated_fmt(const reiser4_subvol *subv)
{
	assert("zam-516", subv != NULL);

	return subv->blocks_fake_allocated;
}

/* get/set value of/to counter of fake allocated unformatted blocks */
__u64 reiser4_subvol_fake_allocated_unf(const reiser4_subvol *subv)
{
	assert("zam-516", subv != NULL);

	return subv->blocks_fake_allocated_unformatted;
}

/* get/set value of/to counter of clustered blocks */
__u64 reiser4_subvol_clustered_blocks(const reiser4_subvol *subv)
{
	assert("edward-601", subv != NULL);

	return subv->blocks_clustered;
}

/* space allocator used by this subvolume */
reiser4_space_allocator *reiser4_get_space_allocator(reiser4_subvol *subv)
{
	assert("edward-1800", subv != NULL);
	return &subv->space_allocator;
}

/* return fake inode used to bind formatted nodes in the page cache */
struct inode *reiser4_get_super_fake(const struct super_block *super)
{
	assert("nikita-1757", super != NULL);
	return get_super_private(super)->fake;
}

/* return fake inode used to bind copied on capture nodes in the page cache */
struct inode *reiser4_get_cc_fake(const struct super_block *super)
{
	assert("nikita-1757", super != NULL);
	return get_super_private(super)->cc;
}

/* return fake inode used to bind bitmaps and journlal heads */
struct inode *reiser4_get_bitmap_fake(const struct super_block *super)
{
	assert("nikita-17571", super != NULL);
	return get_super_private(super)->bitmap;
}

/* Check that @super is (looks like) reiser4 super block. This is mainly for
   use in assertions. */
int is_reiser4_super(const struct super_block *super)
{
	return super != NULL &&
		get_super_private(super) != NULL &&
		super->s_op == &(get_super_private(super)->ops.super);
}

int reiser4_is_set(const struct super_block *super, reiser4_fs_flag f)
{
	return test_bit((int)f, &get_super_private(super)->fs_flags);
}

/**
 * amount of blocks reserved for given group in file system
 */
static __u64 reserved_for_gid(const struct super_block *sb, gid_t gid)
{
	return 0;
}

/**
 * amount of blocks reserved for given user in file system
 */
static __u64 reserved_for_uid(const struct super_block *sb, uid_t uid)
{
	return 0;
}

/**
 * amount of blocks reserved for super user in file system
 */
static __u64 reserved_for_root(const struct super_block *sb)
{
	return 0;
}

/**
 * true if block number @blk makes sense for the file system at @subv.
 */
int reiser4_subvol_blocknr_is_sane(const reiser4_subvol *subv,
				   const reiser4_block_nr *blk)
{
	assert("nikita-2957", subv != NULL);
	assert("nikita-2958", blk != NULL);

	if (reiser4_blocknr_is_fake(blk))
		return 1;
	return *blk < reiser4_subvol_block_count(subv);
}

#if REISER4_DEBUG
static u64 reiser4_subvol_fake_allocated(const reiser4_subvol *subv)
{
	return reiser4_subvol_fake_allocated_fmt(subv) +
		reiser4_subvol_fake_allocated_unf(subv);
}

u64 reiser4_volume_fake_allocated(const struct super_block *sb)
{
	u64 ret;
	spin_lock_reiser4_super(get_super_private(sb));
	ret = reiser4_collect_super_stat(sb,
					 reiser4_subvol_fake_allocated);
	spin_unlock_reiser4_super(get_super_private(sb));
	return ret;
}
#endif

int reiser4_volume_test_set_busy(struct super_block *sb)
{
	assert("edward-1947", sb != NULL);
	return test_and_set_bit(REISER4_BUSY_VOL,
				&get_super_private(sb)->fs_flags);
}

void reiser4_volume_clear_busy(struct super_block *sb)
{
	assert("edward-1949", sb != NULL);
	clear_bit(REISER4_BUSY_VOL, &get_super_private(sb)->fs_flags);
}

int reiser4_volume_is_unbalanced(const struct super_block *sb)
{
	assert("edward-1945", sb != NULL);
	return reiser4_is_set(sb, REISER4_UNBALANCED_VOL);
}

void reiser4_volume_set_unbalanced(struct super_block *sb)
{
	assert("edward-1946", sb != NULL);
	set_bit(REISER4_UNBALANCED_VOL, &get_super_private(sb)->fs_flags);
}

void reiser4_volume_clear_unbalanced(struct super_block *sb)
{
	assert("edward-1948", sb != NULL);
	clear_bit(REISER4_UNBALANCED_VOL, &get_super_private(sb)->fs_flags);
}

int reiser4_volume_has_incomplete_op(const struct super_block *sb)
{
	assert("edward-2247", sb != NULL);
	return reiser4_is_set(sb, REISER4_INCOMPLETE_BRICK_REMOVAL);
}

void reiser4_volume_set_incomplete_op(struct super_block *sb)
{
	assert("edward-2248", sb != NULL);
	set_bit(REISER4_INCOMPLETE_BRICK_REMOVAL, &get_super_private(sb)->fs_flags);
}

void reiser4_volume_clear_incomplete_op(struct super_block *sb)
{
	assert("edward-2249", sb != NULL);
	clear_bit(REISER4_INCOMPLETE_BRICK_REMOVAL, &get_super_private(sb)->fs_flags);
}

void reiser4_volume_set_activated(struct super_block *sb)
{
	assert("edward-2084", sb != NULL);
	set_bit(REISER4_ACTIVATED_VOL, &get_super_private(sb)->fs_flags);
}

int reiser4_volume_is_activated(struct super_block *sb)
{
	assert("edward-2085", sb != NULL);
	return reiser4_is_set(sb, REISER4_ACTIVATED_VOL);
}

/**
 * Calculate data subvolume by @inode and @offset
 */
reiser4_subvol *calc_data_subvol(const struct inode *inode, loff_t offset)
{
	return inode_file_plugin(inode)->calc_data_subvol(inode, offset);
}

/**
 * Return data subvolume by its ID stored on disk
 */
reiser4_subvol *find_data_subvol(const coord_t *coord)
{
	return current_origin(current_vol_plug()->data_subvol_id_find(coord));
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
