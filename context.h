/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Reiser4 context. See context.c for details. */

#if !defined( __REISER4_CONTEXT_H__ )
#define __REISER4_CONTEXT_H__

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "tap.h"
#include "lock.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>
#include <linux/sched.h>	/* for struct task_struct */
#include <linux/rbtree.h>

/*
 * Info specific for a child (stack element)
 */
struct ctx_stack_info {
	struct ctx_stack_info *next; /* pointer to the next element
					in the stack */
	reiser4_subvol *data_subv; /* This is a hint for update_extents(s).
				      Set by set_current_data_subvol().
				      Check by validate_data_reservation().
				      Unset by clear_current_data_subvol()
				      as soon as it is not needed.
				   */
	/* put here other info specific for reiser4 context nesting level */
};

/*
 * Brick-specific part of context
 */
struct ctx_brick_info {
	struct rb_node node;
	u32 brick_id; /* key */
	reiser4_block_nr grabbed_blocks;
};

/* reiser4 per-thread context */
struct reiser4_context {
	/* magic constant. For identification of reiser4 contexts. */
	__u32 magic;

	/* current lock stack. See lock.[ch]. This is where list of all
	   locks taken by current thread is kept. This is also used in
	   deadlock detection. */
	lock_stack stack;

	/* current transcrash. */
	txn_handle *trans;
	/* transaction handle embedded into reiser4_context. ->trans points
	 * here by default. */
	txn_handle trans_in_ctx;

	/* super block we are working with */
	struct super_block *super;

	/* parent fs activation */
	struct fs_activation *outer;

	/* brick-specific parts of the context for all the bricks which
	   participate in the transaction. Sorted by internal brick ID */
	struct rb_root bricks_info;
	struct ctx_brick_info mcbi; /* pre-allocated meta-brick info */

	/* list of taps currently monitored. See tap.c */
	struct list_head taps;

	/* grabbing space is enabled */
	unsigned int grab_enabled:1;
	/* should be set when we are write dirty nodes to disk in jnode_flush or
	 * reiser4_write_logs() */
	unsigned int writeout_mode:1;
	/* true, if current thread is an ent thread */
	unsigned int entd:1;
	/* true, if balance_dirty_pages() should not be run when leaving this
	 * context. This is used to avoid lengthly balance_dirty_pages()
	 * operation when holding some important resource, like directory
	 * ->i_mutex */
	unsigned int nobalance:1;
	/* this bit is used on reiser4_done_context to decide whether context is
	   kmalloc-ed and has to be kfree-ed */
	unsigned int on_stack:1;
	/* file system is read-only */
	unsigned int ro:1;
	/* replacement of PF_FLUSHER */
	unsigned int flush_bd_task:1;

	/* count non-trivial jnode_set_dirty() calls */
	unsigned long nr_marked_dirty;
	/*
	 * reiser4_writeback_inodes calls (via generic_writeback_sb_inodes)
	 * reiser4_writepages_dispatch for each of dirty inodes.
	 * Reiser4_writepages_dispatch captures pages. When number of pages
	 * captured in one reiser4_writeback_inodes reaches some threshold -
	 * some atoms get flushed
	 */
	int nr_captured;
	int nr_children;	/* number of child contexts */
	struct page *locked_page; /* page that should be unlocked in
				   * reiser4_dirty_inode() before taking
				   * a longterm lock (to not violate
				   * reiser4 lock ordering) */
#if REISER4_DEBUG
	reiser4_lock_cnt_info locks; /* debugging information about reiser4
					locks held by the current thread */
	struct task_struct *task; /* so we can easily find owner of the stack */
	struct list_head flushers_link; /* list of all threads doing
					   flush currently */
	err_site err; /* information about last error encountered by reiser4 */
#endif
	void *vp;
	gfp_t gfp_mask;
	struct hint *hint;
};

extern reiser4_context *get_context_by_lock_stack(lock_stack *);
extern int ctx_brick_info_init_static(void);
extern void ctx_brick_info_done_static(void);
extern int ctx_stack_info_init_static(void);
extern void ctx_stack_info_done_static(void);
extern reiser4_subvol *get_current_data_subvol(void);
extern void set_current_data_subvol(reiser4_subvol *subv);
extern void clear_current_data_subvol(void);
extern struct ctx_brick_info *find_context_brick_info(reiser4_context *ctx,
						      u32 brick_id);
extern int insert_context_brick_info(reiser4_context *ctx,
				     struct ctx_brick_info *data);
extern struct ctx_brick_info *alloc_context_brick_info(void);
extern void free_context_brick_info(struct ctx_brick_info *cbi);
static inline ctx_brick_info *context_meta_brick_info(reiser4_context *ctx)
{
	return &ctx->mcbi;
}

static inline void init_context_brick_info(struct ctx_brick_info *cbi,
					   u32 brick_id)
{
	memset(cbi, 0, sizeof(*cbi));
	RB_CLEAR_NODE(&cbi->node);
	cbi->brick_id = brick_id;
}

/* Debugging helps. */
#if REISER4_DEBUG
extern void print_contexts(void);
#endif

#define current_blocksize reiser4_get_current_sb()->s_blocksize
#define current_blocksize_bits reiser4_get_current_sb()->s_blocksize_bits
#define current_tree(subvol_id) (&(current_origin(subvol_id)->tree))

extern reiser4_context *reiser4_init_context(struct super_block *);
extern void init_stack_context(reiser4_context *, struct super_block *);
extern void reiser4_exit_context(reiser4_context *);

/* magic constant we store in reiser4_context allocated at the stack. Used to
   catch accesses to staled or uninitialized contexts. */
#define context_magic ((__u32) 0x4b1b5d0b)

extern int is_in_reiser4_context(void);

/*
 * return reiser4_context for the thread @tsk
 */
static inline reiser4_context *get_context(const struct task_struct *tsk)
{
	assert("vs-1682",
	       ((reiser4_context *) tsk->journal_info)->magic == context_magic);
	return (reiser4_context *) tsk->journal_info;
}

/*
 * return reiser4 context of the current thread, or NULL if there is none.
 */
static inline reiser4_context *get_current_context_check(void)
{
	if (is_in_reiser4_context())
		return get_context(current);
	else
		return NULL;
}

static inline reiser4_context *get_current_context(void);	/* __attribute__((const)); */

/* return context associated with current thread */
static inline reiser4_context *get_current_context(void)
{
	return get_context(current);
}

static inline gfp_t reiser4_ctx_gfp_mask_get(void)
{
	reiser4_context *ctx;

	ctx = get_current_context_check();
	return (ctx == NULL) ? GFP_KERNEL : ctx->gfp_mask;
}

void reiser4_ctx_gfp_mask_set(void);
void reiser4_ctx_gfp_mask_force (gfp_t mask);

/*
 * true if current thread is in the write-out mode. Thread enters write-out
 * mode during jnode_flush and reiser4_write_logs().
 */
static inline int is_writeout_mode(void)
{
	return get_current_context()->writeout_mode;
}

/*
 * enter write-out mode
 */
static inline void writeout_mode_enable(void)
{
	assert("zam-941", !get_current_context()->writeout_mode);
	get_current_context()->writeout_mode = 1;
}

/*
 * leave write-out mode
 */
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

static inline void grab_space_set_enabled(int enabled)
{
	get_current_context()->grab_enabled = enabled;
}

static inline int is_grab_enabled(reiser4_context * ctx)
{
	return ctx->grab_enabled;
}

/* mark transaction handle in @ctx as TXNH_DONT_COMMIT, so that no commit or
 * flush would be performed when it is closed. This is necessary when handle
 * has to be closed under some coarse semaphore, like i_mutex of
 * directory. Commit will be performed by ktxnmgrd. */
static inline void context_set_commit_async(reiser4_context * context)
{
	context->nobalance = 1;
	context->trans->flags |= TXNH_DONT_COMMIT;
}

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
