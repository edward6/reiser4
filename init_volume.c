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

#define MAX_STRIPE_BITS (63)

static struct reiser4_volume *reiser4_alloc_volume(u8 *uuid,
						   int vol_pid,
						   int dist_pid,
						   int stripe_bits)
{
	struct reiser4_volume *vol;

	if (stripe_bits != 0 &&
	    stripe_bits < PAGE_SHIFT &&
	    stripe_bits > MAX_STRIPE_BITS) {
		warning("edward-1814",
			"bad stripe_bits (%d)n", stripe_bits);
		return NULL;
	}
	vol = kzalloc(sizeof(*vol), GFP_KERNEL);
	if (!vol)
		return NULL;
	memcpy(vol->uuid, uuid, 16);
	INIT_LIST_HEAD(&vol->list);
	INIT_LIST_HEAD(&vol->subvols_list);
	atomic_set(&vol->nr_origins, 0);
	vol->vol_plug = volume_plugin_by_unsafe_id(vol_pid);
	vol->dist_plug = distribution_plugin_by_unsafe_id(dist_pid);
	vol->stripe_bits = stripe_bits;
	return vol;
}

struct reiser4_subvol *reiser4_alloc_subvol(u8 *uuid, const char *path,
					    int dformat_pid,
					    u16 mirror_id,
					    u16 num_replicas)
{
	struct reiser4_subvol *subv;

	subv = kzalloc(sizeof(*subv), GFP_KERNEL);
	if (!subv)
		return NULL;
	memcpy(subv->uuid, uuid, 16);

	INIT_LIST_HEAD(&subv->list);
	__init_ch_sub(&subv->ch);

	subv->df_plug = disk_format_plugin_by_unsafe_id(dformat_pid);
	if (subv->df_plug == NULL) {
		warning("edward-1738",
			"%s: unknown disk format plugin %d\n",
			path, dformat_pid);
		kfree(subv);
		return NULL;
	}
	subv->name = kstrdup(path, GFP_KERNEL);
	if (!subv->name) {
		kfree(subv);
		return NULL;
	}
	subv->mirror_id = mirror_id;
	subv->num_replicas = num_replicas;
	if (subv->mirror_id > subv->num_replicas) {
		warning("edward-1739",
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
				   u16 mirror_id, u16 num_replicas,
				   int stripe_bits, reiser4_subvol **result,
				   reiser4_volume **vol)
{
	struct reiser4_subvol *sub;

	assert("edward-1964", vol != NULL);

	*vol = reiser4_search_volume(vol_uuid);
	if (*vol) {
		sub = reiser4_search_subvol(sub_uuid, &(*vol)->subvols_list);
		if (sub) {
			if (result)
				*result = sub;
			return 1;
		}
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   mirror_id, num_replicas);
		if (!sub)
			return -ENOMEM;
	} else {
		*vol = reiser4_alloc_volume(vol_uuid, vol_pid, dist_pid,
					    stripe_bits);
		if (*vol == NULL)
			return -ENOMEM;
		sub = reiser4_alloc_subvol(sub_uuid, path, dformat_pid,
					   mirror_id, num_replicas);
		if (!sub) {
			kfree(*vol);
			return -ENOMEM;
		}
		list_add(&(*vol)->list, &reiser4_volumes);
	}
	list_add(&sub->list, &(*vol)->subvols_list);
	if (result)
		*result = sub;
	notice("edward-1932", "registered subvolume (%s)\n", path);
	return 0;
}

static void reiser4_free_volume(struct reiser4_volume *vol)
{
	assert("edward-1741", vol->conf == NULL);
	kfree(vol);
}

void reiser4_unregister_subvol(struct reiser4_subvol *subv)
{
	assert("edward-1742", subv->bdev == NULL);
	assert("edward-1743", subv->fiber == NULL);
	assert("edward-1744", !subvol_is_set(subv, SUBVOL_ACTIVATED));
	assert("edward-1745", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-1746", list_empty_careful(&subv->ch.tx_list));
	assert("edward-1747", list_empty_careful(&subv->ch.wander_map));
	/*
	 * remove subvolume from volume's subvols_list
	 */
	list_del_init(&subv->list);
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
			reiser4_unregister_subvol(sub);
		reiser4_free_volume(vol);
	}
}

/*
 * Check for reiser4 signature on an off-line device specified by @path.
 * If found, then try to register a respective reiser4 subvolume.
 * On success store pointer to registered subvolume in @result.
 * If reiser4 was found, then return 0. Otherwise return error.
 */
int reiser4_scan_device(const char *path, fmode_t flags, void *holder,
			reiser4_subvol **result, reiser4_volume **host)
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
				   GFP_KERNEL);
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
				      master_get_mirror_id(master),
				      master_get_num_replicas(master),
				      master_get_stripe_bits(master),
				      result, host);
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

/**
 * Make sure that all replicas of the original subvolume
 * @subv has been activated.
 *
 * Pre-conditions:
 * Disk format superblock of the subvolume was found
 */
int check_active_replicas(reiser4_subvol *subv)
{
	u32 repl_id;
	lv_conf *conf;

	assert("edward-2235", subv->super != NULL);
	assert("edward-1748", !is_replica(subv));
	assert("edward-1751", super_volume(subv->super) != NULL);
	assert("edward-1749", super_nr_origins(subv->super) != 0);

	conf = super_conf(subv->super);

	if (has_replicas(subv) &&
	    (conf == NULL || conf_mslot_at(conf, subv->id) == NULL)) {

		warning("edward-1750",
			"%s requires replicas, which "
			" are not registered.",
			subv->name);
		return -EINVAL;
	}

	__for_each_replica(subv, repl_id) {
		reiser4_subvol *repl;

		repl = super_mirror(subv->super, subv->id, repl_id);
		if (repl == NULL) {
			warning("edward-1752",
				"%s: replica #%u is not registered.",
				subv->name, repl_id);
			return -EINVAL;
		}
		assert("edward-1965",
		       subvol_is_set(repl, SUBVOL_ACTIVATED));
	}
	return 0;
}

static void clear_subvol(reiser4_subvol *subv)
{
	subv->bdev = NULL;
	subv->super = NULL;
	subv->mode = 0;
	subv->flags = 0;
	subv->txmod = 0;
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

	if (subvol_is_set(subv, SUBVOL_ACTIVATED)) {
		warning("edward-1933", "Brick %s is busy", subv->name);
		return -EINVAL;
	}
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
	/*
	 * Read format super-block of the subvolume and	perform early
	 * initialisations needed by check_active_replicas().
	 * Note that it may be not the most recent version of format
	 * super-block, since the journal hasn't been yet replayed.
	 *
	 * FIXME-EDWARD: Provide a guarantee that on-disk data needed
	 * for that early initialisation (in particular, nr_origins)
	 * are really actual.
	 */
	page = subv->df_plug->find_format(subv);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto error;
	}
	put_page(page);

	if (is_replica(subv))
		/*
		 * nothing to do any more for replicas,
		 */
		goto ok;
	/*
	 * This is an original subvolume.
	 * Before calling ->init_format() make sure that
	 * all its replicas were activated.
	 */
	ret = check_active_replicas(subv);
	if (ret)
		goto error;
	ret = subv->df_plug->init_format(super, subv);
	if (ret)
		goto error;
	ret = subv->df_plug->version_update(super, subv);
	if (ret) {
		subv->df_plug->release_format(super, subv);
		goto error;
	}
 ok:
	subv->flags |= (1 << SUBVOL_ACTIVATED);
	return 0;
 error:
	blkdev_put(subv->bdev, subv->mode);
	clear_subvol(subv);
	return ret;
}

mirror_t *alloc_mslot(u32 nr_mirrors)
{
	return kzalloc(nr_mirrors * sizeof(mirror_t), GFP_KERNEL);
}

void free_mslot(slot_t slot)
{
	assert("edward-2229", slot != NULL);
	kfree(slot);
}

lv_conf *alloc_lv_conf(u32 nr_mslots)
{
	lv_conf *ret;

	ret = kzalloc(sizeof(*ret) + nr_mslots * sizeof(slot_t),
		      GFP_KERNEL);
	if (ret)
		ret->nr_mslots = nr_mslots;
	return ret;
}

void free_lv_conf(lv_conf *conf)
{
	if (conf != NULL)
		kfree(conf);
}

void free_mslot_at(lv_conf *conf, int idx)
{
	assert("edward-2231", conf != NULL);
	assert("edward-2190", conf->mslots[idx] != NULL);

	free_mslot(conf->mslots[idx]);
	conf->mslots[idx] = NULL;
}

void release_lv_conf(reiser4_volume *vol)
{
	lv_conf *conf;

	assert("edward-1754", vol != NULL);

	conf = vol->conf;

	if (conf != NULL) {
		u32 i;
		for (i = 0; i < conf->nr_mslots; i++)
			if (conf->mslots[i])
				free_mslot_at(conf, i);
		free_lv_conf(conf);
		vol->conf = NULL;
	}
}

/**
 * Deactivate subvolume. Called during umount, or in error paths
 */
void reiser4_deactivate_subvol(struct super_block *super, reiser4_subvol *subv)
{
	assert("edward-1755", subvol_is_set(subv, SUBVOL_ACTIVATED));
	assert("edward-1756", subv->bdev != NULL);
	assert("edward-1757", subv->super != NULL);

	if (!is_replica(subv)) {
		subvol_check_block_counters(subv);
		subv->df_plug->release_format(super, subv);
	}
	assert("edward-1758", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-1759", list_empty_careful(&subv->ch.tx_list));
	assert("edward-1760", list_empty_careful(&subv->ch.wander_map));

	blkdev_put(subv->bdev, subv->mode);
	clear_subvol(subv);
	clear_bit(SUBVOL_ACTIVATED, &subv->flags);
}

static void deactivate_subvolumes_of_type(struct super_block *super,
					  reiser4_subv_type type)
{
	struct reiser4_subvol *subv;
	reiser4_volume *vol = get_super_private(super)->vol;

	list_for_each_entry(subv, &vol->subvols_list, list) {
		if (!subvol_is_set(subv, SUBVOL_ACTIVATED)) {
			/*
			 * subvolume is not active
			 */
			assert("edward-1761", subv->super == NULL);
			continue;
		}
		if (subv->type != type)
			/*
			 * subvolume will be deactivated later
			 */
			continue;
		reiser4_deactivate_subvol(super, subv);
	}
}

/**
 * First we deactivate all non-replicas, as we need to have
 * a complete set of active replicas for journal replay when
 * deactivating original subvolumes.
 */
void __reiser4_deactivate_volume(struct super_block *super)
{
	reiser4_subvol *subv;
	reiser4_volume *vol = super_volume(super);

	deactivate_subvolumes_of_type(super, REISER4_SUBV_ORIGIN);
	deactivate_subvolumes_of_type(super, REISER4_SUBV_REPLICA);

	if (vol->dist_plug->r.done)
		vol->dist_plug->r.done(&vol->aid);

	release_lv_conf(vol);
	vol->num_sgs_bits = 0;
	atomic_set(&vol->nr_origins, 0);

	list_for_each_entry(subv, &vol->subvols_list, list) {
		assert("edward-1763", !subvol_is_set(subv, SUBVOL_ACTIVATED));
		assert("edward-1764", subv->super == NULL);
		assert("edward-1765", subv->bdev == NULL);
		assert("edward-1766", subv->mode == 0);
	}
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
 * Set a pointer to registered subvolume @subv to the respective
 * cell in the matrix of activated subvolumes of logical volume @vol.
 * Allocate column of the matrix, if needed.
 */
static int set_activated_subvol(reiser4_volume *vol, reiser4_subvol *subv)
{
	int ret = 0;
	u64 orig_id = subv->id;
	u16 mirr_id = subv->mirror_id;
	lv_conf *conf = vol->conf;

	assert("edward-2232", conf != NULL);

	if (conf->mslots[orig_id] == NULL) {
		/*
		 * allocate array of pointers to mirrors
		 */
		conf->mslots[orig_id] = alloc_mslot(1 + subv->num_replicas);
		if (conf->mslots[orig_id] == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (conf->mslots[orig_id][mirr_id] != NULL) {
		warning("edward-1767",
			"%s and %s have identical mirror IDs (%llu,%u)",
			conf->mslots[orig_id][mirr_id]->name,
			subv->name,
			orig_id, mirr_id);
		ret = -EINVAL;
		goto out;
	}
	conf->mslots[orig_id][mirr_id] = subv;
 out:
	return ret;
}

/**
 * Activate all subvolumes of the specified @type
 *
 * This function is an idempotent: being called second
 * time with the same @type it won't have any effect.
 */
static int activate_subvolumes_of_type(struct super_block *super,
				       u8 *vol_uuid, reiser4_subv_type type)
{
	int ret;
	struct reiser4_volume *vol;
	struct reiser4_subvol *subv;
	reiser4_super_info_data *info;

	vol = reiser4_search_volume(vol_uuid);
	if (!vol)
		return -EINVAL;
	info = get_super_private(super);
	info->vol = vol;

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
		assert("edward-1769", subvol_is_set(subv, SUBVOL_ACTIVATED));

		ret = set_activated_subvol(vol, subv);
		if (ret)
			goto error;
	}
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
	u32 nr_origins = 0;
	reiser4_volume *vol;
	lv_conf *conf;

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
					  REISER4_SUBV_ORIGIN);
	if (ret)
		goto deactivate;
	/*
	 * At this point all activated original bricks have
	 * complete sets of active replicas (because we called
	 * check_active_replicas()). Now make sure that all
	 * original bricks have been activated.
	 */

	vol = get_super_private(super)->vol;
	assert("edward-2207", vol!= NULL);

	conf = vol->conf;
	for_each_mslot(conf, orig_id) {
		if (conf_mslot_at(conf, orig_id) && conf_origin(conf, orig_id)){
			assert("edward-1773",
			       subvol_is_set(conf_origin(conf, orig_id),
					     SUBVOL_ACTIVATED));
			nr_origins ++;
		}
	}
	if (nr_origins != atomic_read(&vol->nr_origins)) {
		warning("edward-1772",
			"%s: not all bricks are registered (%u instead of %u)",
			super->s_id, nr_origins, atomic_read(&vol->nr_origins));
		ret = -EINVAL;
		goto deactivate;
	}
	/*
	 * initialize logical volume after activating all subvolumes
	 */
	if (vol->vol_plug->init_volume != NULL) {
		ret = vol->vol_plug->init_volume(vol);
		if (ret) {
			warning("edward-1770",
				"(%s): failed to init logical volume (%d)\n",
				super->s_id, ret);
			goto deactivate;
		}
	}
	reiser4_volume_set_activated(super);
	goto out;
 deactivate:
	__reiser4_deactivate_volume(super);
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
