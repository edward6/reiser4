/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Manipulation of reiser4_context */

#include "debug.h"
#include "super.h"
#include "context.h"

#include <linux/writeback.h> /* balance_dirty_pages() */

#if REISER4_DEBUG
/* List of all currently active contexts, used for debugging purposes.  */
context_list_head active_contexts;
/* lock protecting access to active_contexts. */
spinlock_t active_contexts_lock;

void
check_contexts(void)
{
	reiser4_context *ctx;

	spin_lock(&active_contexts_lock);
	for_all_type_safe_list(context, &active_contexts, ctx) {
		assert("", ctx->magic == context_magic);
	}
	spin_unlock(&active_contexts_lock);
}

#endif

struct {
	void *task;
	void *context;
	void *path[16];
} context_ok;



reiser4_internal void get_context_ok(reiser4_context *ctx)
{
	int i;
	void *addr = NULL, *frame = NULL;

#define CTX_FRAME(nr)						\
	case (nr):						\
		addr  = __builtin_return_address((nr));	 	\
                frame = __builtin_frame_address(nr);		\
		break

	memset(&context_ok, 0, sizeof(context_ok));

	context_ok.task = current;
	context_ok.context = ctx;
	for (i = 0; i < 16; i ++) {
		switch(i) {
			CTX_FRAME(0);
			CTX_FRAME(1);
			CTX_FRAME(2);
			CTX_FRAME(3);
			CTX_FRAME(4);
			CTX_FRAME(5);
			CTX_FRAME(6);
			CTX_FRAME(7);
			CTX_FRAME(8);
			CTX_FRAME(9);
			CTX_FRAME(10);
			CTX_FRAME(11);
			CTX_FRAME(12);
			CTX_FRAME(13);
			CTX_FRAME(14);
			CTX_FRAME(15);
		default:
			impossible("", "");
		}
		if (frame > (void *)ctx)
			break;
		context_ok.path[i] = addr;
	}
#undef CTX_FRAME
}


/* initialise context and bind it to the current thread

   This function should be called at the beginning of reiser4 part of
   syscall.
*/
reiser4_internal int
init_context(reiser4_context * context	/* pointer to the reiser4 context
					 * being initalised */ ,
	     struct super_block *super	/* super block we are going to
					 * work with */)
{
	assert("nikita-2662", !in_interrupt() && !in_irq());
	assert("nikita-3356", context != NULL);
	assert("nikita-3357", super != NULL);
	assert("nikita-3358", super->s_op == NULL || is_reiser4_super(super));

	xmemset(context, 0, sizeof *context);

	if (is_in_reiser4_context()) {
		reiser4_context *parent;

		parent = (reiser4_context *) current->fs_context;
		/* NOTE-NIKITA this is dubious */
		if (parent->super == super) {
			context->parent = parent;
#if (REISER4_DEBUG)
			++context->parent->nr_children;
#endif
			return 0;
		}
	}

	context->super = super;
	context->magic = context_magic;
	context->outer = current->fs_context;
	current->fs_context = (struct fs_activation *) context;

	init_lock_stack(&context->stack);

	txn_begin(context);

	context->parent = context;
	tap_list_init(&context->taps);
#if REISER4_DEBUG
	context_list_clean(context);	/* to satisfy assertion */
	spin_lock(&active_contexts_lock);
	context_list_check(&active_contexts);
	context_list_push_front(&active_contexts, context);
	/*check_contexts();*/
	spin_unlock(&active_contexts_lock);
	context->task = current;
#endif
	context->flush_started = INITIAL_JIFFIES;

	grab_space_enable();
	log_entry(super, ":in");
	return 0;
}

reiser4_internal reiser4_context *
get_context_by_lock_stack(lock_stack * owner)
{
	return container_of(owner, reiser4_context, stack);
}

reiser4_internal int
is_in_reiser4_context(void)
{
	return
		current->fs_context != NULL &&
		((unsigned long) current->fs_context->owner) == context_magic;
}

static void
balance_dirty_pages_at(reiser4_context * context)
{
	reiser4_super_info_data * sbinfo = get_super_private(context->super);

	if (context->nr_marked_dirty != 0 && sbinfo->fake &&
	    !(current->flags & PF_MEMALLOC) && !current_is_pdflush()) {
		balance_dirty_pages_ratelimited(sbinfo->fake->i_mapping);
	}
}

reiser4_internal int reiser4_exit_context(reiser4_context * context)
{
        int result = 0;

	assert("nikita-3021", schedulable());

	if (context == context->parent) {
		if (!context->nobalance)
			balance_dirty_pages_at(context);
		result = txn_end(context);
	}
	done_context(context);
	return (result > 0) ? 0 : result;
}

/* release resources associated with context.

   This function should be called at the end of "session" with reiser4,
   typically just before leaving reiser4 driver back to VFS.

   This is good place to put some degugging consistency checks, like that
   thread released all locks and closed transcrash etc.

*/
reiser4_internal void
done_context(reiser4_context * context /* context being released */)
{
	reiser4_context *parent;
	assert("nikita-860", context != NULL);

	parent = context->parent;
	assert("nikita-2174", parent != NULL);
	assert("nikita-2093", parent == parent->parent);
	assert("nikita-859", parent->magic == context_magic);
	assert("vs-646", (reiser4_context *) current->fs_context == parent);
	assert("zam-686", !in_interrupt() && !in_irq());
	/* add more checks here */

	if (parent == context) {
		assert("jmacd-673", parent->trans == NULL);
		assert("jmacd-1002", lock_stack_isclean(&parent->stack));
		assert("nikita-1936", no_counters_are_held());
		assert("nikita-3403", !delayed_inode_updates(context->dirty));
		assert("nikita-2626", tap_list_empty(taps_list()));
		assert("zam-1004", get_super_private(context->super)->delete_sema_owner != current);

		log_entry(context->super, ":ex");

		if (context->grabbed_blocks != 0)
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

#if REISER4_DEBUG
		/* remove from active contexts */
		spin_lock(&active_contexts_lock);
		/*check_contexts();*/
		context_list_remove(parent);
		spin_unlock(&active_contexts_lock);

		assert("zam-684", context->nr_children == 0);
#endif
		current->fs_context = context->outer;
	} else {
#if REISER4_DEBUG
		parent->nr_children--;
		assert("zam-685", parent->nr_children >= 0);
#endif
	}
}

/* Audited by: umka (2002.06.16) */
reiser4_internal int
init_context_mgr(void)
{
#if REISER4_DEBUG
	spin_lock_init(&active_contexts_lock);
	context_list_init(&active_contexts);
#endif
	return 0;
}

#if REISER4_DEBUG_OUTPUT
reiser4_internal void
print_context(const char *prefix, reiser4_context * context)
{
	if (context == NULL) {
		printk("%s: null context\n", prefix);
		return;
	}
#if REISER4_TRACE
	printk("%s: trace_flags: %x\n", prefix, context->trace_flags);
#endif
#if REISER4_DEBUG
	print_lock_counters("\tlocks", &context->locks);
	printk("pid: %i, comm: %s\n", context->task->pid, context->task->comm);
#endif
	print_lock_stack("\tlock stack", &context->stack);
	info_atom("\tatom", context->trans_in_ctx.atom);
}

#if REISER4_DEBUG
void
print_contexts(void)
{
	reiser4_context *context;

	spin_lock(&active_contexts_lock);

	for_all_type_safe_list(context, &active_contexts, context) {
		print_context("context", context);
	}

	spin_unlock(&active_contexts_lock);
}
#endif
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
