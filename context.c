/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Manipulation of reiser4_context */

/*
 * global context used during system call. Variable of this type is allocated
 * on the stack at the beginning of the reiser4 part of the system call and
 * pointer to it is stored in the current->fs_context. This allows us to avoid
 * passing pointer to current transaction and current lockstack (both in
 * one-to-one mapping with threads) all over the call chain.
 *
 * It's kind of like those global variables the prof used to tell you not to
 * use in CS1, except thread specific.;-) Nikita, this was a good idea.
 *
 * In some situations it is desirable to have ability to enter reiser4_context
 * more than once for the same thread (nested contexts). For example, there
 * are some functions that can be called either directly from VFS/VM or from
 * already active reiser4 context (->writepage, for example).
 *
 * In such situations "child" context acts like dummy: all activity is
 * actually performed in the top level context, and get_current_context()
 * always returns top level context.
 * Of course, reiser4_init_context()/reiser4_done_context() have to be properly
 * nested any way.
 *
 * Note that there is an important difference between reiser4 uses
 * ->fs_context and the way other file systems use it. Other file systems
 * (ext3 and reiserfs) use ->fs_context only for the duration of _transaction_
 * (this is why ->fs_context was initially called ->journal_info). This means,
 * that when ext3 or reiserfs finds that ->fs_context is not NULL on the entry
 * to the file system, they assume that some transaction is already underway,
 * and usually bail out, because starting nested transaction would most likely
 * lead to the deadlock. This gives false positives with reiser4, because we
 * set ->fs_context before starting transaction.
 */

#include "debug.h"
#include "super.h"
#include "context.h"
#include "vfs_ops.h"	/* for reiser4_throttle_write() */

#include <linux/writeback.h> /* for current_is_pdflush() */
#include <linux/hardirq.h>

/**
 * Allocate per-subvolume sets of grabbed blocks
 * FIXME-EDWARD: This set is not optimal. Replace it with RB-tree,
 * which provides better scalability
 */
int reiser4_init_context_tail(reiser4_context *ctx,
			      reiser4_super_info_data *sbinfo)
{
	assert("edward-1731", sbinfo != NULL);
	assert("edward-1732", sbinfo->vol != NULL);
	assert("edward-1733", ctx->ctx_grabbed_blocks == NULL);

	ctx->ctx_num_origins = sbinfo_num_origins(sbinfo);

	ctx->ctx_grabbed_blocks = kzalloc((ctx->ctx_num_origins) *
					  sizeof(*ctx->ctx_grabbed_blocks),
					  GFP_KERNEL);
	if (!ctx->ctx_grabbed_blocks)
		return RETERR(-ENOMEM);
	return 0;
}

static int _reiser4_init_context(reiser4_context *context,
				 struct super_block *super)
{
	context->super = super;
	context->magic = context_magic;
	context->outer = current->journal_info;
	current->journal_info = (void *)context;
	context->nr_children = 0;
	context->gfp_mask = GFP_KERNEL;

	init_lock_stack(&context->stack);

	reiser4_txn_begin(context);

	/* initialize head of tap list */
	INIT_LIST_HEAD(&context->taps);
#if REISER4_DEBUG
	context->task = current;
#endif
	grab_space_enable();

	if (get_super_private(super) == NULL ||
	    get_super_private(super)->vol == NULL)
		/*
		 * initialization has to be completed later
		 */
		return 0;
	return reiser4_init_context_tail(context,
					 get_super_private(super));
}

/**
 * initialize context and bind it to the current thread
 * This function should be called at the beginning of reiser4 part of syscall.
 */
reiser4_context *reiser4_init_context(struct super_block *super)
{
	int ret;
	reiser4_context *context;

	assert("nikita-2662", !in_interrupt() && !in_irq());
	assert("nikita-3357", super != NULL);
	assert("nikita-3358", super->s_op == NULL || is_reiser4_super(super));

	context = get_current_context_check();
	if (context && context->super == super) {
		context = (reiser4_context *) current->journal_info;
		context->nr_children++;
		return context;
	}
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (context == NULL)
		return ERR_PTR(RETERR(-ENOMEM));
	ret = _reiser4_init_context(context, super);
	if (ret) {
		kfree(context);
		return ERR_PTR(RETERR(ret));
	}
	return context;
}

/**
 * This is used in scan_mgr which is called with spinlock held and in
 * reiser4_fill_super magic.
 *
 * FIXME-EDWARD: This function is called in critical places where it is not
 * allowed to fail.
 */
int init_stack_context(reiser4_context *context, struct super_block *super)
{
	assert("nikita-2662", !in_interrupt() && !in_irq());
	assert("nikita-3357", super != NULL);
	assert("nikita-3358", super->s_op == NULL || is_reiser4_super(super));
	assert("vs-12", !is_in_reiser4_context());

	memset(context, 0, sizeof(*context));
	context->on_stack = 1;
	return _reiser4_init_context(context, super);
}

/* cast lock stack embedded into reiser4 context up to its container */
reiser4_context *get_context_by_lock_stack(lock_stack * owner)
{
	return container_of(owner, reiser4_context, stack);
}

/* true if there is already _any_ reiser4 context for the current thread */
int is_in_reiser4_context(void)
{
	reiser4_context *ctx;

	ctx = current->journal_info;
	return ctx != NULL && ((unsigned long)ctx->magic) == context_magic;
}

/*
 * call balance dirty pages for the current context.
 *
 * File system is expected to call balance_dirty_pages_ratelimited() whenever
 * it dirties a page. reiser4 does this for unformatted nodes (that is, during
 * write---this covers vast majority of all dirty traffic), but we cannot do
 * this immediately when formatted node is dirtied, because long term lock is
 * usually held at that time. To work around this, dirtying of formatted node
 * simply increases ->nr_marked_dirty counter in the current reiser4
 * context. When we are about to leave this context,
 * balance_dirty_pages_ratelimited() is called, if necessary.
 *
 * This introduces another problem: sometimes we do not want to run
 * balance_dirty_pages_ratelimited() when leaving a context, for example
 * because some important lock (like ->i_mutex on the parent directory) is
 * held. To achieve this, ->nobalance flag can be set in the current context.
 */
static void reiser4_throttle_write_at(reiser4_context *context)
{
	reiser4_super_info_data *sbinfo = get_super_private(context->super);

	/*
	 * call balance_dirty_pages_ratelimited() to process formatted nodes
	 * dirtied during this system call. Do that only if we are not in mount
	 * and there were nodes dirtied in this context and we are not in
	 * writepage (to avoid deadlock) and not in pdflush
	 */
	if (sbinfo != NULL && sbinfo->fake != NULL &&
	    context->nr_marked_dirty != 0 &&
	    !(current->flags & PF_MEMALLOC) &&
	    !current_is_flush_bd_task())
 		reiser4_throttle_write(sbinfo->fake);
}

/* release resources associated with context.

   This function should be called at the end of "session" with reiser4,
   typically just before leaving reiser4 driver back to VFS.

   This is good place to put some degugging consistency checks, like that
   thread released all locks and closed transcrash etc.

*/
static void reiser4_done_context(reiser4_context * context)
				/* context being released */
{
	assert("nikita-860", context != NULL);
	assert("nikita-859", context->magic == context_magic);
	assert("vs-646", (reiser4_context *) current->journal_info == context);
	assert("zam-686", !in_interrupt() && !in_irq());

	/* only do anything when leaving top-level reiser4 context. All nested
	 * contexts are just dummies. */
	if (context->nr_children == 0) {
		assert("jmacd-673", context->trans == NULL);
		assert("jmacd-1002", lock_stack_isclean(&context->stack));
		assert("nikita-1936", reiser4_no_counters_are_held());
		assert("nikita-2626", list_empty_careful(reiser4_taps_list()));
		assert("zam-1004", ergo(get_super_private(context->super),
					get_super_private(context->super)->delete_mutex_owner !=
					current));
		/*
		 * release all grabbed but as yet unused blocks
		 */
		all_grabbed2free();
		/*
		 * synchronize against longterm_unlock_znode():
		 * wake_up_requestor() wakes up requestors without holding
		 * zlock (otherwise they will immediately bump into that lock
		 * after wake up on another CPU). To work around (rare)
		 * situation where requestor has been woken up asynchronously
		 * and managed to run until completion (and destroy its
		 * context and lock stack) before wake_up_requestor() called
		 * wake_up() on it, wake_up_requestor() synchronize on lock
		 * stack spin lock. It has actually been observed that spin
		 * lock _was_ locked at this point, because
		 * wake_up_requestor() took interrupt.
		 */
		spin_lock_stack(&context->stack);
		spin_unlock_stack(&context->stack);

		assert("zam-684", context->nr_children == 0);
		/*
		 * restore original ->fs_context value
		 */
		current->journal_info = context->outer;
		if (context->ctx_grabbed_blocks)
			kfree(context->ctx_grabbed_blocks);
		if (context->on_stack == 0)
			kfree(context);
	} else {
		context->nr_children--;
#if REISER4_DEBUG
		assert("zam-685", context->nr_children >= 0);
#endif
	}
}

/*
 * exit reiser4 context. Call balance_dirty_pages_at() if necessary. Close
 * transaction. Call done_context() to do context related book-keeping.
 */
void reiser4_exit_context(reiser4_context * context)
{
	assert("nikita-3021", reiser4_schedulable());

	if (context->nr_children == 0) {
		if (!context->nobalance)
			reiser4_throttle_write_at(context);

		/* if filesystem is mounted with -o sync or -o dirsync - commit
		   transaction.  FIXME: TXNH_DONT_COMMIT is used to avoid
		   commiting on exit_context when inode semaphore is held and
		   to have ktxnmgrd to do commit instead to get better
		   concurrent filesystem accesses. But, when one mounts with -o
		   sync, he cares more about reliability than about
		   performance. So, for now we have this simple mount -o sync
		   support. */
		if (context->super->s_flags & (MS_SYNCHRONOUS | MS_DIRSYNC)) {
			txn_atom *atom;

			atom = get_current_atom_locked_nocheck();
			if (atom) {
				atom->flags |= ATOM_FORCE_COMMIT;
				context->trans->flags &= ~TXNH_DONT_COMMIT;
				spin_unlock_atom(atom);
			}
		}
		reiser4_txn_end(context);
	}
	reiser4_done_context(context);
}

void reiser4_ctx_gfp_mask_set(void)
{
	reiser4_context *ctx;

	ctx = get_current_context();
	if (ctx->entd == 0 &&
	    list_empty(&ctx->stack.locks) &&
	    ctx->trans->atom == NULL)
		ctx->gfp_mask = GFP_KERNEL;
	else
		ctx->gfp_mask = GFP_NOFS;
}

void reiser4_ctx_gfp_mask_force(gfp_t mask)
{
	reiser4_context *ctx;
	ctx = get_current_context();

	assert("edward-1454", ctx != NULL);

	ctx->gfp_mask = mask;
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
