/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* Transaction manager daemon. */

/*
 * ktxnmgrd is a kernel daemon responsible for committing transactions. It is
 * needed/important for the following reasons:
 *
 *     1. in reiser4 atom is not committed immediately when last transaction
 *     handle closes, unless atom is either too old or too large (see
 *     atom_should_commit()). This is done to avoid committing too frequently.
 *     because:
 *
 *     2. sometimes we don't want to commit atom when closing last transaction
 *     handle even if it is old and fat enough. For example, because we are at
 *     this point under directory semaphore, and committing would stall all
 *     accesses to this directory.
 *
 * ktxnmgrd binds its time sleeping on condition variable. When is awakes
 * either due to (tunable) timeout or because it was explicitly woken up by
 * call to ktxnmgrd_kick(), it scans list of all atoms and commits ones
 * eligible.
 *
 */

#include "debug.h"
#include "kcond.h"
#include "txnmgr.h"
#include "tree.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"

#include <linux/sched.h>	/* for struct task_struct */
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/writeback.h>

static int scan_mgr(txn_mgr * mgr);

/* change current->comm so that ps, top, and friends will see changed
   state. This serves no useful purpose whatsoever, but also costs
   nothing. May be it will make lonely system administrator feeling less alone
   at 3 A.M.
*/
#define set_comm( state ) 						\
	snprintf( current -> comm, sizeof( current -> comm ),	\
		  "%s:%s", __FUNCTION__, ( state ) )

/* The background transaction manager daemon, started as a kernel thread
   during reiser4 initialization. */
static int
ktxnmgrd(void *arg)
{
	struct task_struct *me;
	ktxnmgrd_context *ctx;

	/* standard kernel thread prologue */
	me = current;
	/* reparent_to_init() is done by daemonize() */
	daemonize(__FUNCTION__);

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

	ctx = arg;
	spin_lock(&ctx->guard);
	ctx->tsk = me;
	kcond_broadcast(&ctx->startup);
	while (1) {
		int result;
		txn_mgr *mgr;

		/* software suspend support. */
		if (me->flags & PF_FREEZE) {
			spin_unlock(&ctx->guard);
			refrigerator(PF_IOTHREAD);
			spin_lock(&ctx->guard);
		}

		set_comm("wait");
		/* wait for @ctx -> timeout or explicit wake up.

		   kcond_wait() is called with last argument 1 enabling wakeup
		   by signals so that this thread is not counted in
		   load-average. This doesn't require any special handling,
		   because all signals were blocked.
		*/
		result = kcond_timedwait(&ctx->wait,
					 &ctx->guard, ctx->timeout, 1);

		/* wake up all threads doing umount. See ktxnmgrd_detach(). */
		kcond_broadcast(&ctx->loop);

		if (result != -ETIMEDOUT && result != -EINTR && result != 0) {
			/* some other error */
			warning("nikita-2443", "Error: %i", result);
			continue;
		}

		/* we are asked to exit */
		if (ctx->done)
			break;

		set_comm(result ? "timed" : "run");

		/* wait timed out or ktxnmgrd was woken up by explicit request
		   to commit something. Scan list of atoms in txnmgr and look
		   for too old atoms.
		*/
		do {
			ctx->rescan = 0;
			for_all_type_safe_list(txn_mgrs, &ctx->queue, mgr) {
				scan_mgr(mgr);
				spin_lock(&ctx->guard);
				if (ctx->rescan) {
					/* the list could be modified while ctx
					   spinlock was released, we have to
					   repeat scanning from the
					   beginning  */
					break;
				}
			}
		} while (ctx->rescan);
	}

	spin_unlock(&ctx->guard);

	complete_and_exit(&ctx->finish, 0);
	/* not reached. */
	return 0;
}

#undef set_comm

reiser4_internal void
init_ktxnmgrd_context(ktxnmgrd_context * ctx)
{
	assert("nikita-2442", ctx != NULL);

	xmemset(ctx, 0, sizeof *ctx);
	init_completion(&ctx->finish);
	kcond_init(&ctx->startup);
	kcond_init(&ctx->wait);
	kcond_init(&ctx->loop);
	spin_lock_init(&ctx->guard);
	ctx->timeout = REISER4_TXNMGR_TIMEOUT;
	txn_mgrs_list_init(&ctx->queue);
}

reiser4_internal int
ktxnmgrd_attach(ktxnmgrd_context * ctx, txn_mgr * mgr)
{
	int first_mgr;

	assert("nikita-2448", mgr != NULL);

	spin_lock(&ctx->guard);

	first_mgr = !ctx->started;
	ctx->started = 1;
	ctx->rescan = 1;

	/* attach @mgr to daemon. Not taking spin-locks, because this is early
	   during @mgr initialization. */
	mgr->daemon = ctx;
	txn_mgrs_list_push_back(&ctx->queue, mgr);

	spin_unlock(&ctx->guard);

	if (first_mgr) {
		/* attaching first mgr, start daemon */
		ctx->done = 0;
		/* kernel_thread never fails. */
		kernel_thread(ktxnmgrd, ctx, CLONE_KERNEL);
	}

	spin_lock(&ctx->guard);

	/* daemon thread is not yet initialized */
	if (ctx->tsk == NULL)
		/* wait until initialization completes */
		kcond_wait(&ctx->startup, &ctx->guard, 0);

	assert("nikita-2452", ctx->tsk != NULL);

	spin_unlock(&ctx->guard);
	return 0;
}

reiser4_internal void
ktxnmgrd_detach(txn_mgr * mgr)
{
	ktxnmgrd_context *ctx;

	assert("nikita-2450", mgr != NULL);

	/* this is supposed to happen when @mgr is quiesced and no locking is
	   necessary. */
	ctx = mgr->daemon;
	if (ctx == NULL)
		return;

	spin_lock(&ctx->guard);
	txn_mgrs_list_remove(mgr);
	mgr->daemon = NULL;
	ctx->rescan = 1;

	/* removing last mgr, stop daemon */
	if (txn_mgrs_list_empty(&ctx->queue)) {
		ctx->tsk = NULL;
		ctx->done = 1;
		ctx->started = 0;
		spin_unlock(&ctx->guard);
		kcond_signal(&ctx->wait);

		/* wait until daemon finishes */
		wait_for_completion(&ctx->finish);
	} else {
		kcond_signal(&ctx->wait);
		/* ctx->loop is signaled by ktxnmgrd() after it woke up, but
		 * before it enters scan_mgr() loop. Note that both signaling
		 * of ctx->wait and wait on ctx->loop are done under
		 * ctx->guard spin lock. This guarantees that current thread
		 * cannot lose wakeup. */
		kcond_wait(&ctx->loop, &ctx->guard, 0);
		spin_unlock(&ctx->guard);
	}
}

reiser4_internal void
ktxnmgrd_kick(txn_mgr * mgr)
{
	assert("nikita-3234", mgr != NULL);
	assert("nikita-3235", mgr->daemon != NULL);
	kcond_signal(&mgr->daemon->wait);
}

reiser4_internal int
is_current_ktxnmgrd(void)
{
	return (get_current_super_private()->tmgr.daemon->tsk == current);
}

/* scan one transaction manager for old atoms; should be called with ktxnmgrd
 * spinlock, releases this spin lock at exit */
static int
scan_mgr(txn_mgr * mgr)
{
	int              ret;
	reiser4_context  ctx;
	reiser4_tree    *tree;

	assert("nikita-2454", mgr != NULL);

	/* NOTE-NIKITA this only works for atoms embedded into super blocks. */
	tree = &container_of(mgr, reiser4_super_info_data, tmgr)->tree;
	assert("nikita-2455", tree != NULL);
	assert("nikita-2456", tree->super != NULL);

	init_context(&ctx, tree->super);

	ret = commit_some_atoms(mgr);

	reiser4_exit_context(&ctx);
	return ret;
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
