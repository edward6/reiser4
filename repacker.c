/* Copyright 2003 by Hans Reiser */

/* 
   The reiser4 repacker.

   It walks the reiser4 tree and marks all nodes (reads them if it is
   necessary) for repacking by setting JNODE_REPACK bit. Also, all nodes which
   have no JNODE_REPACK bit set nodes added to a transaction and marked dirty.
*/

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
	REPACKER_RUNNING       = 0x1,
	REPACKER_STOP          = 0x2,
	REPACKER_DESTROY       = 0x4,
	REPACKER_GOES_BACKWARD = 0x8
};

/* Per super block repacker structure for  */
struct repacker {
	/* Back reference to a super block. */
	struct super_block * super;
	/* Repacker thread state */
	enum repacker_state_bits  state;
	/* A spin lock to protect state */
	spinlock_t guard;
	/* A conditional variable to wait repacker state change. */
	kcond_t    cond;
#if REISER4_USE_SYSFS
	/* An object (kobject), externally visible through SysFS. */
	struct kobject kobj;
#endif
	
};


static inline int check_repacker_state_bit(struct repacker *repacker, enum repacker_state_bits bits)
{
	int result;

	spin_lock(&repacker->guard);
	result = !!(repacker->state & bits);
	spin_unlock(&repacker->guard);

	return result;
}

/* Repacker per tread state and statistics. */
struct repacker_cursor {
	reiser4_blocknr_hint hint;
	int count;
	struct  {
		long znodes_dirtied;
		long jnodes_dirtied;
	} stats;
};

static void repacker_cursor_init (struct repacker_cursor * cursor, struct repacker * repacker)
{
	int backward = check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD);

	blocknr_hint_init(&cursor->hint);
	cursor->hint.backward = backward;
}

static void repacker_cursor_done (struct repacker_cursor * cursor)
{
	blocknr_hint_done(&cursor->hint);
}

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

static int process_znode (tap_t * tap, void * arg)
{
	struct repacker_cursor * cursor = arg;
	znode * node = tap->lh->node;

	assert("zam-954", cursor->count > 0);

	if (check_repacker_state_bit(get_current_super_private()->repacker, REPACKER_STOP))
		return -EINTR;

	if (ZF_ISSET(node, JNODE_REPACK))
		return 0;

	znode_make_dirty(node);
	ZF_SET(node, JNODE_REPACK);

	cursor->stats.znodes_dirtied ++;

	if (-- cursor->count <= 0)
		return -EAGAIN;
	return 0;
}

static int process_extent (tap_t *tap, void * arg)
{
	int ret;
	struct repacker_cursor * cursor = arg;

	if (check_repacker_state_bit(get_current_super_private()->repacker, REPACKER_STOP))
		return -EINTR;

	ret = mark_extent_for_repacking(tap, cursor->count);
	if (ret > 0) {
		cursor->stats.jnodes_dirtied += ret;
		cursor->count -= ret;
		if (cursor->count <= 0)
			     return -EAGAIN;
		return 0;
	}

	return ret;
}

/* The reiser4 repacker process nodes by chunks of REPACKER_CHUNK_SIZE
 * size. */
#define REPACKER_CHUNK_SIZE 100

/* It is for calling by tree walker before taking any locks. */
static int prepare_repacking_session (void * arg)
{
	struct repacker_cursor * cursor = arg;
	int ret;

	assert("zam-951", schedulable());

	all_grabbed2free(__FUNCTION__);
	ret = renew_transaction();
	if (ret)
		return ret;

	cursor->count = REPACKER_CHUNK_SIZE;
	return  reiser4_grab_space((__u64)REPACKER_CHUNK_SIZE, BA_CAN_COMMIT | BA_FORCE, __FUNCTION__);
}

/* When the repacker goes backward (from the rightmost key to the leftmost
 * one), it does relocation of all processed nodes to the end of disk.  Thus
 * repacker does what usually the reiser4 flush does but in backward direction
 * and node squeezing is not supported. */
static int relocate_znode (tap_t * tap, void * arg)
{
	reiser4_blocknr_hint hint;
	lock_handle parent_lock;

	blocknr_hint_init(&hint);
	init_lh(&parent_lock);
	
	blocknr_hint_done(&hint);
	return 0;
}

static int relocate_extent (tap_t * tap, void * arg)
{
	return 0;
}

static struct tree_walk_actor forward_actor = {
	.process_znode  = process_znode,
	.process_extent = process_extent,
	.before         = prepare_repacking_session
};



static struct tree_walk_actor backward_actor = {
	.process_znode  = relocate_znode,
	.process_extent = relocate_extent,
	.before         = prepare_repacking_session
};

static int reiser4_repacker (struct repacker * repacker)
{
	struct repacker_cursor cursor;
	int backward;
	struct tree_walk_actor * actor;
	int ret;

	repacker_cursor_init(&cursor, repacker);

	backward = check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD);
	actor = backward ? &backward_actor : &forward_actor;
	ret = tree_walk(NULL, backward, actor, &cursor);
	printk(KERN_INFO "reiser4 repacker: "
	       "%lu formatted node(s) processed, %lu unformatted node(s) processed, ret = %d\n",
	       cursor.stats.znodes_dirtied, cursor.stats.jnodes_dirtied, ret);

	repacker_cursor_done(&cursor);
	return ret;
}

/* The repacker kernel thread code. */
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

	printk(KERN_INFO "Repacker: I am alive, pid = %u\n", me->pid);
	ret = init_context(&ctx, repacker->super);
	if (!ret) {
		int ret1;

		ret = reiser4_repacker(repacker);
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

struct repacker_attr {
	struct attribute attr;
	ssize_t (*show)(struct repacker *, char * buf);
	ssize_t (*store)(struct repacker *, const char * buf, size_t size);
};

static ssize_t start_attr_show (struct repacker * repacker, char * buf)	
{
	return snprintf(buf, PAGE_SIZE , "%d", check_repacker_state_bit(repacker, REPACKER_RUNNING));
}

static ssize_t start_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	int start_stop = 0;

	sscanf(buf, "%d", &start_stop);
	if (start_stop) 
		start_repacker(repacker);
	else
		stop_repacker(repacker);

	return size;
}

static ssize_t direction_attr_show (struct repacker * repacker, char * buf)	
{
	return snprintf(buf, PAGE_SIZE , "%d", check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD));
}

static ssize_t direction_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	int go_left = 0;

	sscanf(buf, "%d", &go_left);

	spin_lock(&repacker->guard);
	if (!(repacker->state & REPACKER_RUNNING)) {
		if (go_left) 
			repacker->state |= REPACKER_GOES_BACKWARD;
		else
			repacker->state &= ~REPACKER_GOES_BACKWARD;
	}
	spin_unlock(&repacker->guard);
	return size;
}

static struct repacker_attr start_attr = {
	.attr = {
		.name = "start",
		.mode = 0644		/* rw-r--r */
	},
	.show = start_attr_show,
	.store = start_attr_store,
};

static struct repacker_attr direction_attr = {
	.attr = {
		.name = "direction",
		.mode = 0644		/* rw-r--r */
	},
	.show = direction_attr_show,
	.store = direction_attr_store,
};

static struct attribute * repacker_def_attrs[] = {
	&start_attr.attr,
	&direction_attr.attr,
	NULL
}; 

static ssize_t repacker_attr_show (struct kobject *kobj, struct attribute *attr,  char *buf)
{
	struct repacker_attr * r_attr = container_of(attr, struct repacker_attr, attr);
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);

	return r_attr->show(repacker, buf);
}
	
static ssize_t repacker_attr_store (struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	struct repacker_attr * r_attr = container_of(attr, struct repacker_attr, attr);
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);

	return r_attr->store(repacker, buf, size);
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

static int init_repacker_sysfs_interface (struct super_block * s)
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

static void done_repacker_sysfs_interface (struct super_block * s)
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

	return init_repacker_sysfs_interface(super);
}

void done_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);
	struct repacker * repacker;

	repacker = sinfo->repacker;
	assert("zam-945", repacker != NULL);
	done_repacker_sysfs_interface(super);

	spin_lock(&repacker->guard);
	repacker->state |= (REPACKER_STOP | REPACKER_DESTROY);
	wait_repacker_completion(repacker);
	spin_unlock(&repacker->guard);

	kfree(repacker);
	sinfo->repacker = NULL;
}
