/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Reiser4 context */

#if !defined( __REISER4_CONTEXT_H__ )
#define __REISER4_CONTEXT_H__

#include "forward.h"
#include "debug.h"
#include "spin_macros.h"
#include "dformat.h"
#include "tslist.h"
#include "jnode.h"
#include "znode.h"
#include "tap.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>
#include <linux/sched.h>	/* for struct task_struct */

/* list of active lock stacks */
ON_DEBUG(TS_LIST_DECLARE(context);)
ON_DEBUG(TS_LIST_DECLARE(flushers);)

/* global context used during system call. Variable of this type is
   allocated on the stack at the beginning of the reiser4 part of the
   system call and pointer to it is stored in the
   current->fs_context. This allows us to avoid passing pointer to
   current transaction and current lockstack (both in one-to-one mapping
   with threads) all over the call chain.

   It's kind of like those global variables the prof used to tell you
   not to use in CS1, except thread specific.;-) Nikita, this was a
   good idea.

   In some situations it is desirable to have ability to enter reiser4_context
   twice for the same thread (nested contexts). For example, there are some
   functions that can be called either directly from VFS/VM or from already
   active reiser4 context (->writepage, for example).

   In such situations "child" context acts like dummy: all activity is
   actually performed in the top level context, and get_current_context()
   always returns top level context. Of course, init_context()/done_context()
   have to be properly nested any way.
*/
struct reiser4_context {
	/* magic constant. For identification of reiser4 contexts. */
	__u32 magic;

	/* current lock stack. See lock.[ch]. This is where list of all
	   locks taken by current thread is kept. This is also used in
	   deadlock detection. */
	lock_stack stack;

	/* current transcrash. */
	txn_handle *trans;
	txn_handle trans_in_ctx;

	/* super block we are working with.  To get the current tree
	   use &get_super_private (reiser4_get_current_sb ())->tree. */
	struct super_block *super;

	/* parent fs activation */
	struct fs_activation *outer;

	/* per-thread grabbed (for further allocation) blocks counter */
	reiser4_block_nr grabbed_blocks;

	/* per-thread tracing flags. Use reiser4_trace_flags enum to set
	   bits in it. */
	__u32 trace_flags;

	/* parent context */
	reiser4_context *parent;
	tap_list_head taps;

	/* grabbing space is enabled */
	int grab_enabled  :1;
    	/* should be set when we are write dirty nodes to disk in jnode_flush or
	 * reiser4_write_logs() */
	int writeout_mode :1;
	int entd          :1;
	int nobalance     :1;

	/* count non-trivial jnode_set_dirty() calls */
	__u64 nr_marked_dirty;
	unsigned long flush_started;
	unsigned long io_started;

#if REISER4_DEBUG
	/* thread ID */
	__u32 tid;

	/* A link of all active contexts. */
	context_list_link contexts_link;
	lock_counters_info locks;
	int nr_children;	/* number of child contexts */
	struct task_struct *task; /* so we can easily find owner of the stack */
	
	reiser4_block_nr grabbed_initially;
	backtrace_path   grabbed_at;
	flushers_list_link  flushers_link;
	err_site err;
#endif
#if REISER4_DEBUG_NODE
	int disable_node_check;
#endif
};

#if REISER4_DEBUG
TS_LIST_DEFINE(context, reiser4_context, contexts_link);
TS_LIST_DEFINE(flushers, reiser4_context, flushers_link);
#endif

extern reiser4_context *get_context_by_lock_stack(lock_stack *);

/* Debugging helps. */
extern int init_context_mgr(void);
#if REISER4_DEBUG_OUTPUT
extern void print_context(const char *prefix, reiser4_context * ctx);
#else
#define print_context(p,c) noop
#endif

#if REISER4_DEBUG_OUTPUT && REISER4_DEBUG
extern void print_contexts(void);
#else
#define print_contexts() noop
#endif

/* Hans, is this too expensive? */
#define current_tree (&(get_super_private(reiser4_get_current_sb())->tree))
#define current_blocksize reiser4_get_current_sb()->s_blocksize
#define current_blocksize_bits reiser4_get_current_sb()->s_blocksize_bits



extern int init_context(reiser4_context * context, struct super_block *super);
extern void done_context(reiser4_context * context);

/* magic constant we store in reiser4_context allocated at the stack. Used to
   catch accesses to staled or uninitialized contexts. */
#define context_magic ((__u32) 0x4b1b5d0b)

extern int is_in_reiser4_context(void);

/* return context associated with given thread */
static inline reiser4_context *
get_context(const struct task_struct *tsk)
{
	return (reiser4_context *) tsk->fs_context;
}


static inline reiser4_context *
get_current_context_check(void)
{
	if (is_in_reiser4_context())
		return get_context(current);
	else
		return NULL;
}

/* return context associated with current thread */
static inline reiser4_context *
get_current_context(void)
{
	return get_context(current);
}

static inline int is_writeout_mode(void)
{
	return get_current_context()->writeout_mode;
}

static inline void writeout_mode_enable(void)
{
	assert("zam-941", !get_current_context()->writeout_mode);
	get_current_context()->writeout_mode = 1;
}

static inline void writeout_mode_disable(void)
{
	assert("zam-942", get_current_context()->writeout_mode);
	get_current_context()->writeout_mode = 0;
}

static inline void grab_space_enable(void) 
{
	get_current_context()->grab_enabled = 1;
}

static inline void grab_space_disable(void) 
{
	get_current_context()->grab_enabled = 0;
}

static inline void grab_space_set_enabled (int enabled)
{
	get_current_context()->grab_enabled = enabled;
}
 
static inline int is_grab_enabled(reiser4_context *ctx)
{
	return ctx->grab_enabled;
}

#define REISER4_TRACE_CONTEXT (0)

#if REISER4_TRACE_TREE && REISER4_TRACE_CONTEXT
extern int write_in_trace(const char *func, const char *mes);

#define log_entry(super, str)						\
({									\
	if (super != NULL && get_super_private(super) != NULL &&	\
	    get_super_private(super)->trace_file.buf != NULL)		\
		write_in_trace(__FUNCTION__, str);			\
})

#else
#define log_entry(super, str) noop
#endif

extern int reiser4_exit_context(reiser4_context * context);

/* __REISER4_CONTEXT_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
