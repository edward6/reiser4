/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Ent daemon. */

#include "debug.h"
#include "kcond.h"
#include "txnmgr.h"
#include "tree.h"
#include "entd.h"
#include "super.h"
#include "context.h"
#include "reiser4.h"

#include <linux/sched.h>	/* for struct task_struct */
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/time.h>         /* for INITIAL_JIFFIES */

static void entd_flush(struct super_block *super);

#define set_comm(state)					\
	snprintf(current->comm, sizeof(current->comm),	\
	         "ent:%s%s", bdevname(super->s_bdev, buf), (state))

static int
entd(void *arg)
{
	char                buf[BDEVNAME_SIZE];
	struct super_block *super;
	struct task_struct *me;
	entd_context       *ctx;

	super = arg;
	/* standard kernel thread prologue */
	me = current;
	/* reparent_to_init() is done by daemonize() */
	daemonize("ent:%s", bdevname(super->s_bdev, buf));

	/* block all signals */
	spin_lock_irq(&me->sighand->siglock);
	siginitsetinv(&me->blocked, 0);
	recalc_sigpending();
	spin_unlock_irq(&me->sighand->siglock);

	/* do_fork() just copies task_struct into the new
	   thread. ->fs_context shouldn't be copied of course. This shouldn't
	   be a problem for the rest of the code though.
	*/
	me->fs_context = NULL;

	ctx = get_entd_context(super);

	spin_lock(&ctx->guard);
	ctx->tsk = me;
	kcond_broadcast(&ctx->startup);
	spin_unlock(&ctx->guard);
	while (1) {
		int result;

		if (me->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);

		set_comm(".");
		spin_lock(&ctx->guard);
		result = kcond_wait(&ctx->wait, &ctx->guard, 1);

		/* we are asked to exit */
		if (ctx->done) {
			spin_unlock(&ctx->guard);
			break;
		}

		spin_unlock(&ctx->guard);
		set_comm("!");
		if (result == 0)
			entd_flush(super);
		else
			/* some other error */
			warning("nikita-3099", "Error: %i", result);
	}

	complete_and_exit(&ctx->finish, 0);
	/* not reached. */
	return 0;
}

void
init_entd_context(struct super_block *super)
{
	entd_context * ctx;

	assert("nikita-3104", super != NULL);

	ctx = get_entd_context(super);

	xmemset(ctx, 0, sizeof *ctx);
	kcond_init(&ctx->startup);
	kcond_init(&ctx->wait);
	init_completion(&ctx->finish);
	spin_lock_init(&ctx->guard);

	kernel_thread(entd, super, CLONE_VM | CLONE_FS | CLONE_FILES);

	spin_lock(&ctx->guard);
	while (ctx->tsk == NULL)
		kcond_wait(&ctx->startup, &ctx->guard, 0);
	spin_unlock(&ctx->guard);
#if REISER4_DEBUG
	flushers_list_init(&ctx->flushers_list);
#endif
}

void
done_entd_context(struct super_block *super)
{
	entd_context * ctx;

	assert("nikita-3103", super != NULL);

	ctx = get_entd_context(super);

	spin_lock(&ctx->guard);
	ctx->done = 1;
	kcond_signal(&ctx->wait);
	spin_unlock(&ctx->guard);

	/* wait until daemon finishes */
	wait_for_completion(&ctx->finish);
}

void enter_flush(struct super_block *super)
{
	entd_context * ctx;
	reiser4_context * cur;

	assert("nikita-3105", super != NULL);
	assert("nikita-3118",
	       get_current_context()->flush_started == INITIAL_JIFFIES);

	ctx = get_entd_context(super);
	cur = get_current_context();

	spin_lock(&ctx->guard);
	ctx->last_flush = jiffies;
	ctx->flushers += 1;
#if REISER4_DEBUG
	flushers_list_push_front(&ctx->flushers_list, get_current_context());
#endif
	spin_unlock(&ctx->guard);
	cur->flush_started = ctx->last_flush;
	cur->io_started = INITIAL_JIFFIES;
}

static const int decay = 3;

void flush_started_io(void)
{
	entd_context * ctx;
	reiser4_context * cur;
	unsigned long delta;
	unsigned long now;

	cur = get_current_context();
	ctx = get_entd_context(cur->super);

	if (cur->io_started != INITIAL_JIFFIES)
		return;

	now = jiffies;
	delta = now - cur->flush_started;
	assert("nikita-3114", time_after_eq(now, cur->flush_started));
	cur->io_started = now;

	spin_lock(&ctx->guard);
	ctx->timeout = (delta + ((1 << decay) - 1) * ctx->timeout) >> decay;
	spin_unlock(&ctx->guard);
}

void leave_flush(struct super_block *super)
{
	entd_context * ctx;

	assert("nikita-3105", super != NULL);

	ctx = get_entd_context(super);

	spin_lock(&ctx->guard);

	assert("nikita-3117", ctx->flushers > 0);
	assert("nikita-3115", 
	       get_current_context()->flush_started != INITIAL_JIFFIES);

	ctx->flushers -= 1;
	if (ctx->flushers == 0)
		ctx->last_flush = INITIAL_JIFFIES;
#if REISER4_DEBUG
	flushers_list_remove_clean(get_current_context());
	get_current_context()->flush_started = INITIAL_JIFFIES;
#endif
	spin_unlock(&ctx->guard);
}

int get_flushers(struct super_block *super, unsigned long *flush_start)
{
	entd_context    * ctx;
	int result;

	assert("nikita-3106", super != NULL);
	assert("nikita-3108", flush_start != NULL);

	ctx = get_entd_context(super);

	/* NOTE: locking is silly */
	spin_lock(&ctx->guard);
	*flush_start = ctx->last_flush;
	result       = ctx->flushers;
	spin_unlock(&ctx->guard);
	return result;
}

void kick_entd(struct super_block *super)
{
	assert("nikita-3109", super != NULL);

	/* contrary to the popular belief it is completely safe to
	 * signal condition variable without spinlock being held. */
	kcond_signal(&get_entd_context(super)->wait);
}

entd_context *get_entd_context(struct super_block *super)
{
	return &get_super_private(super)->entd;
}

static void entd_flush(struct super_block *super)
{
	long            nr_submitted;
	int             result;
	reiser4_context txn;

	result = init_context(&txn, super);
	if (result != 0)
		reiser4_panic("nikita-3102", "Cannot create context: %i", result);

	txn.entd = 1;

	result = flush_some_atom(&nr_submitted, 0);
	if (result != 0)
		warning("nikita-3100", "Flush failed: %i", result);
	reiser4_exit_context(&txn);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
