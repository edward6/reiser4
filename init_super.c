/* Copyright by Hans Reiser, 2003 */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "tree.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"
#include "prof.h"
#include "repacker.h"

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>

#define _INIT_PARAM_LIST (struct super_block * s, reiser4_context * ctx, void * data, int silent)
#define _DONE_PARAM_LIST (struct super_block * s)

#define _INIT_(subsys) static int _init_##subsys _INIT_PARAM_LIST
#define _DONE_(subsys) static void _done_##subsys _DONE_PARAM_LIST

#define _DONE_EMPTY(subsys) _DONE_(subsys) {}

_INIT_(mount_flags_check)
{
	if (bdev_read_only(s->s_bdev) || (s->s_flags & MS_RDONLY)) {
		warning("nikita-3322", "Readonly reiser4 is not yet supported");
		return RETERR(-EROFS);
	}
	return 0;
}

_DONE_EMPTY(mount_flags_check)

_INIT_(sinfo)
{
	reiser4_super_info_data * sbinfo;

	sbinfo = kmalloc(sizeof (reiser4_super_info_data), GFP_KERNEL);
	if (!sbinfo)
		return RETERR(-ENOMEM);

	s->s_fs_info = sbinfo;
	xmemset(sbinfo, 0, sizeof (*sbinfo));

	ON_DEBUG(INIT_LIST_HEAD(&sbinfo->all_jnodes));
	ON_DEBUG(kcond_init(&sbinfo->rcu_done));
	ON_DEBUG(atomic_set(&sbinfo->jnodes_in_flight, 0));
	ON_DEBUG(spin_lock_init(&sbinfo->all_guard));

	sema_init(&sbinfo->delete_sema, 1);
	sema_init(&sbinfo->flush_sema, 1);
	s->s_op = &reiser4_super_operations;

	spin_super_init(sbinfo);
	spin_super_eflush_init(sbinfo);

	return 0;
}

#if REISER4_DEBUG
static void finish_rcu(reiser4_super_info_data *sbinfo)
{
	spin_lock_irq(&sbinfo->all_guard);
	while (atomic_read(&sbinfo->jnodes_in_flight) > 0)
		kcond_wait(&sbinfo->rcu_done, &sbinfo->all_guard, 0);
	spin_unlock_irq(&sbinfo->all_guard);
}
#else
#define finish_rcu(sbinfo) noop
#endif

_DONE_(sinfo)
{
	assert("zam-990", s->s_fs_info != NULL);
	finish_rcu(get_super_private(s));
	kfree(s->s_fs_info);
	s->s_fs_info = NULL;
}

_INIT_(stat)
{
	return reiser4_stat_init(&get_super_private(s)->stats);
}

_DONE_(stat)
{
	reiser4_stat_done(&get_super_private(s)->stats);
}

_INIT_(context)
{
	return init_context(ctx, s);
}

_DONE_(context)
{
	get_current_context()->trans = NULL;
	done_context(get_current_context());
}

_INIT_(parse_options)
{
	return reiser4_parse_options(s, data);
}

_DONE_EMPTY(parse_options)

_INIT_(read_super)
{
	struct buffer_head *super_bh;
	struct reiser4_master_sb *master_sb;
	int plugin_id;
	reiser4_super_info_data * sbinfo = get_super_private(s);
	unsigned long blocksize;

 read_super_block:
#ifdef CONFIG_REISER4_BADBLOCKS
	if ( sbinfo->altsuper )
		super_bh = sb_bread(s, (sector_t) (sbinfo->altsuper >> s->s_blocksize_bits));
	else
#endif
		/* look for reiser4 magic at hardcoded place */
		super_bh = sb_bread(s, (sector_t) (REISER4_MAGIC_OFFSET / s->s_blocksize));

	if (!super_bh)
		return RETERR(-EIO);

	master_sb = (struct reiser4_master_sb *) super_bh->b_data;
	/* check reiser4 magic string */
	if (!strncmp(master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4)) {
		/* reset block size if it is not a right one FIXME-VS: better comment is needed */
		blocksize = d16tocpu(&master_sb->blocksize);

		if (blocksize != PAGE_CACHE_SIZE) {
			if (!silent)
				warning("nikita-2609", "%s: wrong block size %ld\n", s->s_id, blocksize);
			brelse(super_bh);
			return RETERR(-EINVAL);
		}
		if (blocksize != s->s_blocksize) {
			brelse(super_bh);
			if (!sb_set_blocksize(s, (int) blocksize)) {
				return RETERR(-EINVAL);
			}
			goto read_super_block;
		}

		plugin_id = d16tocpu(&master_sb->disk_plugin_id);
		/* only two plugins are available for now */
		assert("vs-476", (plugin_id == FORMAT40_ID || plugin_id == TEST_FORMAT_ID));
		sbinfo->df_plug = disk_format_plugin_by_id(plugin_id);
		sbinfo->diskmap_block = d64tocpu(&master_sb->diskmap);
		brelse(super_bh);
	} else {
		if (!silent)
			warning("nikita-2608", "Wrong magic: %x != %x",
				*(__u32 *) master_sb->magic, *(__u32 *) REISER4_SUPER_MAGIC_STRING);
		/* no standard reiser4 super block found */
		brelse(super_bh);
		/* FIXME-VS: call guess method for all available layout
		   plugins */
		/* umka (2002.06.12) Is it possible when format-specific super
		   block exists but there no master super block? */
		return RETERR(-EINVAL);
	}
	return 0;
}

_DONE_EMPTY(read_super)

_INIT_(tree0)
{
	reiser4_super_info_data * sbinfo = get_super_private(s);

	init_tree_0(&sbinfo->tree);
	sbinfo->tree.super = s;
	return 0;
}

_DONE_EMPTY(tree0)

_INIT_(txnmgr)
{
	txnmgr_init(&get_super_private(s)->tmgr);
	return 0;
}

_DONE_(txnmgr)
{
	txnmgr_done(&get_super_private(s)->tmgr);
}

extern ktxnmgrd_context kdaemon;

_INIT_(ktxnmgrd)
{
	return ktxnmgrd_attach(&kdaemon, &get_super_private(s)->tmgr);
}

_DONE_(ktxnmgrd)
{
	ktxnmgrd_detach(&get_super_private(s)->tmgr);
}

_INIT_(formatted_fake)
{
	return init_formatted_fake(s);
}

_DONE_(formatted_fake)
{
	done_formatted_fake(s);
}

_INIT_(entd)
{
	init_entd_context(s);
	return 0;
}

_DONE_(entd)
{
	done_entd_context(s);
}

_INIT_(disk_format)
{
	return get_super_private(s)->df_plug->get_ready(s, data);
}

_DONE_(disk_format)
{
	get_super_private(s)->df_plug->release(s);
}

_INIT_(object_ops)
{
	build_object_ops(s, &get_super_private(s)->ops);
	return 0;
}

_DONE_EMPTY(object_ops)

_INIT_(sb_counters)
{	
	/* There are some 'committed' versions of reiser4 super block
	   counters, which correspond to reiser4 on-disk state. These counters
	   are initialized here */
	reiser4_super_info_data *sbinfo = get_super_private(s);

	sbinfo->blocks_free_committed = sbinfo->blocks_free;
	sbinfo->nr_files_committed = oids_used(s);

	return 0;
}

_DONE_EMPTY(sb_counters)

_INIT_(cbk_cache)
{
	cbk_cache_init(&get_super_private(s)->tree.cbk_cache);
	return 0;
}

_DONE_(cbk_cache)
{
	cbk_cache_done(&get_super_private(s)->tree.cbk_cache);
}

_INIT_(fs_root)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);
	struct inode * inode;

	inode = reiser4_iget(s, sbinfo->df_plug->root_dir_key(s));
	if (IS_ERR(inode))
		return RETERR(PTR_ERR(inode));

	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		iput(inode);
		return RETERR(-ENOMEM);
	}
	
	s->s_root->d_op = &sbinfo->ops.dentry;

	if (inode->i_state & I_NEW) {
		reiser4_inode *info;

		info = reiser4_inode_data(inode);

		grab_plugin_from(info, file, default_file_plugin(s));
		grab_plugin_from(info, dir, default_dir_plugin(s));
		grab_plugin_from(info, sd, default_sd_plugin(s));
		grab_plugin_from(info, hash, default_hash_plugin(s));
		grab_plugin_from(info, tail, default_tail_plugin(s));
		grab_plugin_from(info, perm, default_perm_plugin(s));
		grab_plugin_from(info, dir_item, default_dir_item_plugin(s));

		assert("nikita-1951", info->pset->file != NULL);
		assert("nikita-1814", info->pset->dir != NULL);
		assert("nikita-1815", info->pset->sd != NULL);
		assert("nikita-1816", info->pset->hash != NULL);
		assert("nikita-1817", info->pset->tail != NULL);
		assert("nikita-1818", info->pset->perm != NULL);
		assert("vs-545", info->pset->dir_item != NULL);

		unlock_new_inode(inode);
	}
	s->s_maxbytes = MAX_LFS_FILESIZE;
	return 0;
}

_DONE_(fs_root)
{
	shrink_dcache_parent(s->s_root);
}

_INIT_(sysfs)
{
	return reiser4_sysfs_init(s);
}

_DONE_(sysfs)
{
	reiser4_sysfs_done(s);
}

_INIT_(repacker)
{
	return init_reiser4_repacker(s);
}

_DONE_(repacker)
{
	done_reiser4_repacker(s);
}

_INIT_(exit_context)
{
	return reiser4_exit_context(ctx);
}

_DONE_EMPTY(exit_context)

struct reiser4_subsys {
	int  (*init) _INIT_PARAM_LIST;
	void (*done) _DONE_PARAM_LIST;
};

#define _SUBSYS(subsys) {.init = &_init_##subsys, .done = &_done_##subsys}
static struct reiser4_subsys subsys_array[] = {
	_SUBSYS(mount_flags_check),
	_SUBSYS(sinfo),
	_SUBSYS(stat),
	_SUBSYS(context),
	_SUBSYS(parse_options),
	_SUBSYS(read_super),
	_SUBSYS(tree0),
	_SUBSYS(txnmgr),
	_SUBSYS(ktxnmgrd),
	_SUBSYS(entd),
	_SUBSYS(formatted_fake),
	_SUBSYS(disk_format),
	_SUBSYS(object_ops),
	_SUBSYS(sb_counters),
	_SUBSYS(cbk_cache),
	_SUBSYS(fs_root),
	_SUBSYS(sysfs),
	_SUBSYS(repacker),
	_SUBSYS(exit_context)
};

#define REISER4_NR_SUBSYS (sizeof(subsys_array) / sizeof(struct reiser4_subsys))

static void done_super (struct super_block * s, int last_done)
{
	int i;
	for (i = last_done; i >= 0; i--)
		subsys_array[i].done(s);
}

int reiser4_fill_super (struct super_block * s, void * data, int silent)
{
	reiser4_context ctx;
	int i;
	int ret;

	assert ("zam-989", s != NULL);

	for (i = 0; i < REISER4_NR_SUBSYS; i++) {
		ret = subsys_array[i].init(s, &ctx, data, silent);
		if (ret) {
			done_super(s, i - 1);
			return ret;
		}
	}
	return 0;
}

#if 0

int reiser4_done_super (struct super_block * s)
{
	reiser4_context ctx;

	init_context(&ctx, s);
	done_super(s, REISER4_NR_SUBSYS - 1);
	return 0;
}

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
