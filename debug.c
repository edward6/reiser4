/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Debugging/logging/tracing/profiling/statistical facilities. */

#include "kattr.h"
#include "debug.h"
#include "super.h"
#include "znode.h"
#include "super.h"
#include "reiser4.h"

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

	/* do something more impressive here, print content of
	   get_current_context() */
	if (get_current_context() != NULL) {
		struct super_block *super;

		print_lock_counters("pins held", lock_counters());
		print_contexts();
		super = get_current_context()->super;
		if ((get_super_private(super) != NULL) && reiser4_is_debugged(super, REISER4_VERBOSE_PANIC))
			print_znodes("znodes", current_tree);
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
}

/* Preemption point: this should be called periodically during long running
   operations (carry, allocate, and squeeze are best examples) */
int
preempt_point(void)
{
	schedulable();
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
void schedulable (void)
{
	if (REISER4_DEBUG && get_current_context() != NULL) {
		lock_counters_info *counters;

		counters = lock_counters();
		if (counters->spin_locked != 0)
			print_lock_counters("in atomic", counters);
		assert ("zam-782", counters->spin_locked == 0);
	}
	might_sleep();
}

#endif

#if REISER4_DEBUG_OUTPUT && REISER4_DEBUG
void
print_lock_counters(const char *prefix, const lock_counters_info * info)
{
	info
	    ("%s: jnode: %i, tree: %i (%i:%i), dk: %i, txnh: %i, atom: %i, stack: %i, txnmgr: %i, "
	     "ktxnmgrd: %i, fq: %i, reiser4_sb: %i, "
	     "inode: %i, spin: %i, long: %i\n" "d: %i, x: %i, t: %i\n", prefix,
	     info->spin_locked_jnode, 
	     info->rw_locked_tree, info->read_locked_tree, 
	     info->write_locked_tree,
	     info->spin_locked_dk, info->spin_locked_txnh,
	     info->spin_locked_atom, info->spin_locked_stack,
	     info->spin_locked_txnmgr, info->spin_locked_ktxnmgrd,
	     info->spin_locked_fq, info->spin_locked_super,
	     info->spin_locked_inode, info->spin_locked,
	     info->long_term_locked_znode, info->d_refs, info->x_refs, info->t_refs);
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
		reiser4_context *context = get_current_context();

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

#define LEFT(p, buf) (PAGE_SIZE - (p - buf) - 1)
#define PRINT(...) ({ p += snprintf(p, LEFT(p, buf) , ## __VA_ARGS__); })

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
			.name = aname,			\
			.mode = 0444 /* r--r--r-- */	\
		},					\
		.cookie = 0,				\
		.show = aproc				\
	},						\
	.format = afmt "\n",				\
	.offset = offsetof(atype, afield),		\
	.size   = sizeof(((atype *)0)->afield)		\
}

#if REISER4_PROF

#ifdef CONFIG_FRAME_POINTER
static void 
update_prof_trace(reiser4_prof_cnt *cnt)
{
	int i;
	int minind;
	__u64 minhit;
	unsigned long hash;
	void *bt[REISER4_PROF_TRACE_DEPTH];

	bt[0] = __builtin_return_address(2);
	bt[1] = __builtin_return_address(3);
	bt[2] = __builtin_return_address(4);
	bt[3] = __builtin_return_address(5);
	bt[4] = __builtin_return_address(6);
	bt[5] = __builtin_return_address(7);

	for (i = 0, hash = 0 ; i < REISER4_PROF_TRACE_DEPTH ; ++ i) {
		hash <<= 4;
		hash ^= (((unsigned long)bt[i]) >> 2);
	}
	minhit = ~0ull;
	minind = 0;
	for (i = 0 ; i < REISER4_PROF_TRACE_NUM ; ++ i) {
		if (hash == cnt->bt[i].hash) {
			++ cnt->bt[i].hits;
			return;
		}
		if (cnt->bt[i].hits < minhit) {
			minhit = cnt->bt[i].hits;
			minind = i;
		}
	}
	memcpy(&cnt->bt[minind].trace, &bt, sizeof bt);
	cnt->bt[minind].hash = hash;
	cnt->bt[minind].hits = 1;
}
#else
#define update_prof_trace(cnt) noop
#endif

void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now, 
		     unsigned long swtch_mark)
{
	__u64 delta;

	delta = now - then;
	cnt->nr ++;
	cnt->total += delta;
	cnt->max = max(cnt->max, delta);
	if (swtch_mark == nr_context_switches()) {
		cnt->noswtch_nr ++;
		cnt->noswtch_total += delta;
		cnt->noswtch_max = max(cnt->noswtch_max, delta);
	}
	update_prof_trace(cnt);
}

static ssize_t 
show_prof_attr(struct super_block * s, reiser4_kattr * kattr, 
	       void * opaque, char * buf)
{
	char *p;
	reiser4_stats_cnt *cnt;
	reiser4_prof_cnt  *val;
	int i;
	(void)opaque;

	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getptrat(reiser4_prof_cnt, 
		       &get_super_private(s)->prof, cnt->offset);
	p = buf;
	PRINT("%llu %llu %llu %llu %llu %llu\n",
	      val->nr, val->total, val->max,
	      val->noswtch_nr, val->noswtch_total, val->noswtch_max);
#ifdef CONFIG_FRAME_POINTER
	for (i = 0 ; i < REISER4_PROF_TRACE_NUM ; ++ i) {
		int j;

		p += snprintf(p, LEFT(p, buf), "\t%llu: ", val->bt[i].hits);
		for (j = 0 ; j < REISER4_PROF_TRACE_DEPTH ; ++ j) {
			char         *module;
			const char   *name;
			char          namebuf[128];
			unsigned long address;
			unsigned long offset;
			unsigned long size;

			address = (unsigned long) val->bt[i].trace[j];
			name = kallsyms_lookup(address, &size, 
					       &offset, &module, namebuf);
			PRINT("0x%lx ", address);
			if (name != NULL)
				PRINT("%s+%lx/%lx [%s] ", name, offset, size,
				      module ? : "core");
		}
		PRINT("\n");
	}
#endif
	return (p - buf);
}

#define DEFINE_PROF_CNT(field)						\
	DEFINE_STAT_CNT_0("prof." #field, 				\
			  field, reiser4_prof, "", show_prof_attr)

reiser4_stats_cnt reiser4_prof_defs[] = {
	DEFINE_PROF_CNT(jload),
	DEFINE_PROF_CNT(carry),
	DEFINE_PROF_CNT(flush_alloc),
	DEFINE_PROF_CNT(forward_squalloc),
	DEFINE_PROF_CNT(load_page)
};

void calibrate_prof(void)
{
	__u64 start;
	__u64 end;

	rdtscll(start);
	schedule_timeout(HZ/100);
	rdtscll(end);
	warning("nikita-2923", "1 sec. == %llu rdtsc.", (end - start) * 100);
}

/* REISER4_PROF */
#endif

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
	PRINT(cnt->format, val);
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
	PRINT(cnt->format, val);
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
	DEFINE_STAT_CNT(vfs_calls.drop_inode),
	DEFINE_STAT_CNT(vfs_calls.delete_inode),
	DEFINE_STAT_CNT(vfs_calls.write_super),

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

	DEFINE_STAT_CNT(file.wait_on_page),
	DEFINE_STAT_CNT(file.fsdata_alloc),
	DEFINE_STAT_CNT(file.private_data_alloc),
	DEFINE_STAT_CNT(file.tail2extent),
	DEFINE_STAT_CNT(file.extent2tail),
	DEFINE_STAT_CNT(file.find_next_item),
	DEFINE_STAT_CNT(file.find_next_item_via_seal),
	DEFINE_STAT_CNT(file.find_next_item_via_right_neighbor),
	DEFINE_STAT_CNT(file.find_next_item_via_cbk),

	DEFINE_STAT_CNT(extent.unfm_block_reads),
	DEFINE_STAT_CNT(extent.broken_seals),
	DEFINE_STAT_CNT(extent.bdp_caused_repeats),

	DEFINE_STAT_CNT(tail.bdp_caused_repeats),

	DEFINE_STAT_CNT(txnmgr.slept_in_wait_atom),
	DEFINE_STAT_CNT(txnmgr.slept_in_wait_event),
	DEFINE_STAT_CNT(txnmgr.commits),
	DEFINE_STAT_CNT(txnmgr.post_commit_writes),
	DEFINE_STAT_CNT(txnmgr.time_spent_in_commits),

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

	DEFINE_STAT_CNT(non_uniq),
	DEFINE_STAT_CNT(non_uniq_max),
	DEFINE_STAT_CNT(stack_size_max)
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
	DEFINE_STAT_LEVEL_CNT(long_term_lock_contented),
	DEFINE_STAT_LEVEL_CNT(long_term_lock_uncontented),

	DEFINE_STAT_LEVEL_CNT(jnode.jload),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_already),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_page),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_async),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_read),

	DEFINE_STAT_LEVEL_CNT(znode.lock_znode),
	DEFINE_STAT_LEVEL_CNT(znode.lock_znode_iteration),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor_iteration),
	DEFINE_STAT_LEVEL_CNT(time_slept),
	DEFINE_STAT_LEVEL_CNT(total_hits_at_level)
};

static void
print_cnt(reiser4_stats_cnt * cnt, const char * prefix, void * base)
{
	info("%s%s:\t ", prefix, cnt->kattr.attr.name);
	info(cnt->format, getat(stat_cnt, base, cnt->offset));
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
		info("tree: at level: %i\n", i +  LEAF_LEVEL);
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
#if REISER4_PROF
	for(i = 0 ; i < sizeof_array(reiser4_prof_defs) && !result ; ++ i)
		result = sysfs_create_file(kobj,
					  &reiser4_prof_defs[i].kattr.attr);
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
	if (get_current_context() != NULL) {
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
		struct super_block *super;

		super = reiser4_get_current_sb();
		assert("nikita-1407", super != NULL);
		if (gfp_flag & __GFP_WAIT)
			schedulable();

		reiser4_spin_lock_sb(super);
		ON_DEBUG(get_super_private(super)->kmalloc_allocated += size);
		reiser4_spin_unlock_sb(super);
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
		struct super_block *super;
		reiser4_super_info_data *info;

		super = reiser4_get_current_sb();
		info = get_super_private(super);

		reiser4_spin_lock_sb(super);
		assert("nikita-1411", info != NULL);
		assert("nikita-1412", info->kmalloc_allocated >= (int) size);
		ON_DEBUG(info->kmalloc_allocated -= size);
		reiser4_spin_unlock_sb(super);
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
		reiser4_super_info_data *info;

		info = get_super_private(sb);

		reiser4_spin_lock_sb(sb);
		assert("nikita-2730", info->kmalloc_allocated >= (int) size);
		ON_DEBUG(info->kmalloc_allocated -= size);
		reiser4_spin_unlock_sb(sb);
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
		(counters->spin_locked_dk == 0) &&
		(counters->spin_locked_txnh == 0) &&
		(counters->spin_locked_atom == 0) &&
		(counters->spin_locked_stack == 0) &&
		(counters->spin_locked_txnmgr == 0) &&
		(counters->spin_locked_inode == 0) &&
		(counters->spin_locked == 0) && 
		(counters->long_term_locked_znode == 0);
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
