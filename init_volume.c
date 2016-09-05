/*
  Copyright (c) 2014-2016 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "debug.h"
#include "super.h"
#include "ondisk_fiber.h"
#include <linux/blkdev.h>

DEFINE_MUTEX(reiser4_volumes_mutex);
static LIST_HEAD(reiser4_volumes); /* list of registered volumes */

/*
 * Reiser5 logical volume (5.X.Y) is a set of subvolumes.
 * Every subvolume represents a physical or logical (LVM) device
 * formatted by plugin of REISER4_FORMAT_PLUGIN_TYPE.
 * All subvolumes are combined in "volume groups".
 * Volume plugin defines distribution among the groups.
 * Distribution plugin defines distribution within single group.
 */

static struct reiser4_volume *reiser4_alloc_volume(u8 *uuid,
						   int vol_pid,
						   int dist_pid,
						   int stripe_size_bits)
{
	struct reiser4_volume *vol;

	vol = kzalloc(sizeof(*vol), GFP_NOFS);
	if (!vol)
		return NULL;
	memcpy(vol->uuid, uuid, 16);
	INIT_LIST_HEAD(&vol->list);
	INIT_LIST_HEAD(&vol->subvols_list);
	vol->vol_plug = volume_plugin_by_id(vol_pid);
	vol->dist_plug = distribution_plugin_by_id(dist_pid);
	vol->stripe_size_bits = stripe_size_bits;
	return vol;
}

struct reiser4_subvol *reiser4_alloc_subvol(u8 *uuid, const char *path,
						   int dformat_pid,
						   __u64 diskmap)
{
	struct reiser4_subvol *subv;

	subv = kzalloc(sizeof(*subv), GFP_NOFS);
	if (!subv)
		return NULL;
	memcpy(subv->uuid, uuid, 16);

	INIT_LIST_HEAD(&subv->list);
	__init_ch_sub(&subv->ch);

	subv->diskmap = diskmap;
	subv->df_plug = disk_format_plugin_by_unsafe_id(dformat_pid);
	if (subv->df_plug == NULL) {
		warning("edward-xxx",
			"%s: unknown disk format plugin %d\n",
			path, dformat_pid);
		kfree(subv);
		return NULL;
	}
	subv->name = kstrdup(path, GFP_NOFS);
	if (!subv->name) {
		kfree(subv);
		return NULL;
	}
	return subv;
}

struct reiser4_volume *reiser4_search_volume(u8 *uuid)
{
	struct reiser4_volume *vol;

	list_for_each_entry(vol, &reiser4_volumes, list) {
		if (memcmp(uuid, vol->uuid, 16) == 0)
			return vol;
	}
	return NULL;
}

static struct reiser4_subvol *reiser4_search_subvol(u8 *uuid,
						    struct list_head *where)
{
	struct reiser4_subvol *sub;

	list_for_each_entry(sub, where, list) {
		if (memcmp(uuid, sub->uuid, 16) == 0)
			return sub;
	}
	return NULL;
}

/*
 * Register a simple reiser4 subvolume
 * Returns:
 * 0   - first time subvolume is seen
 * 1   - subvolume already registered
 * < 0 - error
 */
int reiser4_register_subvol(const char *path,
				   u8 *vol_uuid, u8 *sub_uuid,
				   int dformat_pid, int vol_pid, int dist_pid,
				   __u64 diskmap, int stripe_bits)
{
	struct reiser4_volume *vol;
	struct reiser4_subvol *sub;

	vol = reiser4_search_volume(vol_uuid);
	if (vol) {
		sub = reiser4_search_subvol(sub_uuid, &vol->subvols_list);
		if (sub)
			return 1;
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   diskmap);
		if (!sub)
			return -ENOMEM;
	} else {
		vol = reiser4_alloc_volume(vol_uuid, vol_pid, dist_pid,
					   stripe_bits);
		if (!vol)
			return -ENOMEM;
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   diskmap);
		if (!sub) {
			kfree(vol);
			return -ENOMEM;
		}
		list_add(&vol->list, &reiser4_volumes);
	}
	list_add(&sub->list, &vol->subvols_list);
	vol->num_subvols ++;
	printk("reiser4: registered subvolume (%s)\n", path);
	return 0;
}

static void reiser4_put_volume(struct reiser4_volume *vol)
{
	assert("edward-xxx", vol->aib == NULL);
	assert("edward-xxx", vol->subvols == NULL);
	kfree(vol);
}

static void reiser4_put_subvol(struct reiser4_subvol *subv)
{
	assert("edward-xxx", subv->bdev == NULL);
	assert("edward-xxx", subv->fiber == NULL);
	assert("edward-xxx", !subvol_is_set(subv, SUBVOL_ACTIVATED));
	assert("edward-xxx", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-xxx", list_empty_careful(&subv->ch.tx_list));
	assert("edward-xxx", list_empty_careful(&subv->ch.wander_map));

	if (subv->name)
		kfree(subv->name);
	kfree(subv);
}

/*
 * Called on shutdown
 */
void reiser4_unregister_volumes(void)
{
	struct reiser4_volume *vol;
	struct reiser4_subvol *sub;

	list_for_each_entry(vol, &reiser4_volumes, list) {
		list_for_each_entry(sub, &vol->subvols_list, list)
			reiser4_put_subvol(sub);
		reiser4_put_volume(vol);
	}
}

/*
 * Check for reiser4 signature on an off-line device specified by @path.
 * If found, then try to register a respective reiser4 subvolume.
 * If reiser4 was found, then return 0. Otherwise return error.
 */
int reiser4_scan_device(const char *path, fmode_t flags, void *holder)
{
	int ret = -EINVAL;
	struct block_device *bdev;
	struct page *page;
	struct reiser4_master_sb *master;

	mutex_lock(&reiser4_volumes_mutex);

	bdev = blkdev_get_by_path(path, flags, holder);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto out;
	}
	/*
	 * read master super block
	 */
	page = read_cache_page_gfp(bdev->bd_inode->i_mapping,
				   REISER4_MAGIC_OFFSET >> PAGE_SHIFT,
				   GFP_NOFS);
	if (IS_ERR_OR_NULL(page))
		goto bdev_put;
	master = kmap(page);
	if (strncmp(master->magic,
		    REISER4_SUPER_MAGIC_STRING,
		    sizeof(REISER4_SUPER_MAGIC_STRING)))
		/*
		 * there is no reiser4 on the device
		 */
		goto unmap;
	ret = reiser4_register_subvol(path,
			 master->uuid,
			 master->sub_uuid,
			 le16_to_cpu(get_unaligned(&master->dformat_pid)),
			 le16_to_cpu(get_unaligned(&master->volume_pid)),
			 le16_to_cpu(get_unaligned(&master->distrib_pid)),
			 le64_to_cpu(get_unaligned(&master->diskmap)),
			 master->stripe_size_bits);
	if (ret > 0)
		/* ok, it was registered earlier */
		ret = 0;
 unmap:
	kunmap(page);
	put_page(page);
 bdev_put:
	blkdev_put(bdev, flags);
 out:
	mutex_unlock(&reiser4_volumes_mutex);
	return ret;
}

struct file_system_type *get_reiser4_fs_type(void);

/*
 * Initialize disk format 4.X.Y for a subvolume
 * Pre-condition: subvolume @sub is registered
 */
static int reiser4_activate_subvol(struct super_block *super,
				   reiser4_subvol *subv, reiser4_vg_id vgid)
{
	int ret;
	fmode_t mode = FMODE_READ | FMODE_EXCL;

	assert("edward-xxx", !subvol_is_set(subv, SUBVOL_ACTIVATED));

	if (!(super->s_flags & MS_RDONLY))
		mode |= FMODE_WRITE;

	subv->bdev = blkdev_get_by_path(subv->name,
					mode, get_reiser4_fs_type());
	if (IS_ERR(subv->bdev))
		return PTR_ERR(subv->bdev);
	subv->mode = mode;
	subv->super = super;

	if (blk_queue_nonrot(bdev_get_queue(subv->bdev))) {
		/*
		 * Solid state drive has been detected.
		 * Set Write-Anywhere transaction model
		 * for this subvolume
		 */
		subv->flags |= (1 << SUBVOL_IS_NONROT_DEVICE);
		subv->txmod = WA_TXMOD_ID;
	}
	ret = subv->df_plug->init_format(super, subv, vgid);
	if (ret) {
		blkdev_put(subv->bdev, subv->mode);
		return ret;
	}
	ret = subv->df_plug->version_update(super, subv);
	if (ret) {
		subv->df_plug->release_format(super, subv);
		blkdev_put(subv->bdev, subv->mode);
		subv->bdev = NULL;
		return ret;
	}
	subv->flags |= (1 << SUBVOL_ACTIVATED);
	return 0;
}

static void *alloc_subvols_set(__u32 num_subvols)
{
	void *result;

	result = kzalloc(num_subvols * sizeof(result), GFP_NOFS);
	return result;
}

static void free_subvols_set(reiser4_subvol ***set)
{
	if (*set != NULL) {
		kfree(*set);
		*set = NULL;
	}
}

/**
 * Deactivate subvolume. Called during umount, or in error paths
 */
static void deactivate_subvol(struct super_block *super, reiser4_subvol *subv)
{
	assert("edward-xxx", subvol_is_set(subv, SUBVOL_ACTIVATED));
	assert("edward-xxx", subv->bdev != NULL);
	assert("edward-xxx", subv->super != NULL);
	assert("edward-xxx", get_super_volume(super)->num_subvols != 0);

	subv->df_plug->release_format(super, subv);

	assert("edward-xxx", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-xxx", list_empty_careful(&subv->ch.tx_list));
	assert("edward-xxx", list_empty_careful(&subv->ch.wander_map));

	blkdev_put(subv->bdev, subv->mode);
	subv->bdev = NULL;
	subv->super = NULL;
	clear_bit((int)SUBVOL_ACTIVATED, &subv->flags);
}

static void __reiser4_deactivate_volume(struct super_block *super)
{
	struct reiser4_subvol *subv;
	reiser4_volume *vol = get_super_private(super)->vol;

	list_for_each_entry(subv, &vol->subvols_list, list)
		if (subvol_is_set(subv, SUBVOL_ACTIVATED)) {
			subvol_check_block_counters(subv);
			deactivate_subvol(super, subv);
		}
	if (vol->aib) {
		assert("edward-xxx", vol->dist_plug->done != NULL);
		vol->dist_plug->done(vol->aib);
		vol->aib = NULL;
	}
	free_subvols_set(&vol->subvols);
}

/**
 * Deactivate all subvolumes except ones of the specified
 * volume group @vgid
 */
static void deactivate_subvolumes_except(struct super_block *super,
					 reiser4_vg_id vgid)
{
	struct reiser4_subvol *subv;
	reiser4_volume *vol = get_super_private(super)->vol;

	list_for_each_entry(subv, &vol->subvols_list, list)
		if (subvol_is_set(subv, SUBVOL_ACTIVATED) &&
		    subv->vgid != vgid) {
			subvol_check_block_counters(subv);
			deactivate_subvol(super, subv);
		}
}

/**
 * Deactivate volume starting from not mirrors
 */
void reiser4_deactivate_volume(struct super_block *super)
{
	mutex_lock(&reiser4_volumes_mutex);
	deactivate_subvolumes_except(super, REISER4_VG_MIRRORS);
	__reiser4_deactivate_volume(super);
	mutex_unlock(&reiser4_volumes_mutex);
}

/**
 * Set a registered subvolume @src to the array
 * of activated subvolumes @dst at the position
 * @dst_pos. Allocate the array, if needed.
 *
 */
static int set_activated_subvol(reiser4_subvol ***dst, u64 dst_pos,
				reiser4_subvol *src, u64 num_total)
{
	int ret = 0;

	if (*dst == NULL) {
		*dst = alloc_subvols_set(num_total);
		if (*dst == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if ((*dst)[dst_pos] != NULL) {
		warning("edward-xxx",
			"subvolumes %s and %s have the same id(%llu)",
			(*dst)[dst_pos]->name, src->name, src->id);
		ret = -EINVAL;
		goto out;
	}
	(*dst)[dst_pos] = src;
 out:
	return ret;
}

/**
 * Activate all subvolumes of the volume group @vgid
 *
 * This function is an "idempotent of high order". The order
 * of that idempotent is equal to the number of volume groups
 */
static int reiser4_activate_volume_group(struct super_block *super,
					 u8 *vol_uuid, reiser4_vg_id vgid,
					 u32 *nr_activated)
{
	int ret;
	struct reiser4_volume *vol;
	struct reiser4_subvol *subv;
	reiser4_super_info_data *info;

	*nr_activated = 0;
	info = get_super_private(super);

	vol = reiser4_search_volume(vol_uuid);
	if (!vol)
		return -EINVAL;

	info->vol = vol;
	vol->info = info;

	assert("edward-xxx", vol->aib == NULL);

	list_for_each_entry(subv, &vol->subvols_list, list) {
		if (subvol_is_set(subv, SUBVOL_ACTIVATED))
			continue;
		ret = reiser4_activate_subvol(super, subv, vgid);
		if (ret == -E_NOTEXP) {
			/*
			 * this subvolume will be activated later
			 */
			break;
		}
		if (ret)
			goto error;
		assert("edward-xxx", subvol_is_set(subv, SUBVOL_ACTIVATED));

		ret = set_activated_subvol(&vol->subvols,
					   subv->id,
					   subv,
					   vol->num_subvols);
		if (ret)
			goto error;
		(*nr_activated) ++;
	}
	if (vol->num_subvols == 1) {
		/*
		 * this is a simple (not compound) volume,
		 * therefore, managed by trivial plugins
		 */
		vol->dist_plug = distribution_plugin_by_id(NONE_DISTRIB_ID);
		vol->vol_plug = volume_plugin_by_id(TRIV_VOLUME_ID);
	}
	/*
	 * initialize aib descriptor after activating all subvolumes
	 */
	if (vol->dist_plug->init != NULL) {
		ret = vol->dist_plug->init(vol,
					   vol->num_subvols,
					   vol->num_sgs_bits,
					   &vol->vol_plug->aib_ops,
					   &vol->aib);
		if (ret) {
			warning("edward-xxx",
				"(%s): failed to init distribution (%d)\n",
				super->s_id, ret);
			goto error;
		}
	}
	/*
	 * release fibers, which are not needed for regular operations
	 */
	list_for_each_entry(subv, &vol->subvols_list, list)
		reiser4_fiber_done(subv);
	return 0;
 error:
	__reiser4_deactivate_volume(super);
	return ret;
}

/*
 * Initialize disk formats of all subvolumes.
 */
int reiser4_activate_volume(struct super_block *super, u8 *vol_uuid)
{
	int ret;
	u32 nr;

	mutex_lock(&reiser4_volumes_mutex);
	/*
	 * First we activate mirrors to have a complete set
	 * of active mirrors before the journal replay procedure
	 * invoked for non-mirrors.
	 */
	ret = reiser4_activate_volume_group(super, vol_uuid,
					    REISER4_VG_MIRRORS, &nr);
	if (ret)
		goto out;
	if (nr)
		notice("", "activated %u mirrors of %s",
		       nr, super_origin(super)->name);

	ret = reiser4_activate_volume_group(super, vol_uuid,
					    REISER4_VG_ALL, &nr);
 out:
	mutex_unlock(&reiser4_volumes_mutex);
	return ret;
}


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
