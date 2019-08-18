/*
  Copyright (c) 2014-2019 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* Reiser4 logical volume initialization and activation */

#include "debug.h"
#include "super.h"
#include "plugin/item/brick_symbol.h"
#include "plugin/volume/volume.h"
#include <linux/blkdev.h>

DEFINE_MUTEX(reiser4_volumes_mutex);
static LIST_HEAD(reiser4_volumes); /* list of registered volumes */

#define MAX_STRIPE_BITS (63)

/**
 * Allocate and initialize a volume header.
 *
 * @uuid: unique volume ID;
 * @vol_plug: volume plugin this volume is managed by;
 * @dist_plug: plugin distributing stripes among bricks;
 * @stripe_bits: defines size of stripe (minimal unit of distribution).
 */
static reiser4_volume *reiser4_alloc_volume(u8 *uuid,
					    volume_plugin *vol_plug,
					    distribution_plugin *dist_plug,
					    int stripe_bits)
{
	struct reiser4_volume *vol;

	vol = kzalloc(sizeof(*vol), GFP_KERNEL);
	if (!vol)
		return NULL;
	memcpy(vol->uuid, uuid, 16);
	vol->vol_plug = vol_plug;
	vol->dist_plug = dist_plug;
	vol->stripe_bits = stripe_bits;

	INIT_LIST_HEAD(&vol->list);
	INIT_LIST_HEAD(&vol->subvols_list);
	atomic_set(&vol->custom_brick_id, METADATA_SUBVOL_ID);
	atomic_set(&vol->nr_origins, 0);
	return vol;
}

/**
 * Allocate and initialize a brick header.
 *
 * @df_plug: disk format plugin this brick is managed by;
 * @uuid: bricks's external ID;
 * @subvol_id: bricks's internal ID;
 * @mirror_id: 0 if brick is original. Serial number of replica otherwise;
 * @num_replicas: total number of replicas
 */
struct reiser4_subvol *reiser4_alloc_subvol(u8 *uuid,
					    const char *path,
					    disk_format_plugin *df_plug,
					    u64 subvol_id,
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

	subv->name = kstrdup(path, GFP_KERNEL);
	if (!subv->name) {
		kfree(subv);
		return NULL;
	}
	subv->df_plug = df_plug;
	subv->id = subvol_id;
	subv->mirror_id = mirror_id;
	subv->num_replicas = num_replicas;
	return subv;
}

/**
 * Lookup volume by its ID.
 * Pre-condition: @reiser4_volumes_mutex is down
 */
struct reiser4_volume *reiser4_search_volume(u8 *uuid)
{
	struct reiser4_volume *vol;

	list_for_each_entry(vol, &reiser4_volumes, list) {
		if (memcmp(uuid, vol->uuid, 16) == 0)
			return vol;
	}
	return NULL;
}

/**
 * Lookup brick by its external ID.
 * Pre-condition: @reiser4_volumes_mutex is down
 */
static reiser4_subvol *reiser4_search_subvol(u8 *uuid,
						    struct list_head *where)
{
	reiser4_subvol *sub;

	list_for_each_entry(sub, where, list) {
		if (memcmp(uuid, sub->uuid, 16) == 0)
			return sub;
	}
	return NULL;
}

static int check_volume_params(reiser4_volume *vol,
			       volume_plugin *vol_plug,
			       distribution_plugin *dist_plug,
			       int stripe_bits,
			       const char **what_differs)
{
	int ret = -EINVAL;

	if (vol->vol_plug != vol_plug)
		*what_differs = "volume plugins";
	else if (vol->dist_plug != dist_plug)
		*what_differs = "distribution plugins";
	else if (vol->stripe_bits != stripe_bits)
		*what_differs = "stripe sizes";
	else
		ret = 0;
	return ret;
}

/**
 * Register a brick.
 * Returns:
 * 0   - first time subvolume is seen
 * 1   - subvolume already registered
 * < 0 - error
 *
 * Pre-condition: @reiser4_volumes_mutex is down,
 * all passed volume parameters are valid.
 */
static int reiser4_register_subvol(const char *path,
				   u8 *vol_uuid,
				   u8 *sub_uuid,
				   disk_format_plugin *df_plug,
				   volume_plugin *vol_plug,
				   distribution_plugin *dist_plug,
				   u16 mirror_id,
				   u16 num_replicas,
				   int stripe_bits,
				   u64 subvol_id,
				   reiser4_subvol **result,
				   reiser4_volume **vol)
{
	const char *what_differs;
	struct reiser4_subvol *sub;

	assert("edward-1964", vol != NULL);

	*vol = reiser4_search_volume(vol_uuid);
	if (*vol) {
		int ret = check_volume_params(*vol,
					      vol_plug,
					      dist_plug,
					      stripe_bits,
					      &what_differs);
		if (ret) {
			/*
			 * Found, but not happy.
			 * Most likely it is because user specified
			 * wrong options when formatting bricks.
			 */
			warning("edward-2317",
				"%s: bricks w/ different %s in the same volume",
				path, what_differs);
			return ret;
		}
		sub = reiser4_search_subvol(sub_uuid, &(*vol)->subvols_list);
		if (sub) {
			if (result)
				*result = sub;
			return 1;
		}
		sub = reiser4_alloc_subvol(sub_uuid,
					   path,
					   df_plug,
					   subvol_id,
					   mirror_id, num_replicas);
		if (!sub)
			return -ENOMEM;
	} else {
		*vol = reiser4_alloc_volume(vol_uuid,
					    vol_plug,
					    dist_plug,
					    stripe_bits);
		if (*vol == NULL)
			return -ENOMEM;
		sub = reiser4_alloc_subvol(sub_uuid,
					   path,
					   df_plug,
					   subvol_id,
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
	notice("edward-1932", "brick %s has been registered", path);
	return 0;
}

static void reiser4_free_volume(struct reiser4_volume *vol)
{
	assert("edward-1741", vol->conf == NULL);
	kfree(vol);
}

/**
 * Retrieve information about a registered volume.
 * This is a REISER4_SCAN_DEV ioctl handler.
 */
int reiser4_volume_header(struct reiser4_vol_op_args *args)
{
	int idx = 0;
	const struct reiser4_volume *vol;
	const struct reiser4_volume *this = NULL;

	mutex_lock(&reiser4_volumes_mutex);

	list_for_each_entry(vol, &reiser4_volumes, list) {
		if (idx == args->s.vol_idx) {
			this = vol;
			break;
		}
		idx ++;
	}
	if (!this) {
		mutex_unlock(&reiser4_volumes_mutex);
		args->error = -ENOENT;
		return 0;
	}
	memcpy(args->u.vol.id, this->uuid, 16);
	if (this->conf)
		args->u.vol.fs_flags |= (1 << REISER4_ACTIVATED_VOL);

	mutex_unlock(&reiser4_volumes_mutex);
	return 0;
}

/**
 * Retrieve information about a registered brick.
 * This is a REISER4_SCAN_DEV ioctl handler.
 *
 * Pre-condition: @args contains uuid of the host volume and
 * serial number of the brick in the list of volume's bricks.
 */
int reiser4_brick_header(struct reiser4_vol_op_args *args)
{
	int idx = 0;
	const reiser4_volume *vol;
	const reiser4_subvol *subv;
	const reiser4_subvol *this = NULL;

	mutex_lock(&reiser4_volumes_mutex);
	vol = reiser4_search_volume(args->u.vol.id);
	if (!vol) {
		mutex_unlock(&reiser4_volumes_mutex);
		args->error = -EINVAL;
		return 0;
	}
	list_for_each_entry(subv, &vol->subvols_list, list) {
		if (idx == args->s.brick_idx) {
			this = subv;
			break;
		}
		idx ++;
	}
	if (!this) {
		mutex_unlock(&reiser4_volumes_mutex);
		args->error = -ENOENT;
		return 0;
	}
	memcpy(args->u.brick.ext_id, this->uuid, 16);
	strncpy(args->d.name, this->name, strlen(this->name));
	mutex_unlock(&reiser4_volumes_mutex);
	return 0;
}

/**
 * Remove @subv from volume's list of registered subvolumes and release it.
 * Pre-condition: @reiser4_volumes_mutex is down.
 */
static void unregister_subvol_locked(struct reiser4_subvol *subv)
{
	assert("edward-1742", subv->bdev == NULL);
	assert("edward-1743", subv->apx == NULL);
	assert("edward-1744", !subvol_is_set(subv, SUBVOL_ACTIVATED));
	assert("edward-1745", list_empty_careful(&subv->ch.overwrite_set));
	assert("edward-1746", list_empty_careful(&subv->ch.tx_list));
	assert("edward-1747", list_empty_careful(&subv->ch.wander_map));

	notice("edward-2312", "brick %s has been unregistered", subv->name);

	list_del_init(&subv->list);
	if (subv->name)
		kfree(subv->name);
	kfree(subv);
}

/**
 * Find a brick in the set of registered bricks, remove it
 * from the set and release it.
 *
 * This is called when removing brick form a logical volume,
 * and on error paths.
 */
void reiser4_unregister_subvol(struct reiser4_subvol *victim)
{
	struct reiser4_volume *vol;

	mutex_lock(&reiser4_volumes_mutex);

	list_for_each_entry(vol, &reiser4_volumes, list) {
		struct reiser4_subvol *subv;
		list_for_each_entry(subv, &vol->subvols_list, list) {
			if (subv == victim) {
				unregister_subvol_locked(subv);
				if (list_empty(&vol->subvols_list)) {
					list_del(&vol->list);
					reiser4_free_volume(vol);
				}
				goto out;
			}
		}
	}
 out:
	mutex_unlock(&reiser4_volumes_mutex);
}

/**
 * Find a brick in the list of registered bricks by name,
 * remove it from the list and release it.
 *
 * This is a REISER4_SCAN_DEV ioctl handler.
 */
int reiser4_unregister_brick(struct reiser4_vol_op_args *args)
{
	int ret = 0;
	struct reiser4_volume *vol;

	mutex_lock(&reiser4_volumes_mutex);

	list_for_each_entry(vol, &reiser4_volumes, list) {
		struct reiser4_subvol *subv;
		list_for_each_entry(subv, &vol->subvols_list, list) {
			if (!strncmp(args->d.name,
				     subv->name, strlen(subv->name))) {
				if (subvol_is_set(subv, SUBVOL_ACTIVATED)) {
					warning("edward-2314",
					"Can not unregister activated brick %s",
						subv->name);
					ret = -EINVAL;
					goto out;
				}
				unregister_subvol_locked(subv);
				if (list_empty(&vol->subvols_list)) {
					list_del(&vol->list);
					reiser4_free_volume(vol);
				}
				goto out;
			}
		}
	}
	warning("edward-2313",
		"Can not find registered brick %s", args->d.name);
	ret = -EINVAL;
 out:
	mutex_unlock(&reiser4_volumes_mutex);
	return ret;
}

/*
 * Called on shutdown
 */
void reiser4_unregister_volumes(void)
{
	struct reiser4_volume *vol;
	struct reiser4_subvol *sub;

	mutex_lock(&reiser4_volumes_mutex);

	list_for_each_entry(vol, &reiser4_volumes, list) {
		list_for_each_entry(sub, &vol->subvols_list, list)
			unregister_subvol_locked(sub);
		assert("edward-2328", list_empty(&vol->subvols_list));
		list_del(&vol->list);
		reiser4_free_volume(vol);
	}
	assert("edward-2329", list_empty(&reiser4_volumes));

	mutex_unlock(&reiser4_volumes_mutex);
}

/**
 * read master super-block from disk and make its copy
 */
int reiser4_read_master_sb(struct block_device *bdev,
			   struct reiser4_master_sb *copy)
{
	struct page *page;
	struct reiser4_master_sb *master;
	/*
	 * read master super block
	 */
	page = read_cache_page_gfp(bdev->bd_inode->i_mapping,
				   REISER4_MAGIC_OFFSET >> PAGE_SHIFT,
				   GFP_KERNEL);
	if (IS_ERR_OR_NULL(page))
		return -EINVAL;
	master = kmap(page);
	if (strncmp(master->magic,
		    REISER4_SUPER_MAGIC_STRING,
		    sizeof(REISER4_SUPER_MAGIC_STRING))) {
		/*
		 * there is no reiser4 on the device
		 */
		kunmap(page);
		put_page(page);
		return -EINVAL;
	}
	memcpy(copy, master, sizeof(*master));
	kunmap(page);
	put_page(page);
	return 0;
}

/**
 * Read master and format super-blocks from device specified by @path and
 * check their magics.
 * If found, then check parameters of found master and format super-blocks
 * and try to register a brick accociated with this device.
 * On success store pointer to registered subvolume in @result.
 * On success return 0. Otherwise return error.
 */
int reiser4_scan_device(const char *path, fmode_t flags, void *holder,
			reiser4_subvol **result, reiser4_volume **host)
{
	int ret;
	u64 subv_id;
	struct block_device *bdev;
	struct reiser4_master_sb master;
	u16 df_pid, dist_pid, vol_pid;
	u8 stripe_bits = 0;
	u16 mirror_id, nr_replicas;
	disk_format_plugin *df_plug;
	volume_plugin *vol_plug;
	distribution_plugin *dist_plug = NULL;

	mutex_lock(&reiser4_volumes_mutex);

	bdev = blkdev_get_by_path(path, flags, holder);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto out;
	}
	ret = reiser4_read_master_sb(bdev, &master);
	if (ret)
		goto bdev_put;

	ret = -EINVAL;
	df_pid = master_get_dformat_pid(&master);
	df_plug = disk_format_plugin_by_unsafe_id(df_pid);
	if (df_plug == NULL)
		/* unknown disk format plugin */
		goto bdev_put;

	vol_pid = master_get_volume_pid(&master);
	vol_plug = volume_plugin_by_unsafe_id(vol_pid);
	if (!vol_plug)
		/* unknown volume plugin */
		goto bdev_put;

	mirror_id = master_get_mirror_id(&master);
	nr_replicas = master_get_num_replicas(&master);
	if (mirror_id > nr_replicas) {
		warning("edward-1739",
		       "%s: mirror id (%u) larger than number of replicas (%u)",
			path, mirror_id, nr_replicas);
		goto bdev_put;
	}

	dist_pid = master_get_distrib_pid(&master);
	dist_plug = distribution_plugin_by_unsafe_id(dist_pid);
	if (!dist_plug)
		/* unknown distribution plugin */
		goto bdev_put;

	stripe_bits = master_get_stripe_bits(&master);
	if (stripe_bits != 0 &&
	    stripe_bits < PAGE_SHIFT &&
	    stripe_bits > MAX_STRIPE_BITS) {
		warning("edward-1814",
			"bad stripe_bits value (%d)n", stripe_bits);
		goto bdev_put;
	}
	/*
	 * Now retrieve subvolume's internal ID from format super-block.
	 * It is allowed to do before activating subvolume, because
	 * internal ID never get changed during subvolume's life.
	 * Thus, format super-block always contains actual version of
	 * internal ID (even before transaction replay).
	 */
	ret = df_plug->extract_subvol_id(bdev, &subv_id);
	if (ret)
		goto bdev_put;
	ret = reiser4_register_subvol(path,
				      master.uuid,
				      master.sub_uuid,
				      df_plug,
				      vol_plug,
				      dist_plug,
				      mirror_id,
				      nr_replicas,
				      stripe_bits,
				      subv_id,
				      result, host);
	if (ret > 0)
		/* ok, it was registered earlier */
		ret = 0;
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
	fmode_t mode = FMODE_READ | FMODE_EXCL;
	reiser4_volume *vol = super_volume(super);

	assert("edward-2309", vol != NULL);
	assert("edward-2301", !subvol_is_set(subv, SUBVOL_ACTIVATED));

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
	if (is_replica(subv)) {
		if (!vol->conf) {
			/*
			 * This is a replica of meta-data brick.
			 * Allocate temporary config needed to
			 * activate original meta-data brick.
			 * This temporary config will be replaced
			 * with an actual one after replaying
			 * transactions on the meta-data brick
			 * (see read_check_volume_params()).
			 */
			assert("edward-2310",
			       subv->id == METADATA_SUBVOL_ID);
			vol->conf = alloc_lv_conf(1 /* one mslot */);
			if (!vol->conf)
				return -ENOMEM;
		}
		/*
		 * Nothing to do any more for replicas!
		 * Particularly, we are not entitled to
		 * replay journal on replicas (only on
		 * original bricks - it will also update
		 * replica blocks properly).
		 */
		goto ok;
	}
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
	printk("reiser4: brick %s activated\n", subv->name);
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
	if (conf == NULL)
		return;
	if (conf->tab)
		current_dist_plug()->r.free(conf->tab);
	kfree(conf);
}

void free_mslot_at(lv_conf *conf, u64 idx)
{
	assert("edward-2231", conf != NULL);
	assert("edward-2190", conf->mslots[idx] != NULL);

	free_mslot(conf->mslots[idx]);
	conf->mslots[idx] = NULL;
}

void release_lv_conf(reiser4_volume *vol, lv_conf *conf)
{
	u32 i;

	assert("edward-2263", vol->conf == conf);

	if (!conf)
		return;
	/*
	 * release distribution table
	 */
	if (vol->dist_plug->r.done)
		vol->dist_plug->r.done(&conf->tab);

	assert("edward-2264", conf->tab == NULL);
	/*
	 * release content of mslots
	 */
	for (i = 0; i < conf->nr_mslots; i++)
		if (conf->mslots[i])
			free_mslot_at(conf, i);
	free_lv_conf(conf);
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
	printk("reiser4: brick %s deactivated\n", subv->name);
}

static void deactivate_subvolumes_cond(struct super_block *super,
				       int(*cond)(reiser4_subvol *))
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
		if (!cond(subv))
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
	int ret;
	reiser4_subvol *subv;
	reiser4_volume *vol = super_volume(super);
	lv_conf *conf = vol->conf;

	if (reiser4_volume_is_activated(super) && !rofs_super(super)) {
		u32 orig_id;
		for_each_mslot(conf, orig_id) {
			if (!conf->mslots[orig_id])
				continue;
			subv = conf_origin(conf, orig_id);
			if (!subvol_is_set(subv, SUBVOL_IS_ORPHAN)) {
				ret = capture_brick_super(subv);
				if (ret != 0)
					warning("vs-898",
					    "Failed to capture superblock (%d)",
						ret);
			}
		}
		ret = txnmgr_force_commit_all(super, 1);
		if (ret != 0)
			warning("jmacd-74438",
				"txn_force failed: %d", ret);
		all_grabbed2free();
	}
	if (vol->vol_plug->done_volume)
		vol->vol_plug->done_volume(vol);

	deactivate_subvolumes_cond(super, is_origin);
	deactivate_subvolumes_cond(super, is_replica);

	if (vol->new_conf) {
		assert("edward-2254",
		       reiser4_volume_is_unbalanced(super));
		assert("edward-2255",
		       vol->new_conf->tab == vol->conf->tab);

		vol->new_conf->tab = NULL;
		free_lv_conf(vol->new_conf);
		vol->new_conf = NULL;
	}
	vol->victim = NULL;

	release_lv_conf(vol, vol->conf);
	vol->conf = NULL;
	vol->num_sgs_bits = 0;
	atomic_set(&vol->nr_origins, 0);

	assert("edward-2302", !list_empty(&vol->subvols_list));

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
 * Set a pointer to activated subvolume @subv (original, or
 * replica) at the respective slot in the table of activated
 * subvolumes of logical volume @vol. Allocate column of the
 * table, if needed.
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
		 * slot is "empty". Allocate a "column" -
		 * array of pointers to mirrors
		 */
		conf->mslots[orig_id] = alloc_mslot(1 + subv->num_replicas);
		if (conf->mslots[orig_id] == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (conf->mslots[orig_id][mirr_id] != NULL) {
		warning("edward-1767",
			"wrong set of registered bricks: "
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
 * Activate all subvolumes of type specified by @cond
 */
static int activate_subvolumes_cond(struct super_block *super, u8 *vol_uuid,
				    int(*cond)(reiser4_subvol *))
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
		if (list_empty_careful(&vol->subvols_list))
			return 0;
		if (!cond(subv))
			continue;
		if (subvol_is_set(subv, SUBVOL_ACTIVATED))
			continue;
		ret = reiser4_activate_subvol(super, subv);
		if (ret)
			return ret;
		assert("edward-1769", subvol_is_set(subv, SUBVOL_ACTIVATED));

		ret = set_activated_subvol(vol, subv);
		if (ret)
			return ret;
	}
	return 0;
}

static int is_meta_origin(reiser4_subvol *subv)
{
	return is_meta_brick_id(subv->id) && is_origin(subv);
}

static int is_meta_replica(reiser4_subvol *subv)
{
	return is_meta_brick_id(subv->id) && is_replica(subv);
}

/**
 * Activate all subvolumes (components) of asymmetric logical
 * volume in a particular order.
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
	 * Order of activation (don't change it).
	 *
	 * Before activating an original brick we need to activate
	 * all its replicas, because activation of original brick is
	 * followed with journal replay and for every IO submitted
	 * for the original brick we always immediately submit IOs
	 * for all its replicas. In contrast with original bricks,
	 * replicas are activated without journal replay.
	 *
	 * Besides, we need to start from the replica of meta-data
	 * brick, which contains system information needed to activate
	 * other (data) bricks.
	 */
	ret = activate_subvolumes_cond(super, vol_uuid, is_meta_replica);
	if (ret)
		goto deactivate;
	ret = activate_subvolumes_cond(super, vol_uuid, is_meta_origin);
	if (ret)
		goto deactivate;
	ret = activate_subvolumes_cond(super, vol_uuid, is_replica);
	if (ret)
		goto deactivate;
	ret = activate_subvolumes_cond(super, vol_uuid, is_origin);
	if (ret)
		goto deactivate;
	/*
	 * At this point all activated original bricks have complete
	 * sets of active replicas - it is guaranteed by calling
	 * check_active_replicas() when activating an original brick.
	 * Now make sure that the set of original bricks is complete.
	 */
	vol = get_super_private(super)->vol;
	assert("edward-2207", vol != NULL);

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
		   "%s: wrong set of registered bricks (found %u, expected %u)",
			super->s_id, nr_origins, atomic_read(&vol->nr_origins));
		ret = -EINVAL;
		goto deactivate;
	}
	/*
	 * Identify activated subvolumes
	 */
	if (!get_meta_subvol()) {
		warning("edward-2298",
			"%s: meta-data brick is not registered", super->s_id);
		ret = -EINVAL;
		goto deactivate;
	}
	for_each_data_mslot(conf, orig_id) {
		reiser4_subvol *subv;
		if (!conf_mslot_at(conf, orig_id) ||
		    !conf_origin(conf, orig_id))
			continue;
		subv = conf_origin(conf, orig_id);
		if (!brick_identify(subv)) {
			warning("edward-2299",
				"%s: Brick %s doesn't match logical volume.",
				super->s_id, subv->name);
			ret = -EINVAL;
			goto deactivate;
		}
	}
	/*
	 * initialize logical volume after activating all subvolumes
	 */
	if (vol->vol_plug->init_volume != NULL) {
		ret = vol->vol_plug->init_volume(super, vol);
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
