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
 * Logical volume is a set of subvolumes.
 * Every subvolume represents a physical or logical (built by LVM
   means) device, formatted by a plugin of REISER4_FORMAT_PLUGIN_TYPE.
 * All subvolumes are combined in "volume groups".
 * Volume plugin defines distribution among those groups.
 * Distribution plugin defines distribution within a single group.
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

static struct reiser4_subvol *reiser4_alloc_subvol(u8 *uuid, const char *path,
						   int dformat_pid,
						   u64 diskmap,
						   u16 mirror_id,
						   u16 num_replicas)
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
	subv->mirror_id = mirror_id;
	subv->num_replicas = num_replicas;
	if (subv->mirror_id > subv->num_replicas) {
		warning("edward-xxx",
		     "%s: mirror id (%u) larger than number of replicas (%u)",
		     path, subv->mirror_id, subv->num_replicas);
		kfree(subv->name);
		kfree(subv);
		return NULL;
	}
	if (is_replica(subv))
		subv->type = REISER4_SUBV_REPLICA;
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
static int reiser4_register_subvol(const char *path,
				   u8 *vol_uuid, u8 *sub_uuid,
				   int dformat_pid, int vol_pid, int dist_pid,
				   u64 diskmap, u16 mirror_id, u16 num_replicas,
				   int stripe_bits)
{
	struct reiser4_volume *vol;
	struct reiser4_subvol *sub;

	vol = reiser4_search_volume(vol_uuid);
	if (vol) {
		sub = reiser4_search_subvol(sub_uuid, &vol->subvols_list);
		if (sub)
			return 1;
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   diskmap, mirror_id, num_replicas);
		if (!sub)
			return -ENOMEM;
	} else {
		vol = reiser4_alloc_volume(vol_uuid, vol_pid, dist_pid,
					   stripe_bits);
		if (!vol)
			return -ENOMEM;
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   diskmap, mirror_id, num_replicas);
		if (!sub) {
			kfree(vol);
			return -ENOMEM;
		}
		list_add(&vol->list, &reiser4_volumes);
	}
	list_add(&sub->list, &vol->subvols_list);
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
				      master_get_dformat_pid(master),
				      master_get_volume_pid(master),
				      master_get_distrib_pid(master),
				      master_get_diskmap_loc(master),
				      master_get_mirror_id(master),
				      master_get_num_replicas(master),
				      master_get_stripe_bits(master));
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

int check_active_replicas(reiser4_subvol *subv)
{
	u32 repl_id;
	assert("edward-xxx", !is_replica(subv));

	if ((super_num_origins(subv->super) == 0) ||
	    (super_volume(subv->super)->subvols == NULL) ||
	    (super_volume(subv->super)->subvols[subv->id] == NULL)) {

		warning("edward-xxx",
			"%s requires replicas, which "
			" are not registered.",
			subv->name);
		return -EINVAL;
	}
	assert("edward-xxx", super_volume(subv->super) != NULL);

	for_each_replica(subv->id, repl_id) {
		reiser4_subvol *repl;
		repl = super_mirror(subv->super, subv->id, repl_id);
		if (repl == NULL) {
			warning("edward-xxx",
				"%s requires replica No%u, which "
				" is not registered.",
				subv->name, repl_id);
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * Initialize disk format 4.X.Y for a subvolume
 * Pre-condition: subvolume @sub is registered
 */
int reiser4_activate_subvol(struct super_block *super,
				   reiser4_subvol *subv)
{
	int ret;
	struct page *page;
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
	page = subv->df_plug->find_format(subv, 1);
	if (IS_ERR(page)) {
		blkdev_put(subv->bdev, subv->mode);
		return PTR_ERR(page);
	}
	put_page(page);
	if (is_replica(subv))
		/*
		 * nothing to do any more for replicas,
		 */
		goto exit;
	/*
	 * Make sure that all replicas were activated
	 */
	ret = check_active_replicas(subv);
	if (ret)
		return ret;
	ret = subv->df_plug->init_format(super, subv);
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
 exit:
	subv->flags |= (1 << SUBVOL_ACTIVATED);
	return 0;
}

static void *alloc_subvols_set(__u32 num_subvols)
{
	void *result;

	result = kzalloc(num_subvols * sizeof(result), GFP_NOFS);
	return result;
}

static void free_subvols_set(reiser4_volume *vol)
{
	assert("edward-xxx", vol != NULL);

	if (vol->subvols != NULL) {
		kfree(vol->subvols);
		vol->subvols = NULL;
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

	if (!is_replica(subv)) {
		subvol_check_block_counters(subv);
		subv->df_plug->release_format(super, subv);
	}
	assert("edward-xxx", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-xxx", list_empty_careful(&subv->ch.tx_list));
	assert("edward-xxx", list_empty_careful(&subv->ch.wander_map));

	blkdev_put(subv->bdev, subv->mode);
	subv->bdev = NULL;
	subv->super = NULL;
	clear_bit((int)SUBVOL_ACTIVATED, &subv->flags);
}

static void deactivate_subvolumes_of_type(struct super_block *super,
					  reiser4_subv_type type)
{
	struct reiser4_subvol *subv;
	reiser4_volume *vol = get_super_private(super)->vol;

	list_for_each_entry(subv, &vol->subvols_list, list) {
		if (!subvol_is_set(subv, SUBVOL_ACTIVATED))
			/*
			 * subvolume is not active
			 */
			continue;
		if (subv->type != type)
			/*
			 * subvolume will be deactivated later
			 */
			continue;
		deactivate_subvol(super, subv);
	}
}

/**
 * First we deactivate all non-replicas, as we need to have
 * a complete set of active replicas for journal replay when
 * deactivating original subvolumes.
 */
void __reiser4_deactivate_volume(struct super_block *super)
{
	reiser4_volume *vol = super_volume(super);

	deactivate_subvolumes_of_type(super, REISER4_SUBV_OTHER);
	deactivate_subvolumes_of_type(super, REISER4_SUBV_REPLICA);

	if (vol->aib) {
		assert("edward-xxx", vol->dist_plug->done != NULL);
		vol->dist_plug->done(vol->aib);
		vol->aib = NULL;
	}
	free_subvols_set(vol);
}

/**
 * Deactivate volume. Called during umount, or in error paths
 */
void reiser4_deactivate_volume(struct super_block *super)
{
	mutex_lock(&reiser4_volumes_mutex);
	__reiser4_deactivate_volume(super);
	mutex_unlock(&reiser4_volumes_mutex);
}

/**
 * Set a registered subvolume @subv to the table
 * of activated subvolumes of logical volume @vol.
 * Allocate arrays of pointers, if needed.
 */
static int set_activated_subvol(reiser4_volume *vol, reiser4_subvol *subv)
{
	int ret = 0;
	u64 orig_id = subv->id;
	u16 mirr_id = subv->mirror_id;

	if (vol->subvols == NULL) {
		/*
		 * allocate set for original subvolumes
		 */
		vol->subvols = alloc_subvols_set(vol->num_origins);
		if (vol->subvols == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (vol->subvols[orig_id] == NULL) {
		/*
		 * allocate set for mirrors
		 */
		vol->subvols[orig_id] =
			alloc_subvols_set(1 + subv->num_replicas);
		if (vol->subvols[orig_id] == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (vol->subvols[orig_id][mirr_id] != NULL) {
		warning("edward-xxx",
			"%s and %s have the same (id,mirror_id)=(%llu,%u)",
			vol->subvols[orig_id][mirr_id]->name,
			subv->name,
			orig_id, mirr_id);
		ret = -EINVAL;
		goto out;
	}
	vol->subvols[orig_id][mirr_id] = subv;
 out:
	return ret;
}

/**
 * Activate all subvolumes of the specified @type
 *
 * This function is an "idempotent of high order". The order
 * of that idempotent is equal to the number of volume groups
 */
static int activate_subvolumes_of_type(struct super_block *super,
				       u8 *vol_uuid, reiser4_subv_type type)
{
	int ret;
	struct reiser4_volume *vol;
	struct reiser4_subvol *subv;
	reiser4_super_info_data *info;

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
		if (subv->type != type)
			/*
			 * this subvolume will be activated later
			 */
			continue;
		ret = reiser4_activate_subvol(super, subv);
		if (ret)
			goto error;
		assert("edward-xxx", subvol_is_set(subv, SUBVOL_ACTIVATED));

		ret = set_activated_subvol(vol, subv);
		if (ret)
			goto error;
	}
	if (vol->num_origins == 1) {
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
					   vol->num_origins,
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

/**
 * Activate all subvolumes-components of a logical volume.
 * Handle all cases of incomplete registration (when not all
 * components were registered in the system).
 *
 * @super: super-block associated with the logical volume;
 * @vol_uuid: uuid of the logical volume.
 */
int reiser4_activate_volume(struct super_block *super, u8 *vol_uuid)
{
	int ret;
	u32 orig_id;

	mutex_lock(&reiser4_volumes_mutex);
	/*
	 * First we activate all replicas, as we need to have a
	 * complete set of active replicas for journal replay
	 * when activating original subvolumes.
	 */
	ret = activate_subvolumes_of_type(super, vol_uuid,
					  REISER4_SUBV_REPLICA);
	if (ret)
		goto out;
	ret = activate_subvolumes_of_type(super, vol_uuid,
					  REISER4_SUBV_OTHER);
	if (ret)
		goto out;
	/*
	 * Make sure that all origins were activated
	 */
	if (current_num_origins() == 0) {
		warning("edward-xxx",
			"%s requires at least one origin, which is not "
			"registered.", super->s_id);
		ret = -EINVAL;
		goto out;
	}
	for_each_origin(orig_id) {
		reiser4_subvol *orig;
		orig = super_origin(super, orig_id);
		if (orig == NULL) {
			warning("edward-xxx",
				"%s requires origin No%u, which is not "
				"registered.", super->s_id, orig_id);
			ret = -EINVAL;
			goto out;
		}
		assert("edward-xxx",
		       subvol_is_set(orig, SUBVOL_ACTIVATED));
	}
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
