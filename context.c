/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Manipulation of reiser4_context */

#include "debug.h"
#include "super.h"
#include "context.h"

#include <linux/writeback.h> /* balance_dirty_pages() */

#if REISER4_DEBUG
/* This list and the two fields that follow maintain the currently active
   contexts, used for debugging purposes.  */

spinlock_t active_contexts_lock;
context_list_head active_contexts;
#endif

/* initialise context and bind it to the current thread
  
   This function should be called at the beginning of reiser4 part of
   syscall.
*/
int
init_context(reiser4_context * context	/* pointer to the reiser4 context
					 * being initalised */ ,
	     struct super_block *super	/* super block we are going to
					 * work with */)
{

	PROF_BEGIN(init_context);

	assert("nikita-2662", !in_interrupt() && !in_irq());

	if (context == NULL || super == NULL) {
		BUG();
	}

	if (super->s_op != NULL && super->s_op != &reiser4_super_operations)
		BUG();

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
			__PROF_END(init_context, 6, 0);
			return 0;
		}
	}

	context->super = super;
#if (REISER4_DEBUG)
	context->tid = current->pid;
#endif
	assert("green-7", super->s_op == NULL || super->s_op == &reiser4_super_operations);

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
	spin_unlock(&active_contexts_lock);
	context->task = current;
#endif
	context->flush_started = INITIAL_JIFFIES;

	grab_space_enable();
	log_entry(super, ":in");
	__PROF_END(init_context, 3, 0);
	return 0;
}

reiser4_context *
get_context_by_lock_stack(lock_stack * owner)
{
	return container_of(owner, reiser4_context, stack);
}

int
is_in_reiser4_context(void)
{
	return current->fs_context != NULL && 
		((__u32) current->fs_context->owner) == context_magic;
}

static void 
balance_dirty_pages_at(reiser4_context * context)
{
	reiser4_super_info_data * sbinfo = get_super_private(context->super);

	if (context->nr_marked_dirty != 0 && sbinfo->fake && 
	    !(current->flags & PF_MEMALLOC) && !current_is_pdflush()) {
		balance_dirty_pages(sbinfo->fake->i_mapping);
	}
}

int reiser4_exit_context(reiser4_context * context)
{
        int result = 0;

	assert("nikita-3021", schedulable());

	if (context == context->parent) {
		if (!(context->nobalance))
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
/* Audited by: umka (2002.06.16) */
void
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
		assert("nikita-2626", tap_list_empty(taps_list()));

		log_entry(context->super, ":ex");

		if (context->grabbed_blocks != 0)
			all_grabbed2free("done_context: free grabbed blocks");
		
		/* synchronize against longterm_unlock_znode(). */
		spin_lock_stack(&context->stack);
		spin_unlock_stack(&context->stack);

#if REISER4_DEBUG
		/* remove from active contexts */
		spin_lock(&active_contexts_lock);
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
int
init_context_mgr(void)
{
#if REISER4_DEBUG
	spin_lock_init(&active_contexts_lock);
	context_list_init(&active_contexts);
#endif
	return 0;
}

#if REISER4_DEBUG_OUTPUT
void
print_context(const char *prefix, reiser4_context * context)
{
	if (context == NULL) {
		printk("%s: null context\n", prefix);
		return;
	}
#if REISER4_CONTEXT
	printk("%s: trace_flags: %x\n", prefix, context->trace_flags);
#endif
#if REISER4_DEBUG
	printk("\ttid: %i\n", context->tid);
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

	for_all_tslist(context, &active_contexts, context) {
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
