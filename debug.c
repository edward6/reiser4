/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Debugging/logging/tracing/profiling/statistical facilities. */

#include "kattr.h"
#include "reiser4.h"
#include "context.h"
#include "super.h"

#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>

__u32 reiser4_current_trace_flags = 0;

extern void show_stack(unsigned long *esp);
extern void cond_resched(void);

static char panic_buf[REISER4_PANIC_MSG_BUFFER_SIZE];
static spinlock_t panic_guard = SPIN_LOCK_UNLOCKED;

/* Your best friend. Call it on each occasion.  This is called by
    fs/reiser4/debug.h:reiser4_panic(). */
void
reiser4_do_panic(const char *format /* format string */ , ... /* rest */)
{
	va_list args;

	/* FIXME-NIKITA bust_spinlocks() should go here. Quoting
	   lib/bust_spinlocks.c:
	  
	   bust_spinlocks() clears any spinlocks which would prevent oops,
	   die(), BUG() and panic() information from reaching the user.
	*/
	spin_lock(&panic_guard);
	va_start(args, format);
	vsnprintf(panic_buf, sizeof(panic_buf), format, args);
	va_end(args);
	printk(KERN_EMERG "reiser4 panicked cowardly: %s", panic_buf);
	spin_unlock(&panic_guard);

	DEBUGON(1);

	/* do something more impressive here, print content of
	   get_current_context() */
	if (get_current_context_check() != NULL) {
		struct super_block *super;
		reiser4_context *ctx;

		print_lock_counters("pins held", lock_counters());
		print_contexts();
		ctx = get_current_context();
		super = ctx->super;
		if ((get_super_private(super) != NULL) && reiser4_is_debugged(super, REISER4_VERBOSE_PANIC))
			print_znodes("znodes", current_tree);
#if REISER4_DEBUG
		{
			reiser4_context *top;
			extern spinlock_t active_contexts_lock;

			top = ctx->parent;
			spin_lock(&active_contexts_lock);
			context_list_remove(top);
			spin_unlock(&active_contexts_lock);
		}
#endif
	}

	BUG();
	/* to make gcc happy about noreturn attribute */
	panic(panic_buf);
}

void
reiser4_print_prefix(const char *level, const char *mid,
		     const char *function, const char *file, int lineno)
{
	const char *comm;
	int   pid;

	if (unlikely(in_interrupt() || in_irq())) {
		comm = "interrupt";
		pid  = 0;
	} else {
		comm = current->comm;
		pid  = current->pid;
	}
	printk("%s reiser4[%.16s(%i)]: %s (%s:%i)[%s]:\n",
	       level, comm, pid, function, file, lineno, mid);
	report_err();
}

/* Preemption point: this should be called periodically during long running
   operations (carry, allocate, and squeeze are best examples) */
int
preempt_point(void)
{
	assert("nikita-3008", schedulable());
	cond_resched();
	return signal_pending(current);
}

#if REISER4_DEBUG
/* Debugging aid: return struct where information about locks taken by current
   thread is accumulated. This can be used to formulate lock ordering
   constraints and various assertions.
  
*/
lock_counters_info *
lock_counters()
{
	reiser4_context *ctx = get_current_context();
	assert("jmacd-1123", ctx != NULL);
	return &ctx->locks;
}

/* check that no spinlocks are held */
int schedulable (void)
{
	if (REISER4_DEBUG && get_current_context_check() != NULL) {
		lock_counters_info *counters;

		counters = lock_counters();
		if (counters->spin_locked != 0) {
			print_lock_counters("in atomic", counters);
			return 0;
		}
		return 1;
	}
	might_sleep();
	return 1;
}
#endif

#if REISER4_DEBUG_OUTPUT && REISER4_DEBUG
void
print_lock_counters(const char *prefix, const lock_counters_info * info)
{
	printk("%s: jnode: %i, tree: %i (r:%i,w:%i), dk: %i (r:%i,w:%i)\n"
	       "txnh: %i, atom: %i, stack: %i, txnmgr: %i, "
	       "ktxnmgrd: %i, fq: %i, reiser4_sb: %i\n"
	       "inode: %i, cbk_cache: %i, epoch: %i, eflush: %i\n"
	       "spin: %i, long: %i inode_sem: (r:%i,w:%i)\n"
	       "d: %i, x: %i, t: %i\n", prefix,
	       info->spin_locked_jnode, 
	       info->rw_locked_tree, info->read_locked_tree, 
	       info->write_locked_tree,

	       info->rw_locked_dk, info->read_locked_dk, info->write_locked_dk,

	       info->spin_locked_txnh,
	       info->spin_locked_atom, info->spin_locked_stack,
	       info->spin_locked_txnmgr, info->spin_locked_ktxnmgrd,
	       info->spin_locked_fq, info->spin_locked_super,
	       info->spin_locked_inode_object, 
	       info->spin_locked_cbk_cache,
	       info->spin_locked_epoch,
	       info->spin_locked_super_eflush,
	       info->spin_locked,
	       info->long_term_locked_znode,
	       info->inode_sem_r, info->inode_sem_w,
	       info->d_refs, info->x_refs, info->t_refs);
}
#endif

#if REISER4_DEBUG

/* check_stack() - check for possible stack overflow */
void
check_stack(void)
{
	if (REISER4_DEBUG_STACK) {
		char dummy;
		unsigned gap;
		reiser4_context *context = get_current_context_check();

		if (context == NULL)
			return;
		gap = abs(&dummy - (char *) context);
		if (gap > REISER4_STACK_GAP) {
			warning("nikita-1079", "Stack overflow is close: %i", gap);
		}
#if 0 && REISER4_STATS
		if (gap > 5000 && STS.stack_size_max < gap) {
			warning("nikita-2731", "New stack max: %i", gap);
			dump_stack();

		}
#endif
		if (gap > REISER4_STACK_ABORT) {
			printk("Stack overflow! Starting busyloop\n");
			while ( 1 )
				if ( !in_interrupt() && !in_irq() )
					schedule();
			reiser4_panic("nikita-1080", "Stack overflow: %i", gap);
		}

		reiser4_stat_stack_check_max(gap);
	}
}

#endif

int
reiser4_are_all_debugged(struct super_block *super, __u32 flags)
{
	return (get_super_private(super)->debug_flags & flags) == flags;
}

int
reiser4_is_debugged(struct super_block *super, __u32 flag)
{
	return get_super_private(super)->debug_flags & flag;
}

typedef struct reiser4_stats_cnt {
	reiser4_kattr  kattr;
	ptrdiff_t      offset;
	size_t         size;
	const char    *format;
} reiser4_stats_cnt;

#define getptrat(type, ptr, offset) ((type *)(((char *)(ptr)) + (offset)))
#define getat(type, ptr, offset) (*getptrat(type, ptr, offset))

#define DEFINE_STAT_CNT_0(aname, afield, atype, afmt, aproc)	\
{							\
	.kattr = {					\
		.attr = {				\
			.name = (char *)aname,		\
			.mode = 0444 /* r--r--r-- */	\
		},					\
		.cookie = 0,				\
		.show = aproc				\
	},						\
	.format = afmt "\n",				\
	.offset = offsetof(atype, afield),		\
	.size   = sizeof(((atype *)0)->afield)		\
}

#if REISER4_STATS

static ssize_t 
show_stat_attr(struct super_block * s, reiser4_kattr * kattr, 
	       void * opaque, char * buf)
{
	char *p;
	reiser4_stats_cnt *cnt;
	stat_cnt val;

	(void)opaque;

	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getat(stat_cnt, &get_super_private(s)->stats, cnt->offset);
	p = buf;
	KATTR_PRINT(p, buf, cnt->format, val);
	return (p - buf);
}

static ssize_t 
show_stat_level_attr(struct super_block * s, reiser4_kattr * kattr, 
		     void *da, char * buf)
{
	char *p;
	reiser4_stats_cnt *cnt;
	stat_cnt val;
	int level;

	level = *(int *)da;
	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getat(stat_cnt, &get_super_private(s)->stats.level[level],
		    cnt->offset);
	p = buf;
	KATTR_PRINT(p, buf, cnt->format, val);
	return (p - buf);
}

#define DEFINE_STAT_CNT(field)						\
	DEFINE_STAT_CNT_0(#field, field, reiser4_stat, "%lu", show_stat_attr)

reiser4_stats_cnt reiser4_stat_defs[] = {
	DEFINE_STAT_CNT(tree.cbk),
	DEFINE_STAT_CNT(tree.cbk_found),
	DEFINE_STAT_CNT(tree.cbk_notfound),
	DEFINE_STAT_CNT(tree.cbk_restart),
	DEFINE_STAT_CNT(tree.cbk_cache_hit),
	DEFINE_STAT_CNT(tree.cbk_cache_miss),
	DEFINE_STAT_CNT(tree.cbk_cache_wrong_node),
	DEFINE_STAT_CNT(tree.cbk_cache_race),
	DEFINE_STAT_CNT(tree.pos_in_parent_hit),
	DEFINE_STAT_CNT(tree.pos_in_parent_miss),
	DEFINE_STAT_CNT(tree.pos_in_parent_set),
	DEFINE_STAT_CNT(tree.fast_insert),
	DEFINE_STAT_CNT(tree.fast_paste),
	DEFINE_STAT_CNT(tree.fast_cut),
	DEFINE_STAT_CNT(tree.reparenting),
	DEFINE_STAT_CNT(tree.rd_key_skew),
	DEFINE_STAT_CNT(tree.multikey_restart),
	DEFINE_STAT_CNT(tree.check_left_nonuniq),
	DEFINE_STAT_CNT(tree.left_nonuniq_found),

	DEFINE_STAT_CNT(vfs_calls.lookup),
	DEFINE_STAT_CNT(vfs_calls.create),
	DEFINE_STAT_CNT(vfs_calls.mkdir),
	DEFINE_STAT_CNT(vfs_calls.symlink),
	DEFINE_STAT_CNT(vfs_calls.mknod),
	DEFINE_STAT_CNT(vfs_calls.rename),
	DEFINE_STAT_CNT(vfs_calls.readlink),
	DEFINE_STAT_CNT(vfs_calls.follow_link),
	DEFINE_STAT_CNT(vfs_calls.setattr),
	DEFINE_STAT_CNT(vfs_calls.getattr),
	DEFINE_STAT_CNT(vfs_calls.read),
	DEFINE_STAT_CNT(vfs_calls.write),
	DEFINE_STAT_CNT(vfs_calls.truncate),
	DEFINE_STAT_CNT(vfs_calls.statfs),
	DEFINE_STAT_CNT(vfs_calls.bmap),
	DEFINE_STAT_CNT(vfs_calls.link),
	DEFINE_STAT_CNT(vfs_calls.llseek),
	DEFINE_STAT_CNT(vfs_calls.readdir),
	DEFINE_STAT_CNT(vfs_calls.ioctl),
	DEFINE_STAT_CNT(vfs_calls.mmap),
	DEFINE_STAT_CNT(vfs_calls.unlink),
	DEFINE_STAT_CNT(vfs_calls.rmdir),
	DEFINE_STAT_CNT(vfs_calls.alloc_inode),
	DEFINE_STAT_CNT(vfs_calls.destroy_inode),
	DEFINE_STAT_CNT(vfs_calls.delete_inode),
	DEFINE_STAT_CNT(vfs_calls.write_super),
	DEFINE_STAT_CNT(vfs_calls.private_data_alloc),

	DEFINE_STAT_CNT(dir.readdir.calls),
	DEFINE_STAT_CNT(dir.readdir.reset),
	DEFINE_STAT_CNT(dir.readdir.rewind_left),
	DEFINE_STAT_CNT(dir.readdir.left_non_uniq),
	DEFINE_STAT_CNT(dir.readdir.left_restart),
	DEFINE_STAT_CNT(dir.readdir.rewind_right),
	DEFINE_STAT_CNT(dir.readdir.adjust_pos),
	DEFINE_STAT_CNT(dir.readdir.adjust_lt),
	DEFINE_STAT_CNT(dir.readdir.adjust_gt),
	DEFINE_STAT_CNT(dir.readdir.adjust_eq),

	DEFINE_STAT_CNT(file.page_ops.readpage_calls),
	DEFINE_STAT_CNT(file.page_ops.writepage_calls),
	DEFINE_STAT_CNT(file.tail2extent),
	DEFINE_STAT_CNT(file.extent2tail),
	DEFINE_STAT_CNT(file.find_file_item),
	DEFINE_STAT_CNT(file.find_file_item_via_seal),
	DEFINE_STAT_CNT(file.find_file_item_via_right_neighbor),
	DEFINE_STAT_CNT(file.find_file_item_via_cbk),

	DEFINE_STAT_CNT(extent.unfm_block_reads),
	DEFINE_STAT_CNT(extent.broken_seals),
	DEFINE_STAT_CNT(extent.bdp_caused_repeats),

	DEFINE_STAT_CNT(tail.bdp_caused_repeats),

	DEFINE_STAT_CNT(txnmgr.slept_in_wait_atom),
	DEFINE_STAT_CNT(txnmgr.slept_in_wait_event),
	DEFINE_STAT_CNT(txnmgr.commits),
	DEFINE_STAT_CNT(txnmgr.post_commit_writes),
	DEFINE_STAT_CNT(txnmgr.time_spent_in_commits),
	DEFINE_STAT_CNT(txnmgr.raced_with_truncate),
	DEFINE_STAT_CNT(txnmgr.empty_bio),
	DEFINE_STAT_CNT(txnmgr.commit_from_writepage),

	DEFINE_STAT_CNT(flush.squeezed_completely),
	DEFINE_STAT_CNT(flush.flushed_with_unallocated),
	DEFINE_STAT_CNT(flush.squeezed_leaves),
	DEFINE_STAT_CNT(flush.squeezed_leaf_items),
	DEFINE_STAT_CNT(flush.squeezed_leaf_bytes),
	DEFINE_STAT_CNT(flush.flush),
	DEFINE_STAT_CNT(flush.left),
	DEFINE_STAT_CNT(flush.right),
	DEFINE_STAT_CNT(flush.slept_in_mtflush_sem),

	DEFINE_STAT_CNT(pool.alloc),
	DEFINE_STAT_CNT(pool.kmalloc),

	DEFINE_STAT_CNT(seal.perfect_match),
	DEFINE_STAT_CNT(seal.key_drift),
	DEFINE_STAT_CNT(seal.out_of_cache),
	DEFINE_STAT_CNT(seal.wrong_node),
	DEFINE_STAT_CNT(seal.didnt_move),
	DEFINE_STAT_CNT(seal.found),

	DEFINE_STAT_CNT(hashes.znode.lookup),
	DEFINE_STAT_CNT(hashes.znode.insert),
	DEFINE_STAT_CNT(hashes.znode.remove),
	DEFINE_STAT_CNT(hashes.znode.scanned),
	DEFINE_STAT_CNT(hashes.zfake.lookup),
	DEFINE_STAT_CNT(hashes.zfake.insert),
	DEFINE_STAT_CNT(hashes.zfake.remove),
	DEFINE_STAT_CNT(hashes.zfake.scanned),
	DEFINE_STAT_CNT(hashes.jnode.lookup),
	DEFINE_STAT_CNT(hashes.jnode.insert),
	DEFINE_STAT_CNT(hashes.jnode.remove),
	DEFINE_STAT_CNT(hashes.jnode.scanned),
	DEFINE_STAT_CNT(hashes.lnode.lookup),
	DEFINE_STAT_CNT(hashes.lnode.insert),
	DEFINE_STAT_CNT(hashes.lnode.remove),
	DEFINE_STAT_CNT(hashes.lnode.scanned),
	DEFINE_STAT_CNT(hashes.eflush.lookup),
	DEFINE_STAT_CNT(hashes.eflush.insert),
	DEFINE_STAT_CNT(hashes.eflush.remove),
	DEFINE_STAT_CNT(hashes.eflush.scanned),

	DEFINE_STAT_CNT(wff.asked),
	DEFINE_STAT_CNT(wff.iteration),
	DEFINE_STAT_CNT(wff.wait_flush),
	DEFINE_STAT_CNT(wff.kicked),
	DEFINE_STAT_CNT(wff.cleaned),
	DEFINE_STAT_CNT(wff.skipped_ent),
	DEFINE_STAT_CNT(wff.skipped_last),
	DEFINE_STAT_CNT(wff.skipped_congested),
	DEFINE_STAT_CNT(wff.low_priority),
	DEFINE_STAT_CNT(wff.removed),
	DEFINE_STAT_CNT(wff.toolong),

	DEFINE_STAT_CNT(non_uniq),
	DEFINE_STAT_CNT(non_uniq_max),
	DEFINE_STAT_CNT(stack_size_max),

	DEFINE_STAT_CNT(pcwb_calls),
	DEFINE_STAT_CNT(pcwb_formatted),
	DEFINE_STAT_CNT(pcwb_unformatted),
	DEFINE_STAT_CNT(pcwb_no_jnode),
	DEFINE_STAT_CNT(pcwb_ented),
	DEFINE_STAT_CNT(pcwb_not_written),
	DEFINE_STAT_CNT(pcwb_written)
};

#define DEFINE_STAT_LEVEL_CNT(field)						\
	DEFINE_STAT_CNT_0(#field, field, 					\
			  reiser4_level_stat, "%lu", show_stat_level_attr)

reiser4_stats_cnt reiser4_stat_level_defs[] = {
	DEFINE_STAT_LEVEL_CNT(carry_restart),
	DEFINE_STAT_LEVEL_CNT(carry_done),
	DEFINE_STAT_LEVEL_CNT(carry_left_in_carry),
	DEFINE_STAT_LEVEL_CNT(carry_left_in_cache),
	DEFINE_STAT_LEVEL_CNT(carry_left_missed),
	DEFINE_STAT_LEVEL_CNT(carry_left_not_avail),
	DEFINE_STAT_LEVEL_CNT(carry_left_refuse),
	DEFINE_STAT_LEVEL_CNT(carry_right_in_carry),
	DEFINE_STAT_LEVEL_CNT(carry_right_in_cache),
	DEFINE_STAT_LEVEL_CNT(carry_right_missed),
	DEFINE_STAT_LEVEL_CNT(carry_right_not_avail),
	DEFINE_STAT_LEVEL_CNT(insert_looking_left),
	DEFINE_STAT_LEVEL_CNT(insert_looking_right),
	DEFINE_STAT_LEVEL_CNT(insert_alloc_new),
	DEFINE_STAT_LEVEL_CNT(insert_alloc_many),
	DEFINE_STAT_LEVEL_CNT(insert),
	DEFINE_STAT_LEVEL_CNT(delete),
	DEFINE_STAT_LEVEL_CNT(cut),
	DEFINE_STAT_LEVEL_CNT(paste),
	DEFINE_STAT_LEVEL_CNT(extent),
	DEFINE_STAT_LEVEL_CNT(paste_restarted),
	DEFINE_STAT_LEVEL_CNT(update),
	DEFINE_STAT_LEVEL_CNT(modify),
	DEFINE_STAT_LEVEL_CNT(half_split_race),
	DEFINE_STAT_LEVEL_CNT(dk_vs_create_race),
	DEFINE_STAT_LEVEL_CNT(track_lh),
	DEFINE_STAT_LEVEL_CNT(sibling_search),
	DEFINE_STAT_LEVEL_CNT(cbk_key_moved),
	DEFINE_STAT_LEVEL_CNT(cbk_met_ghost),
	DEFINE_STAT_LEVEL_CNT(page_try_release),
	DEFINE_STAT_LEVEL_CNT(page_released),
	DEFINE_STAT_LEVEL_CNT(emergency_flush),

	DEFINE_STAT_LEVEL_CNT(jnode.jload),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_already),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_page),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_async),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_read),

	DEFINE_STAT_LEVEL_CNT(znode.lock),
	DEFINE_STAT_LEVEL_CNT(znode.lock_iteration),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor_iteration),
	DEFINE_STAT_LEVEL_CNT(znode.lock_read),
	DEFINE_STAT_LEVEL_CNT(znode.lock_write),
	DEFINE_STAT_LEVEL_CNT(znode.lock_lopri),
	DEFINE_STAT_LEVEL_CNT(znode.lock_hipri),
	DEFINE_STAT_LEVEL_CNT(znode.lock_contented),
	DEFINE_STAT_LEVEL_CNT(znode.lock_uncontented),
	DEFINE_STAT_LEVEL_CNT(znode.unlock),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_found),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_found_read),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_scan),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_convoy),

	DEFINE_STAT_LEVEL_CNT(time_slept),
	DEFINE_STAT_LEVEL_CNT(total_hits_at_level)
};

static void
print_cnt(reiser4_stats_cnt * cnt, const char * prefix, void * base)
{
	printk("%s%s:\t ", prefix, cnt->kattr.attr.name);
	printk(cnt->format, getat(stat_cnt, base, cnt->offset));
}

/* Print statistical data accumulated so far. */
void
reiser4_print_stats()
{
	reiser4_stat *s;
	int i;

	s = &get_current_super_private()->stats;
	for(i = 0 ; i < sizeof_array(reiser4_stat_defs) ; ++ i)
		print_cnt(&reiser4_stat_defs[i], "", s);

	for (i = 0; i < REAL_MAX_ZTREE_HEIGHT; ++i) {
		int j;

		if (s->level[i].total_hits_at_level <= 0)
			continue;
		printk("tree: at level: %i\n", i +  LEAF_LEVEL);
		for(j = 0 ; j < sizeof_array(reiser4_stat_level_defs) ; ++ j)
			print_cnt(&reiser4_stat_level_defs[j], "\t", &s->level[i]);
	}
}

int
reiser4_populate_kattr_level_dir(struct kobject * kobj)
{
	int result;
	int i;

	result = 0;
	for(i = 0 ; i < sizeof_array(reiser4_stat_level_defs) && !result ; ++ i)
		result = sysfs_create_file(kobj,
					   &reiser4_stat_level_defs[i].kattr.attr);
	if (result != 0)
		warning("nikita-2921", "Failed to add sysfs level attr: %i, %i",
			result, i);
	return result;
}

#else
void
reiser4_print_stats()
{
}
#endif

int
reiser4_populate_kattr_dir(struct kobject * kobj)
{
	int result;
	int i;

	result = 0;
#if REISER4_STATS
	for(i = 0 ; i < sizeof_array(reiser4_stat_defs) && !result ; ++ i)
		result = sysfs_create_file(kobj,
					  &reiser4_stat_defs[i].kattr.attr);

#endif
	if (result != 0)
		warning("nikita-2920", "Failed to add sysfs attr: %i, %i",
			result, i);
	return result;
}


/* tracing setup: global trace flags stored in global variable plus
   per-thread trace flags plus per-fs trace flags.
   */
__u32 get_current_trace_flags(void)
{
	__u32 flags;

	flags = reiser4_current_trace_flags;
	if (get_current_context_check() != NULL) {
		flags |= get_current_context()->trace_flags;
		if (get_current_super_private() != NULL)
			flags |= get_current_super_private()->trace_flags;
	}

	return flags;
}

/* allocate memory. This calls kmalloc(), performs some additional checks, and
   keeps track of how many memory was allocated on behalf of current super
   block. */
void *
reiser4_kmalloc(size_t size /* number of bytes to allocate */ ,
		int gfp_flag /* allocation flag */ )
{
	if (REISER4_DEBUG) {
		reiser4_super_info_data *sbinfo;

		sbinfo = get_current_super_private();

		assert("nikita-1407", sbinfo != NULL);
		if (gfp_flag & __GFP_WAIT)
			assert("nikita-3009", schedulable());

		reiser4_spin_lock_sb(sbinfo);
		ON_DEBUG(sbinfo->kmalloc_allocated += size);
		reiser4_spin_unlock_sb(sbinfo);
	}
	return kmalloc(size, gfp_flag);
}

/* release memory allocated by reiser4_kmalloc() and update counter. */
void
reiser4_kfree(void *area /* memory to from */,
	      size_t size UNUSED_ARG /* number of bytes to free */)
{
	assert("nikita-1410", area != NULL);

	kfree(area);
	if (REISER4_DEBUG) {
		reiser4_super_info_data *sbinfo;

		sbinfo = get_current_super_private();

		reiser4_spin_lock_sb(sbinfo);
		assert("nikita-1411", sbinfo != NULL);
		assert("nikita-1412", sbinfo->kmalloc_allocated >= (int) size);
		ON_DEBUG(sbinfo->kmalloc_allocated -= size);
		reiser4_spin_unlock_sb(sbinfo);
	}
}

void
reiser4_kfree_in_sb(void *area /* memory to from */,
		    size_t size UNUSED_ARG /* number of bytes to free */,
		    struct super_block *sb)
{
	assert("nikita-2729", area != NULL);
	kfree(area);
	if (REISER4_DEBUG) {
		reiser4_super_info_data *sbinfo;

		sbinfo = get_super_private(sb);

		reiser4_spin_lock_sb(sbinfo);
		assert("nikita-2730", sbinfo->kmalloc_allocated >= (int) size);
		ON_DEBUG(sbinfo->kmalloc_allocated -= size);
		reiser4_spin_unlock_sb(sbinfo);
	}
}


#if REISER4_DEBUG

int
no_counters_are_held()
{
	lock_counters_info *counters;

	counters = lock_counters();
	return
		(counters->spin_locked_jnode == 0) &&
		(counters->rw_locked_tree == 0) &&
		(counters->read_locked_tree == 0) &&
		(counters->write_locked_tree == 0) &&
		(counters->rw_locked_dk == 0) &&
		(counters->read_locked_dk == 0) &&
		(counters->write_locked_dk == 0) &&
		(counters->spin_locked_txnh == 0) &&
		(counters->spin_locked_atom == 0) &&
		(counters->spin_locked_stack == 0) &&
		(counters->spin_locked_txnmgr == 0) &&
		(counters->spin_locked_inode_object == 0) &&
		(counters->spin_locked == 0) && 
		(counters->long_term_locked_znode == 0) &&
		(counters->inode_sem_r == 0) &&
		(counters->inode_sem_w == 0);
}

int
commit_check_locks()
{
	lock_counters_info *counters;
	int inode_sem_r;
	int inode_sem_w;
	int result;

	counters = lock_counters();
	inode_sem_r = counters->inode_sem_r;
	inode_sem_w = counters->inode_sem_w;

	counters->inode_sem_r = counters->inode_sem_w = 0;
	result = no_counters_are_held();
	counters->inode_sem_r = inode_sem_r;
	counters->inode_sem_w = inode_sem_w;
	return result;
}

void 
return_err(int code, const char *file, int line)
{
	if (code < 0) {
		reiser4_context *ctx = get_current_context();

		if (ctx != NULL) {
			fill_backtrace(ctx->err.path);
			ctx->err.code = code;
			ctx->err.file = file;
			ctx->err.line = line;
		}
	}
}

void 
report_err(void)
{
	reiser4_context *ctx = get_current_context_check();

	if (ctx != NULL) {
		if (ctx->err.code != 0) {
#ifdef CONFIG_FRAME_POINTER
			int i;
			for (i = 0; i < REISER4_BACKTRACE_DEPTH ; ++ i)
				printk("0x%p ", ctx->err.path[i]);
			printk("\n");
#endif
			printk("code: %i at %s:%i ", 
			       ctx->err.code, ctx->err.file, ctx->err.line);
		}
	}
}

#endif

#ifdef CONFIG_FRAME_POINTER
void fill_backtrace(backtrace_path path)
{
	path[0] = __builtin_return_address(1);
	path[1] = __builtin_return_address(2);
	path[2] = __builtin_return_address(3);
	path[3] = __builtin_return_address(4);
	path[4] = __builtin_return_address(5);
	path[5] = __builtin_return_address(6);
}
#endif

#if KERNEL_DEBUGGER
void debugtrap(void)
{
	/* do nothing. Put break point here. */
#ifdef CONFIG_KGDB
	extern void breakpoint(void);
	breakpoint();
#endif
}
#endif


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
