/* Copyright 2005 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "flush.h"
#include "safe_link.h"
#include "checksum.h"
#include "plugin/volume/volume.h"

#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/backing-dev.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

/* slab cache for inodes */
static struct kmem_cache *inode_cache;

static struct dentry *reiser4_debugfs_root = NULL;

/**
 * init_once - constructor for reiser4 inodes
 * @cache: cache @obj belongs to
 * @obj: inode to be initialized
 *
 * Initialization function to be called when new page is allocated by reiser4
 * inode cache. It is set on inode cache creation.
 */
static void init_once(void *obj)
{
	struct reiser4_inode_object *info;

	info = obj;

	/* initialize vfs inode */
	inode_init_once(&info->vfs_inode);

	/*
	 * initialize reiser4 specific part fo inode.
	 * NOTE-NIKITA add here initializations for locks, list heads,
	 * etc. that will be added to our private inode part.
	 */
	INIT_LIST_HEAD(get_readdir_list(&info->vfs_inode));
	init_rwsem(&info->p.conv_sem);
	/* init semaphore which is used during inode loading */
	loading_init_once(&info->p);
	INIT_RADIX_TREE(jnode_tree_by_reiser4_inode(&info->p),
			GFP_ATOMIC);
#if REISER4_DEBUG
	info->p.nr_jnodes = 0;
#endif
}

/**
 * init_inodes - create znode cache
 *
 * Initializes slab cache of inodes. It is part of reiser4 module initialization
 */
static int init_inodes(void)
{
	inode_cache = kmem_cache_create("reiser4_inode",
					sizeof(struct reiser4_inode_object),
					0,
					SLAB_HWCACHE_ALIGN |
					SLAB_RECLAIM_ACCOUNT, init_once);
	if (inode_cache == NULL)
		return RETERR(-ENOMEM);
	return 0;
}

/**
 * done_inodes - delete inode cache
 *
 * This is called on reiser4 module unloading or system shutdown.
 */
static void done_inodes(void)
{
	destroy_reiser4_cache(&inode_cache);
}

/**
 * reiser4_alloc_inode - alloc_inode of super operations
 * @super: super block new inode is allocated for
 *
 * Allocates new inode, initializes reiser4 specific part of it.
 */
static struct inode *reiser4_alloc_inode(struct super_block *super)
{
	struct reiser4_inode_object *obj;

	assert("nikita-1696", super != NULL);
	obj = kmem_cache_alloc(inode_cache, reiser4_ctx_gfp_mask_get());
	if (obj != NULL) {
		reiser4_inode *info;

		info = &obj->p;

		info->pset = plugin_set_get_empty();
		info->hset = plugin_set_get_empty();
		info->extmask = 0;
		info->locality_id = 0ull;
		info->plugin_mask = 0;
		info->heir_mask = 0;
#if !REISER4_INO_IS_OID
		info->oid_hi = 0;
#endif
		reiser4_seal_init(&info->sd_seal, NULL, NULL);
		coord_init_invalid(&info->sd_coord, NULL);
		info->flags = 0;
		spin_lock_init(&info->guard);
		/* this deals with info's loading semaphore */
		loading_alloc(info);
		info->vroot = UBER_TREE_ADDR;
		return &obj->vfs_inode;
	} else
		return NULL;
}

/**
 * reiser4_destroy_inode - destroy_inode of super operations
 * @inode: inode being destroyed
 *
 * Puts reiser4 specific portion of inode, frees memory occupied by inode.
 */
static void reiser4_destroy_inode(struct inode *inode)
{
	reiser4_inode *info;

	info = reiser4_inode_data(inode);

	assert("vs-1220", inode_has_no_jnodes(info));

	if (!is_bad_inode(inode) && is_inode_loaded(inode)) {
		file_plugin *fplug = inode_file_plugin(inode);
		if (fplug->destroy_inode != NULL)
			fplug->destroy_inode(inode);
	}
	reiser4_dispose_cursors(inode);
	if (info->pset)
		plugin_set_put(info->pset);
	if (info->hset)
		plugin_set_put(info->hset);

	/*
	 * cannot add similar assertion about ->i_list as prune_icache return
	 * inode into slab with dangling ->list.{next,prev}. This is safe,
	 * because they are re-initialized in the new_inode().
	 */
	assert("nikita-2895", hlist_empty(&inode->i_dentry));
	assert("nikita-2896", hlist_unhashed(&inode->i_hash));
	assert("nikita-2898", list_empty_careful(get_readdir_list(inode)));

	/* this deals with info's loading semaphore */
	loading_destroy(info);

	kmem_cache_free(inode_cache,
			container_of(info, struct reiser4_inode_object, p));
}

/**
 * reiser4_dirty_inode - dirty_inode of super operations
 * @inode: inode being dirtied
 *
 * Updates stat data.
 */
static void reiser4_dirty_inode(struct inode *inode, int flags)
{
	int result;
	reiser4_context *ctx;

	if (!is_in_reiser4_context())
		return;
	ctx = get_current_context();

	if (ctx->ro) {
		warning("edward-2200",
			"failed to make inode %llu dirty (read-only FS)",
			(unsigned long long)get_inode_oid(inode));
		return;
	}
	assert("edward-1606", !IS_RDONLY(inode));
	assert("edward-1607",
	       (inode_file_plugin(inode)->estimate.update(inode) <=
		ctx_subvol_grabbed(ctx, get_meta_subvol()->id)));

	if (ctx->locked_page)
		unlock_page(ctx->locked_page);

	result = reiser4_update_sd(inode);

	if (ctx->locked_page)
		lock_page(ctx->locked_page);
	if (result)
		warning("edward-1605", "failed to dirty inode for %llu: %d",
			get_inode_oid(inode), result);
}

/**
 * ->evict_inode() of super operations
 * @inode: inode to delete
 *
 * Calls file plugin's delete_object method to delete object items from
 * filesystem tree and calls clear_inode().
 */
static void reiser4_evict_inode(struct inode *inode)
{
	reiser4_context *ctx;
	file_plugin *fplug;

	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx)) {
		warning("vs-15", "failed to init context");
		return;
	}

	if (inode->i_nlink == 0 && is_inode_loaded(inode)) {
		fplug = inode_file_plugin(inode);
		if (fplug != NULL && fplug->delete_object != NULL)
			fplug->delete_object(inode);
	}

	truncate_inode_pages_final(&inode->i_data);
	inode->i_blocks = 0;
	clear_inode(inode);
	reiser4_exit_context(ctx);
}

/**
 * reiser4_put_super - put_super of super operations
 * @super: super block to free
 *
 * Stops daemons, release resources, umounts in short.
 */
static void reiser4_put_super(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	reiser4_context *ctx;

	sbinfo = get_super_private(super);
	assert("vs-1699", sbinfo);

	debugfs_remove(sbinfo->tmgr.debugfs_atom_count);
	debugfs_remove(sbinfo->tmgr.debugfs_id_count);
	debugfs_remove(sbinfo->debugfs_root);

	ctx = reiser4_init_context(super);
	if (IS_ERR(ctx)) {
		warning("vs-17", "failed to init context");
		return;
	}
	/*
	 * release disk format related resources
	 */
	reiser4_deactivate_volume(super);
	reiser4_jnodes_done();
	reiser4_done_formatted_fake(super);
	reiser4_done_csum_tfm(sbinfo->csum_tfm);

	/* stop daemons: ktxnmgr and entd */
	reiser4_done_entd(super);
	reiser4_done_ktxnmgrd(super);
	reiser4_done_txnmgr(&sbinfo->tmgr);

	assert("edward-1890", list_empty(&get_super_private(super)->all_jnodes));
	assert("edward-1891", get_current_context()->trans->atom == NULL);

	reiser4_exit_context(ctx);
	reiser4_done_fs_info(super);
}

/**
 * reiser4_statfs - statfs of super operations
 * @super: super block of file system in queried
 * @stafs: buffer to fill with statistics
 *
 * Returns information about filesystem.
 */
static int reiser4_statfs(struct dentry *dentry, struct kstatfs *statfs)
{
	sector_t total;
	sector_t reserved;
	sector_t free;
	sector_t forroot;
	sector_t deleted;
	reiser4_context *ctx;
	struct super_block *super = dentry->d_sb;

	assert("nikita-408", super != NULL);
	assert("nikita-409", statfs != NULL);

	ctx = reiser4_init_context(super);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	statfs->f_type = reiser4_statfs_type(super);
	statfs->f_bsize = super->s_blocksize;

	/*
	 * 5% of total block space is reserved. This is needed for flush and
	 * for truncates (so that we are able to perform truncate/unlink even
	 * on the otherwise completely full file system). If this reservation
	 * is hidden from statfs(2), users will mistakenly guess that they
	 * have enough free space to complete some operation, which is
	 * frustrating.
	 *
	 * Another possible solution is to subtract ->blocks_reserved from
	 * ->f_bfree, but changing available space seems less intrusive than
	 * letting user to see 5% of disk space to be used directly after
	 * mkfs.
	 */
	total = reiser4_volume_block_count(super);
	reserved = reiser4_volume_blocks_reserved(super);
	deleted = txnmgr_count_deleted_blocks();
	free = reiser4_volume_free_blocks(super) + deleted;
	forroot = reiser4_volume_reserved4user(super, 0, 0);

	/*
	 * These counters may be in inconsistent state because we take the
	 * values without keeping any global spinlock.  Here we do a sanity
	 * check that free block counter does not exceed the number of all
	 * blocks.
	 */
	if (free > total)
		free = total;
	statfs->f_blocks = total - reserved;
	/* make sure statfs->f_bfree is never larger than statfs->f_blocks */
	if (free > reserved)
		free -= reserved;
	else
		free = 0;
	statfs->f_bfree = free;

	if (free > forroot)
		free -= forroot;
	else
		free = 0;
	statfs->f_bavail = free;

	statfs->f_files = 0;
	statfs->f_ffree = 0;
	/*
	 * maximal acceptable name length depends on directory plugin
	 */
	assert("nikita-3351", super->s_root->d_inode != NULL);
	statfs->f_namelen = reiser4_max_filename_len(super->s_root->d_inode);
	reiser4_exit_context(ctx);
	return 0;
}

/**
 * reiser4_writeback_inodes - writeback_inodes of super operations
 * @super:
 * @wb:
 * @wbc:
 *
 * This method is called by background and non-backgound writeback.
 * Reiser4's implementation uses generic_writeback_sb_inodes to call
 * reiser4_writepages_dispatch for each of dirty inodes.
 * reiser4_writepages_dispatch handles pages dirtied via shared
 * mapping - dirty pages get into atoms. Writeout is called to flush
 * some atoms.
 */
static long reiser4_writeback_inodes(struct super_block *super,
				     struct bdi_writeback *wb,
				     struct writeback_control *wbc,
				     struct wb_writeback_work *work,
				     bool flush_all)
{
	long result;
	reiser4_context *ctx;

	if (wbc->for_kupdate)
		/* reiser4 has its own means of periodical write-out */
		goto skip;

	spin_unlock(&wb->list_lock);
	ctx = reiser4_init_context(super);
	if (IS_ERR(ctx)) {
		warning("vs-13", "failed to init context");
		spin_lock(&wb->list_lock);
		goto skip;
	}
	ctx->flush_bd_task = 1;
	/*
	 * call reiser4_writepages for each of dirty inodes to turn
	 * dirty pages into transactions if they were not yet.
	 */
	spin_lock(&wb->list_lock);
	result = generic_writeback_sb_inodes(super, wb, wbc, work, flush_all);
	spin_unlock(&wb->list_lock);

	if (result <= 0)
		goto exit;
	wbc->nr_to_write = result;

	/* flush goes here */
	reiser4_writeout(super, wbc);
 exit:
	/* avoid recursive calls to ->writeback_inodes */
	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);
	spin_lock(&wb->list_lock);

	return result;
 skip:
	writeback_skip_sb_inodes(super, wb);
	return 0;
}

/**
 * ->sync_fs() of super operations
 */
static int reiser4_sync_fs(struct super_block *super, int wait)
{
	reiser4_context *ctx;
	struct bdi_writeback *wb;
	struct wb_writeback_work work = {
		.sb		= super,
		.sync_mode	= WB_SYNC_ALL,
		.range_cyclic	= 0,
		.nr_pages	= LONG_MAX,
		.reason		= WB_REASON_SYNC,
		.for_sync	= 1,
	};
	struct writeback_control wbc = {
		.sync_mode	= work.sync_mode,
		.range_cyclic	= work.range_cyclic,
		.range_start	= 0,
		.range_end	= LLONG_MAX,
	};
	ctx = reiser4_init_context(super);
	if (IS_ERR(ctx)) {
		warning("edward-1567", "failed to init context");
		return PTR_ERR(ctx);
	}
	/*
	 * We don't capture superblock here.
	 * Superblock is captured only by operations, which change
	 * its fields different from free_blocks, nr_files, next_oid.
	 * After system crash the mentioned fields are recovered from
	 * journal records, see reiser4_journal_recover_sb_data().
	 * Also superblock is captured at final commit when releasing
	 * disk format.
	 */
	wb = &inode_to_bdi(reiser4_get_super_fake(super))->wb;
	spin_lock(&wb->list_lock);
	generic_writeback_sb_inodes(super, wb, &wbc, &work, true);
	spin_unlock(&wb->list_lock);
	wbc.nr_to_write = LONG_MAX;
	/*
	 * (flush goes here)
	 * commit all transactions
	 */
	reiser4_writeout(super, &wbc);

	reiser4_exit_context(ctx);
	return 0;
}

static int reiser4_remount(struct super_block *s, int *mount_flags, char *arg)
{
	sync_filesystem(s);
	return 0;
}

/**
 * reiser4_show_options - show_options of super operations
 * @m: file where to write information
 * @mnt: mount structure
 *
 * Makes reiser4 mount options visible in /proc/mounts.
 */
static int reiser4_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *super;
	reiser4_super_info_data *sbinfo;

	super = dentry->d_sb;
	sbinfo = get_super_private(super);

	seq_printf(m, ",atom_max_size=0x%x", sbinfo->tmgr.atom_max_size);
	seq_printf(m, ",atom_max_age=0x%x", sbinfo->tmgr.atom_max_age);
	seq_printf(m, ",atom_min_size=0x%x", sbinfo->tmgr.atom_min_size);
	seq_printf(m, ",atom_max_flushers=0x%x",
		   sbinfo->tmgr.atom_max_flushers);
	seq_printf(m, ",cbk_cache_slots=0x%x",
		   super_meta_subvol(super)->tree.cbk_cache.nr_slots);
	return 0;
}

struct super_operations reiser4_super_operations = {
	.alloc_inode = reiser4_alloc_inode,
	.destroy_inode = reiser4_destroy_inode,
	.dirty_inode = reiser4_dirty_inode,
	.evict_inode = reiser4_evict_inode,
	.put_super = reiser4_put_super,
	.sync_fs = reiser4_sync_fs,
	.statfs = reiser4_statfs,
	.remount_fs = reiser4_remount,
	.writeback_inodes = reiser4_writeback_inodes,
	.show_options = reiser4_show_options
};

/**
 * fill_super - initialize super block on mount.
 * All subvolumes of the volume should be already registered in the system
 *
 * @super: super block to fill
 * @data: reiser4 specific mount option
 * @silent:
 */
static int fill_super(struct super_block *super, void *data, int silent)
{
	u32 subv_id;
	reiser4_context ctx;
	int result;
	reiser4_super_info_data *sbinfo;
	lv_conf *conf;
	u8 vol_uuid[16];

	assert("zam-989", super != NULL);

	super->s_op = NULL;
	/*
	 * context initialization will be completed after init_volume(),
	 * as we don't know number of subvolumes yet.
	 */
	init_stack_context(&ctx, super);
	/*
	 * allocate reiser4 private super info
	 */
	if ((result = reiser4_init_fs_info(super)) != 0)
		goto failed_init_sinfo;

	sbinfo = get_super_private(super);

	if ((result = reiser4_init_csum_tfm(&sbinfo->csum_tfm)) != 0)
		goto failed_init_csum_tfm;

	/* initialize various reiser4 parameters, parse mount options */
	if ((result = reiser4_init_super_data(super, data)) != 0)
		goto failed_init_super_data;

	/* set filesystem blocksize */
	if ((result = reiser4_read_master(super, silent, vol_uuid)) != 0)
		goto failed_read_master;

	/* initialize transaction manager */
	reiser4_init_txnmgr(&sbinfo->tmgr);

	/* initialize ktxnmgrd context and start kernel thread ktxnmrgd */
	if ((result = reiser4_init_ktxnmgrd(super)) != 0)
		goto failed_init_ktxnmgrd;

	/* initialize entd context and start kernel thread entd */
	if ((result = reiser4_init_entd(super)) != 0)
		goto failed_init_entd;

	/* initialize address spaces for formatted nodes and bitmaps */
	if ((result = reiser4_init_formatted_fake(super)) != 0)
		goto failed_init_formatted_fake;

	/* initialize jnode hash table */
	if ((result = reiser4_jnodes_init()) != 0)
		goto failed_jnodes_init;

	/* initialize disk formats of all subvolumes */
	if ((result = reiser4_activate_volume(super, vol_uuid)) != 0)
		goto failed_activate_volume;

	sbinfo->default_uid = 0;
	sbinfo->default_gid = 0;
	sbinfo->nr_files_committed = oids_used(super);

	/* calculate total number of blocks in the logical volume */
	conf = sbinfo_conf(sbinfo);
	for_each_mslot(conf, subv_id) {
		if (!conf_mslot_at(conf, subv_id))
			continue;
		sbinfo->vol_block_count +=
			conf_origin(conf, subv_id)->block_count;
	}
	/* get inode of root directory */
	if ((result = reiser4_init_root_inode(super)) != 0)
		goto failed_init_root_inode;

	/* finish unfinished truncates */
	if ((result = process_safelinks(super, get_meta_subvol()) != 0))
		goto failed_process_safelinks;

	reiser4_exit_context(&ctx);

	sbinfo->debugfs_root = debugfs_create_dir(super->s_id,
						  reiser4_debugfs_root);
	if (sbinfo->debugfs_root) {
		debugfs_create_u32("atom_count", S_IFREG|S_IRUSR,
				   sbinfo->debugfs_root,
				   &sbinfo->tmgr.atom_count);
		debugfs_create_u32("id_count", S_IFREG|S_IRUSR,
				   sbinfo->debugfs_root,
				   &sbinfo->tmgr.id_count);
	}
	return 0;
 failed_process_safelinks:
	dput(super->s_root);
 failed_init_root_inode:
	reiser4_deactivate_volume(super);
 failed_activate_volume:
	reiser4_jnodes_done();
 failed_jnodes_init:
	reiser4_done_formatted_fake(super);
 failed_init_formatted_fake:
	reiser4_done_entd(super);
 failed_init_entd:
	reiser4_done_ktxnmgrd(super);
 failed_init_ktxnmgrd:
	reiser4_done_txnmgr(&sbinfo->tmgr);
 failed_read_master:
 failed_init_super_data:
 failed_init_csum_tfm:
	reiser4_done_fs_info(super);
 failed_init_sinfo:
	reiser4_exit_context(&ctx);
	return result;
}

/**
 * reiser4_mount - mount of file_system_type operations
 * @fs_type:
 * @flags: mount flags
 * @dev_name: block device file name
 * @data: specific mount options
 *
 * Reiser4 mount entry.
 */
static struct dentry *reiser4_mount(struct file_system_type *fs_type, int flags,
				    const char *dev_name, void *data)
{
	int ret;
	reiser4_volume *host = NULL;
	reiser4_subvol *subv = NULL;
	/*
	 * the volume could be created by old version of reiser4progs,
	 * so try to register it here.
	 */
	ret = reiser4_scan_device(dev_name, flags, fs_type, &subv, &host,
				  NULL);
	if (ret)
		return ERR_PTR(ret);

	assert("edward-1966", host != NULL);
	assert("edward-1967", subv != NULL);

	if (!is_meta_brick_id(subv->id)) {
		/*
		 * Scan all registered bricks to find meta-data brick
		 */
		subv = find_meta_brick_by_id(host);
		if (subv == NULL) {
			warning("edward-1968",
				"%s: meta-data brick is not registered.", 
				dev_name);
			return ERR_PTR(-EINVAL);
		}
		dev_name = subv->name;
	}
	return mount_bdev(fs_type, flags, dev_name, data, fill_super);
}

/* structure describing the reiser4 filesystem implementation */
static struct file_system_type reiser4_fs_type = {
	.owner = THIS_MODULE,
	.name = "reiser4",
	.fs_flags = FS_REQUIRES_DEV,
	.mount = reiser4_mount,
	.kill_sb = kill_block_super,
	.next = NULL
};

void destroy_reiser4_cache(struct kmem_cache **cachep)
{
	BUG_ON(*cachep == NULL);
	kmem_cache_destroy(*cachep);
	*cachep = NULL;
}

struct file_system_type *get_reiser4_fs_type(void)
{
	return &reiser4_fs_type;
}

/**
 * Used by volume.reiser4 to scan devices when no FS is mounted
 */
static long reiser4_control_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	int ret;
	struct reiser4_vol_op_args *op_args;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case REISER4_IOC_SCAN_DEV:
		op_args = memdup_user((void __user *)arg, sizeof(*op_args));
		if (IS_ERR(op_args))
			return PTR_ERR(op_args);

		ret = reiser4_offline_op(op_args);
		if (copy_to_user((struct reiser4_vol_op_args __user *)arg,
				 op_args, sizeof(*op_args)))
			ret = RETERR(-EFAULT);
		kfree(op_args);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static const struct file_operations reiser4_ctl_fops = {
	.unlocked_ioctl	 = reiser4_control_ioctl,
	.compat_ioctl = reiser4_control_ioctl,
	.owner	 = THIS_MODULE,
	.llseek = noop_llseek,
};

static struct miscdevice reiser4_misc = {
	.minor		= REISER4_MINOR,
	.name		= "reiser4-control",
	.fops		= &reiser4_ctl_fops
};

MODULE_ALIAS_MISCDEV(REISER4_MINOR);
MODULE_ALIAS("devname:reiser4-control");

static int reiser4_interface_init(void)
{
	return misc_register(&reiser4_misc);
}

static void reiser4_interface_exit(void)
{
	misc_deregister(&reiser4_misc);
}

/**
 * init_reiser4 - reiser4 initialization entry point
 *
 * Initializes reiser4 slabs, registers reiser4 filesystem type. It is called
 * on kernel initialization or during reiser4 module load.
 */
static int __init init_reiser4(void)
{
	int result;

	printk(KERN_INFO
	       "Loading Reiser4 (Software Framework Release: %d.%d.%d). "
	       "See reiser4.wiki.kernel.org for a description of Reiser4.\n",
	       get_release_number_principal(),
	       get_release_number_major(),
	       get_release_number_minor());

	/* initialize slab cache of inodes */
	if ((result = init_inodes()) != 0)
		goto failed_inode_cache;

	/* initialize cache of znodes */
	if ((result = init_znodes()) != 0)
		goto failed_init_znodes;

	/* initialize all plugins */
	if ((result = init_plugins()) != 0)
		goto failed_init_plugins;

	/* initialize cache of plugin_set-s and plugin_set's hash table */
	if ((result = init_plugin_set()) != 0)
		goto failed_init_plugin_set;

	/* initialize caches of txn_atom-s and txn_handle-s */
	if ((result = init_txnmgr_static()) != 0)
		goto failed_init_txnmgr_static;

	/* initialize cache of jnodes */
	if ((result = init_jnodes()) != 0)
		goto failed_init_jnodes;

	/* initialize cache of flush queues */
	if ((result = reiser4_init_fqs()) != 0)
		goto failed_init_fqs;

	/* initialize cache of structures attached to dentry->d_fsdata */
	if ((result = reiser4_init_dentry_fsdata()) != 0)
		goto failed_init_dentry_fsdata;

	/* initialize cache of structures attached to file->private_data */
	if ((result = reiser4_init_file_fsdata()) != 0)
		goto failed_init_file_fsdata;
	/*
	 * initialize cache of d_cursors. See plugin/file_ops_readdir.c for
	 * more details
	 */
	if ((result = reiser4_init_d_cursor()) != 0)
		goto failed_init_d_cursor;

	/* initialize cache of blocknr set entries */
	if ((result = blocknr_set_init_static()) != 0)
		goto failed_init_blocknr_set;

	/* initialize cache of blocknr list entries */
	if ((result = blocknr_list_init_static()) != 0)
		goto failed_init_blocknr_list;

	/* initialize cache of ctx_brick_info */
	if ((result = ctx_brick_info_init_static()) != 0)
		goto failed_init_ctx_brick_info;

	/* initialize cache of ctx_stack_info */
	if ((result = flush_init_static()) != 0)
		goto failed_init_flush;

	/* initialize interface */
	if ((result = reiser4_interface_init()) != 0)
		goto failed_init_interface;

	if ((result = register_filesystem(&reiser4_fs_type)) == 0) {
		reiser4_debugfs_root = debugfs_create_dir("reiser4", NULL);
		return 0;
	}
	reiser4_interface_exit();
 failed_init_interface:
	done_flush_static();
 failed_init_flush:
	ctx_brick_info_done_static();
 failed_init_ctx_brick_info:
	blocknr_list_done_static();
 failed_init_blocknr_list:
	blocknr_set_done_static();
 failed_init_blocknr_set:
	reiser4_done_d_cursor();
 failed_init_d_cursor:
	reiser4_done_file_fsdata();
 failed_init_file_fsdata:
	reiser4_done_dentry_fsdata();
 failed_init_dentry_fsdata:
	reiser4_done_fqs();
 failed_init_fqs:
	done_jnodes();
 failed_init_jnodes:
	done_txnmgr_static();
 failed_init_txnmgr_static:
	done_plugin_set();
 failed_init_plugin_set:
 failed_init_plugins:
	done_znodes();
 failed_init_znodes:
	done_inodes();
 failed_inode_cache:
	return result;
}

/**
 * done_reiser4 - reiser4 exit entry point
 *
 * Unregister reiser4 filesystem type, deletes caches. It is called on shutdown
 * or at module unload.
 */
static void __exit done_reiser4(void)
{
	int result;

	debugfs_remove(reiser4_debugfs_root);
	result = unregister_filesystem(&reiser4_fs_type);
	BUG_ON(result != 0);
	reiser4_interface_exit();
	ctx_brick_info_done_static();
	blocknr_list_done_static();
	blocknr_set_done_static();
	reiser4_done_d_cursor();
	reiser4_done_file_fsdata();
	reiser4_done_dentry_fsdata();
	reiser4_done_fqs();
	done_jnodes();
	done_txnmgr_static();
	done_plugin_set();
	done_znodes();
	destroy_reiser4_cache(&inode_cache);
	reiser4_unregister_volumes();
}

module_init(init_reiser4);
module_exit(done_reiser4);

MODULE_ALIAS_FS("reiser4");

MODULE_DESCRIPTION("Reiser4 filesystem");
MODULE_AUTHOR("Hans Reiser <Reiser@Namesys.COM>");

MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
