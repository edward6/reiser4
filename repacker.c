/* Copyright 2003 by Hans Reiser */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/sched.h>

#include <asm/atomic.h>

#include "reiser4.h"
#include "kattr.h"
#include "super.h"
#include "repacker.h"
#include "tree.h"
#include "tree_walk.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"

#include "plugin/item/extent.h"

struct repacker {
	struct super_block * super;
	atomic_t running;
#if REISER4_USE_SYSFS
	struct kobject kobj;
#endif
	
};

struct repacker_stats {
	long blocks_dirtied;
};

static int dirtying_znode (znode * node, void * arg)
{
	struct repacker_stats * stats = arg;
	int ret;

	if (!znode_is_dirty(node))
		return 0;

	ret = reiser4_grab_space((__u64)1, BA_CAN_COMMIT | BA_FORMATTED, "repacker");
	if (ret)
		return ret;

	znode_make_dirty(node);
	
	{
		reiser4_context * ctx = get_current_context();
		long _ret;

		_ret = txn_end(ctx);
		if (_ret < 0)
			ret = (int)_ret;
		txn_begin(ctx);
	}

	stats->blocks_dirtied ++;
	return ret;
}

static int dirtying_extent (const coord_t * coord, void * stats)
{
	return 0;
}

static struct tree_walk_actor repacker_actor = {
	.process_znode = dirtying_znode,
	.process_extent = dirtying_extent
};

static int repacker_d(void *arg)
{
	struct super_block * super = arg;
	struct repacker * repacker = get_super_private(super)->repacker;
	struct task_struct * me = current; 
	int ret;

	reiser4_context ctx;

	daemonize("k_reiser4_repacker_d");

	/* block all signals */
	spin_lock_irq(&me->sighand->siglock);
	siginitsetinv(&me->blocked, 0);
	recalc_sigpending();
	spin_unlock_irq(&me->sighand->siglock);

	/* zeroing the fs_context copied form parent process' task struct. */
	me->fs_context = NULL;

	ret = init_context(&ctx, super);
	if (ret)
		goto done;

	printk(KERN_INFO "Repacker: I am alive, pid = %u\n", me->pid);
	{
		struct repacker_stats stats = {.blocks_dirtied = 0};
		ret = tree_walk(NULL, &repacker_actor, &stats);
		printk(KERN_INFO "reiser4 repacker: %lu blocks processed\n",
		       stats.blocks_dirtied);
	}
 done:
	{ 
		int ret1;
	
		ret1 = reiser4_exit_context(&ctx);
		if (!ret && ret1)
			ret = ret1;
	}

	atomic_set(&repacker->running, 0);
	return ret;
}

static int start_repacker(struct super_block * super) 
{
	struct repacker * repacker = get_super_private(super)->repacker;

	if (atomic_read(&repacker->running))
		return 0;

	atomic_set(&repacker->running, 1);
	kernel_thread(repacker_d, super, CLONE_VM | CLONE_FS | CLONE_FILES);

	return 0;
} 

static void stop_repacker(struct super_block * super)
{
	struct repacker * repacker = get_super_private(super)->repacker;

	atomic_set(&repacker->running, 1);
}

#if REISER4_USE_SYSFS

static struct attribute start_repacker_attr = {
	.name = "start",
	.mode = 0644		/* rw-r--r */
};

static struct attribute * repacker_def_attrs[] = {
	&start_repacker_attr,
	NULL
}; 

static ssize_t repacker_attr_show (struct kobject *kobj, struct attribute *attr,  char *buf)
{
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);

	return snprintf(buf, PAGE_SIZE , "%d", atomic_read(&repacker->running));
}

static ssize_t repacker_attr_store (struct kobject *kobj, struct attribute *attr,  const char *buf, size_t size)
{
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);
	int start_stop = 0;

	sscanf(buf, "%d", &start_stop);
	if (start_stop) 
		start_repacker(repacker->super);
	else
		stop_repacker(repacker->super);

	return size;
}

static struct sysfs_ops repacker_sysfs_ops = {
	.show  = repacker_attr_show,
	.store = repacker_attr_store
};

static struct kobj_type repacker_ktype = {
	.sysfs_ops     = &repacker_sysfs_ops,
	.default_attrs = repacker_def_attrs,
	.release       = NULL
};

static int init_repacker_sysfs_iface (struct super_block * s)
{
	int ret = 0;
	reiser4_super_info_data * sinfo = get_super_private(s);
	struct kobject * root = &sinfo->kobj;
	struct repacker * repacker = sinfo->repacker;

	assert("zam-947", repacker != NULL);

	snprintf(repacker->kobj.name, KOBJ_NAME_LEN, "repacker");
	repacker->kobj.parent = kobject_get(root); 
	repacker->kobj.ktype = &repacker_ktype;
	ret = kobject_register(&repacker->kobj);

	return ret;
}

static void done_repacker_sysfs_iface (struct super_block * s)
{
	reiser4_super_info_data * sinfo = get_super_private(s);

	kobject_unregister(&sinfo->repacker->kobj);
}

#else

#define init_repacker_sysfs_iface(s) (0)
#define done_repacker_sysfs_iface(s) do{}while(0)

#endif

int init_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);

	assert ("zam-946", sinfo->repacker == NULL);
	sinfo->repacker = kmalloc(sizeof (struct repacker), GFP_KERNEL);
	if (sinfo->repacker == NULL)
		return -ENOMEM;
	xmemset(sinfo->repacker, 0, sizeof(struct repacker));
	sinfo->repacker->super = super;
	return init_repacker_sysfs_iface(super);
}

void done_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);

	assert("zam-945", sinfo->repacker != NULL);
	done_repacker_sysfs_iface(super);
	kfree(sinfo->repacker);
	sinfo->repacker = NULL;
}
