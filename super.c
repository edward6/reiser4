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
#include "super.h"
#include "reiser4.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

static __u64 reserved_for_gid(const reiser4_subvol *subv, gid_t gid);
static __u64 reserved_for_uid(const reiser4_subvol *subv, uid_t uid);
static __u64 reserved_for_root(const reiser4_subvol *subv);

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

/* get number of blocks in logical volume */
__u64 reiser4_volume_block_count(const struct super_block *super)
{
	__u32 subv_id;
	__u64 result = 0;

	assert("edward-xxx", super != NULL);

	for_each_notmirr(subv_id)
		result += reiser4_subvol_block_count(super_subvol(super,
								  subv_id));
	return result;
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
	__u32 subv_id;
	__u64 result = 0;

	assert("edward-xxx", super != NULL);

	for_each_notmirr(subv_id)
		result += reiser4_subvol_blocks_reserved(super_subvol(super,
								      subv_id));
	return result;
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

__u64 reiser4_subvol_fiber_len(reiser4_subvol *subv)
{
	assert("edward-xxx", subv != NULL);
	return subv->fiber_len;
}

void reiser4_subvol_set_fiber_len(reiser4_subvol *subv, __u64 len)
{
	assert("edward-xxx", subv != NULL);
	subv->fiber_len = len;
}

/* amount of free blocks in logical volume */
__u64 reiser4_volume_free_blocks(const struct super_block *super)
{
	__u32 subv_id;
	__u64 result = 0;

	assert("edward-xxx", super != NULL);

	for_each_notmirr(subv_id)
		result += reiser4_subvol_free_blocks(super_subvol(super,
								  subv_id));
	return result;

}

/* get mkfs unique identifier */
__u32 reiser4_mkfs_id(const struct super_block *super, __u32 subv_id)
{
	assert("vpf-221", super != NULL);
	return super_subvol(super, subv_id)->mkfs_id;
}

/* amount of free blocks */
__u64 reiser4_subvol_free_committed_blocks(const reiser4_subvol *subv)
{
	return subv->blocks_free_committed;
}

/* amount of blocks in subvolume reserved for @uid and @gid */
long reiser4_subvol_reserved4user(const reiser4_subvol *subv,
				  uid_t uid, /* user id */
				  gid_t gid  /* group id */)
{
	long reserved = 0;

	assert("nikita-456", subv != NULL);

	if (REISER4_SUPPORT_GID_SPACE_RESERVATION)
		reserved += reserved_for_gid(subv, gid);
	if (REISER4_SUPPORT_UID_SPACE_RESERVATION)
		reserved += reserved_for_uid(subv, uid);
	if (REISER4_SUPPORT_ROOT_SPACE_RESERVATION && (uid == 0))
		reserved += reserved_for_root(subv);
	return reserved;
}

/* amount of blocks in volume reserved for @uid and @gid */
long reiser4_volume_reserved4user(const struct super_block *super,
				  uid_t uid, /* user id */
				  gid_t gid  /* group id */)
{
	__u32 subv_id;
	__u64 result = 0;

	assert("edward-xxx", super != NULL);

	for_each_notmirr(subv_id)
		result +=
		reiser4_subvol_reserved4user(super_subvol(super, subv_id),
					     uid, gid);
	return result;
}

/* get/set value of/to grabbed blocks counter */
__u64 reiser4_grabbed_blocks(const reiser4_subvol *subv)
{
	assert("zam-512", subv != NULL);

	return subv->blocks_grabbed;
}

__u64 reiser4_flush_reserved(const reiser4_subvol *subv)
{
	assert("vpf-285", subv != NULL);

	return subv->blocks_flush_reserved;
}

/* get/set value of/to counter of fake allocated formatted blocks */
__u64 reiser4_fake_allocated(const reiser4_subvol *subv)
{
	assert("zam-516", subv != NULL);

	return subv->blocks_fake_allocated;
}

/* get/set value of/to counter of fake allocated unformatted blocks */
__u64 reiser4_fake_allocated_unformatted(const reiser4_subvol *subv)
{
	assert("zam-516", subv != NULL);

	return subv->blocks_fake_allocated_unformatted;
}

/* get/set value of/to counter of clustered blocks */
__u64 reiser4_clustered_blocks(const reiser4_subvol *subv)
{
	assert("edward-601", subv != NULL);

	return subv->blocks_clustered;
}

/* space allocator used by this subvolume */
reiser4_space_allocator *reiser4_get_space_allocator(reiser4_subvol *subv)
{
	assert("edward-xxx", subv != NULL);
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

/* amount of blocks reserved for given group in file system */
static __u64 reserved_for_gid(const reiser4_subvol *subv UNUSED_ARG,
			      gid_t gid UNUSED_ARG/* group id */)
{
	return 0;
}

/* amount of blocks reserved for given user in file system */
static __u64 reserved_for_uid(const reiser4_subvol *subv UNUSED_ARG,
			      uid_t uid UNUSED_ARG/* user id */)
{
	return 0;
}

/* amount of blocks reserved for super user in file system */
static __u64 reserved_for_root(const reiser4_subvol *subv UNUSED_ARG)
{
	return 0;
}

/*
 * true if block number @blk makes sense for the file system at @super.
 */
int reiser4_blocknr_is_sane_for(const reiser4_subvol *subv,
				const reiser4_block_nr *blk)
{
	assert("nikita-2957", subv != NULL);
	assert("nikita-2958", blk != NULL);

	if (reiser4_blocknr_is_fake(blk))
		return 1;
	return *blk < reiser4_subvol_block_count(subv);
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
