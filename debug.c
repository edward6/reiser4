/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Debugging/logging/tracing/profiling facilities. */

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
#include <linux/vmalloc.h>

__u32 reiser4_current_trace_flags = 0;

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
	panic("%s", panic_buf);
}

void
reiser4_print_prefix(const char *level, int reperr, const char *mid,
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
	if (reperr)
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
	       "inode: %i, cbk_cache: %i, epoch: %i, eflush: %i, zlock: %i\n"
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
	       info->spin_locked_zlock,
	       info->spin_locked,
	       info->long_term_locked_znode,
	       info->inode_sem_r, info->inode_sem_w,
	       info->d_refs, info->x_refs, info->t_refs);
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
		(counters->spin_locked_zlock == 0) &&
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
			fill_backtrace(&ctx->err.path, 
				       REISER4_BACKTRACE_DEPTH, 0);
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
				printk("0x%p ", ctx->err.path.trace[i]);
			printk("\n");
#endif
			printk("code: %i at %s:%i\n", 
			       ctx->err.code, ctx->err.file, ctx->err.line);
		}
	}
}

#endif

#ifdef CONFIG_FRAME_POINTER
extern int kswapd(void *);

#include <linux/personality.h>
extern struct exec_domain default_exec_domain;
static int is_last_frame(void *addr)
{
	if (addr == NULL)
		return 1;
	/* XXX gross hack */
	else if ((void *)kswapd < addr && addr < (void *)wakeup_kswapd)	
		return 1;
	else
		return 0;
}

void fill_backtrace(backtrace_path *path, int depth, int shift)
{
	int i;
	void *addr;

	cassert(REISER4_BACKTRACE_DEPTH == 6);
	assert("nikita-3229", shift < 6);

	/* long live Duff! */

#define FRAME(nr)						\
	case (nr):						\
		addr  = __builtin_return_address((nr) + 2);	\
		break

	xmemset(path, 0, sizeof *path);
	addr = NULL;
	for (i = 0; i < depth; ++ i) {
		switch(i + shift) {
			FRAME(0);
			FRAME(1);
			FRAME(2);
			FRAME(3);
			FRAME(4);
			FRAME(5);
			FRAME(6);
			FRAME(7);
			FRAME(8);
			FRAME(9);
			FRAME(10);
		default:
			impossible("nikita-3230", "everything is wrong");
		}
		path->trace[i] = addr;
		if (is_last_frame(addr))
			break;
	}
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


#if REISER4_DEBUG
/* debugging aid: jnode invariant */
int
jnode_invariant_f(const jnode * node,
		  char const **msg)
{
#define _ergo(ant, con) 						\
	((*msg) = "{" #ant "} ergo {" #con "}", ergo((ant), (con)))
#define _check(exp) ((*msg) = #exp, (exp))

	return
		_check(node != NULL) &&

		/* [jnode-queued] */

		/* only relocated node can be queued, except that when znode
		 * is being deleted, its JNODE_RELOC bit is cleared */
		_ergo(JF_ISSET(node, JNODE_FLUSH_QUEUED),
		      JF_ISSET(node, JNODE_RELOC) || 
		      JF_ISSET(node, JNODE_HEARD_BANSHEE)) &&

		_check(node->jnodes.prev != NULL) &&
		_check(node->jnodes.next != NULL) &&

		/* [jnode-refs] invariant */

		/* only referenced jnode can be loaded */
		_check(atomic_read(&node->x_count) >= atomic_read(&node->d_count));

}

/* debugging aid: check znode invariant and panic if it doesn't hold */
int
jnode_invariant(const jnode * node, int tlocked, int jlocked)
{
	char const *failed_msg;
	int result;
	reiser4_tree *tree;

	tree = jnode_get_tree(node);

	assert("umka-063312", node != NULL);
	assert("umka-064321", tree != NULL);

	if (!jlocked && !tlocked)
		LOCK_JNODE((jnode *) node);
	if (!tlocked)
		RLOCK_TREE(jnode_get_tree(node));
	result = jnode_invariant_f(node, &failed_msg);
	if (!result) {
		info_jnode("corrupted node", node);
		warning("jmacd-555", "Condition %s failed", failed_msg);
	}
	if (!tlocked)
		RUNLOCK_TREE(jnode_get_tree(node));
	if (!jlocked && !tlocked)
		UNLOCK_JNODE((jnode *) node);
	return result;
}
#endif

#if REISER4_STATS
void reiser4_stat_inc_at_level_jput(const jnode * node)
{
	reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jput);
}

void reiser4_stat_inc_at_level_jputlast(const jnode * node)
{
	reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jputlast);
}
#endif

#if REISER4_DEBUG_OUTPUT

const char *
jnode_type_name(jnode_type type)
{
	switch (type) {
	case JNODE_UNFORMATTED_BLOCK:
		return "unformatted";
	case JNODE_FORMATTED_BLOCK:
		return "formatted";
	case JNODE_BITMAP:
		return "bitmap";
	case JNODE_IO_HEAD:
		return "io head";
	case JNODE_INODE:
		return "inode";
	case LAST_JNODE_TYPE:
		return "last";
	default:{
			static char unknown[30];

			sprintf(unknown, "unknown %i", type);
			return unknown;
		}
	}
}

#define jnode_state_name( node, flag )			\
	( JF_ISSET( ( node ), ( flag ) ) ? ((#flag "|")+6) : "" )

/* debugging aid: output human readable information about @node */
void
info_jnode(const char *prefix /* prefix to print */ ,
	   const jnode * node /* node to print */ )
{
	assert("umka-068", prefix != NULL);

	if (node == NULL) {
		printk("%s: null\n", prefix);
		return;
	}

	printk("%s: %p: state: %lx: [%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i,"
	       " block: %s, d_count: %d, x_count: %d, "
	       "pg: %p, atom: %p, lock: %i:%i, type: %s, ",
	       prefix, node, node->state,
	       jnode_state_name(node, JNODE_PARSED),
	       jnode_state_name(node, JNODE_HEARD_BANSHEE),
	       jnode_state_name(node, JNODE_LEFT_CONNECTED),
	       jnode_state_name(node, JNODE_RIGHT_CONNECTED),
	       jnode_state_name(node, JNODE_ORPHAN),
	       jnode_state_name(node, JNODE_CREATED),
	       jnode_state_name(node, JNODE_RELOC),
	       jnode_state_name(node, JNODE_OVRWR),
	       jnode_state_name(node, JNODE_DIRTY),
	       jnode_state_name(node, JNODE_IS_DYING),
	       jnode_state_name(node, JNODE_MAPPED),
	       jnode_state_name(node, JNODE_EFLUSH),
	       jnode_state_name(node, JNODE_FLUSH_QUEUED),
	       jnode_state_name(node, JNODE_RIP),
	       jnode_state_name(node, JNODE_MISSED_IN_CAPTURE),
	       jnode_state_name(node, JNODE_WRITEBACK),
	       jnode_state_name(node, JNODE_NEW),
	       jnode_state_name(node, JNODE_DKSET),
	       jnode_state_name(node, JNODE_EPROTECTED),
	       jnode_get_level(node), sprint_address(jnode_get_block(node)),
	       atomic_read(&node->d_count), atomic_read(&node->x_count),
	       jnode_page(node), node->atom,
#if REISER4_LOCKPROF
	       node->guard.held, node->guard.trying,
#else
	       0, 0,
#endif
	       jnode_type_name(jnode_get_type(node)));
	if (jnode_is_unformatted(node)) {
		printk("inode: %llu, index: %lu, ", 
		       node->key.j.objectid, node->key.j.index);
	}
}

/* debugging aid: output human readable information about @node */
void
print_jnode(const char *prefix /* prefix to print */ ,
	    const jnode * node /* node to print */)
{
	if (jnode_is_znode(node))
		print_znode(prefix, JZNODE(node));
	else
		info_jnode(prefix, node);
}

/* this is cut-n-paste replica of print_znodes() */
void
print_jnodes(const char *prefix, reiser4_tree * tree)
{
	jnode *node;
	jnode *next;
	j_hash_table *htable;
	int tree_lock_taken;

	if (tree == NULL)
		tree = current_tree;

	/* this is a debugging function. It can be called by reiser4_panic()
	   with tree spin-lock already held. Trylock is not exactly what we
	   want here, but it is passable.
	*/
	tree_lock_taken = write_trylock_tree(tree);
	htable = &tree->jhash_table;

	for_all_in_htable(htable, j, node, next) {
		info_jnode(prefix, node);
		printk("\n");
	}
	if (tree_lock_taken)
		WUNLOCK_TREE(tree);
}
/* NIKITA-FIXME-HANS: find all endifs and add a comment indicating what if they belong to */
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
