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

#include <linux/spinlock.h>
#include "kcond.h"

enum repacker_state_bits {
	REPACKER_RUNNING = 0x1,
	REPACKER_STOP    = 0x2,
	REPACKER_DESTROY = 0x4
};

struct repacker {
	struct super_block * super;
	enum repacker_state_bits  state;
	spinlock_t guard;
	kcond_t    cond;
#if REISER4_USE_SYSFS
	struct kobject kobj;
#endif
	
};

struct repacker_stats {
	int count;
	long znodes_dirtied;
	long jnodes_dirtied;
};

static int renew_transaction (void)
{
	reiser4_context * ctx = get_current_context();
	long _ret;

	_ret = txn_end(ctx);
	txn_begin(ctx);

	if (_ret < 0)
		return (int)_ret;
	return 0;
}

static inline int check_repacker_state(struct repacker *repacker, enum repacker_state_bits bits)
{
	int result;

	spin_lock(&repacker->guard);
	result = !!(repacker->state & bits);
	spin_unlock(&repacker->guard);

	return result;
}

static int dirtying_znode (tap_t * tap, void * arg)
{
	struct repacker_stats * stats = arg;

	assert("zam-954", stats->count > 0);

	if (check_repacker_state(get_current_super_private()->repacker, REPACKER_STOP))
		return -EINTR;

	if (znode_is_dirty(tap->lh->node))
		return 0;

	znode_make_dirty(tap->lh->node);

	stats->znodes_dirtied ++;

	if (-- stats->count <= 0)
		return -EAGAIN;
	return 0;
}

static int dirtying_extent (tap_t *tap, void * arg)
{
	int ret;
	struct repacker_stats * stats = arg;

	if (check_repacker_state(get_current_super_private()->repacker, REPACKER_STOP))
		return -EINTR;

	ret = mark_extent_for_repacking(tap, stats->count);
	if (ret > 0) {
		stats->jnodes_dirtied += ret;
		stats->count -= ret;
		if (stats->count <= 0)
			     return -EAGAIN;
		return 0;
	}

	return ret;
}

/* The reiser4 repacker process nodes by chunks of REPACKER_CHUNK_SIZE
 * size. */
#define REPACKER_CHUNK_SIZE 100

static int prepare_repacking_session (void * arg)
{
	struct repacker_stats * stats = arg;
	int ret;

	assert("zam-951", schedulable());

	all_grabbed2free(__FUNCTION__);
	ret = renew_transaction();
	if (ret)
		return ret;

	stats->count = REPACKER_CHUNK_SIZE;
	return  reiser4_grab_space((__u64)REPACKER_CHUNK_SIZE, BA_CAN_COMMIT | BA_FORCE, __FUNCTION__);
}

static struct tree_walk_actor repacker_actor = {
	.process_znode  = dirtying_znode,
	.process_extent = dirtying_extent,
	.before         = prepare_repacking_session
};

static int repacker_d(void *arg)
{
	struct repacker * repacker = arg;
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

	ret = init_context(&ctx, repacker->super);
	if (ret)
		goto done;

	printk(KERN_INFO "Repacker: I am alive, pid = %u\n", me->pid);
	{
		struct repacker_stats stats = {.znodes_dirtied = 0, .jnodes_dirtied = 0};

		ret = tree_walk(NULL, 0, &repacker_actor, &stats);
		printk(KERN_INFO "reiser4 repacker: "
		       "%lu formatted node(s) processed, %lu unformatted node(s) processed, ret = %d\n",
		       stats.znodes_dirtied, stats.jnodes_dirtied, ret);
	}
 done:
	{ 
		int ret1;
	
		ret1 = reiser4_exit_context(&ctx);
		if (!ret && ret1)
			ret = ret1;
	}

	spin_lock(&repacker->guard);
	repacker->state &= ~REPACKER_RUNNING;
	kcond_broadcast(&repacker->cond);
	spin_unlock(&repacker->guard);

	return ret;
}

static void wait_repacker_completion(struct repacker * repacker)
{
	if (repacker->state & REPACKER_RUNNING) {
		kcond_wait(&repacker->cond, &repacker->guard, 0);
		assert("zam-956", !(repacker->state & REPACKER_RUNNING));
	}
}

static int start_repacker(struct repacker * repacker) 
{
	spin_lock(&repacker->guard);
	if (!(repacker->state & REPACKER_DESTROY)) {
		repacker->state &= ~REPACKER_STOP;
		if (!(repacker->state & REPACKER_RUNNING)) {
			repacker->state |= REPACKER_RUNNING;
			spin_unlock(&repacker->guard);
			kernel_thread(repacker_d, repacker, CLONE_VM | CLONE_FS | CLONE_FILES);
			return 0;
		}
	}
	spin_unlock(&repacker->guard);
	return 0;
} 

static void stop_repacker(struct repacker * repacker)
{
	spin_lock(&repacker->guard);
	repacker->state |= REPACKER_STOP;
	spin_unlock(&repacker->guard);
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
	return snprintf(buf, PAGE_SIZE , "%d", check_repacker_state(repacker, REPACKER_RUNNING));
}

static ssize_t repacker_attr_store (struct kobject *kobj, struct attribute *attr,  const char *buf, size_t size)
{
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);
	int start_stop = 0;

	sscanf(buf, "%d", &start_stop);
	if (start_stop) 
		start_repacker(repacker);
	else
		stop_repacker(repacker);

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

	spin_lock_init(&sinfo->repacker->guard);
	kcond_init(&sinfo->repacker->cond);

	return init_repacker_sysfs_iface(super);
}

void done_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);
	struct repacker * repacker;

	repacker = sinfo->repacker;
	assert("zam-945", repacker != NULL);
	done_repacker_sysfs_iface(super);

	spin_lock(&repacker->guard);
	repacker->state |= (REPACKER_STOP | REPACKER_DESTROY);
	wait_repacker_completion(repacker);
	spin_unlock(&repacker->guard);

	kfree(repacker);
	sinfo->repacker = NULL;
}
