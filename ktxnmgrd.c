/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */
/* Transaction manager daemon. */

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

#if (0)
#define ktxnmgrd_trace( args... ) info( "ktxnmgrd: " ##args )
#else
#define ktxnmgrd_trace( args... ) noop
#endif

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
int
ktxnmgrd(void *arg)
{
	struct task_struct *me;
	ktxnmgrd_context *ctx;

	/* standard kernel thread prologue */
	me = current;
	/* reparent_to_init() is done by daemonize() */
	daemonize();
	strcpy(me->comm, __FUNCTION__);

	/* block all signals */
	spin_lock_irq(&me->sig->siglock);
	siginitsetinv(&me->blocked, 0);
	recalc_sigpending();
	spin_unlock_irq(&me->sig->siglock);

	/*
	 * do_fork() just copies task_struct into the new
	 * thread. ->fs_context shouldn't be copied of course. This shouldn't
	 * be a problem for the rest of the code though.
	 */
	me->fs_context = NULL;

	ctx = arg;
	spin_lock(&ctx->guard);
	ctx->tsk = me;
	kcond_broadcast(&ctx->startup);
	ktxnmgrd_trace("started\n");
	while (1) {
		int result;
		txn_mgr *mgr;

		/*
		 * software suspend support. Doesn't work currently
		 * (kcond_timedwait).
		 */
		if (me->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);

		set_comm("wait");
		/*
		 * wait for @ctx -> timeout or explicit wake up.
		 *
		 * kcond_wait() is called with last argument 1 enabling wakeup
		 * by signals so that this thread is not counted in
		 * load-average. This doesn't require any special handling,
		 * because all signals were blocked.
		 */
		result = kcond_timedwait(&ctx->wait, &ctx->guard, ctx->timeout, 1	/* signalable */
		    );
		if ((result != -ETIMEDOUT) && (result != 0)) {
			/*
			 * some other error
			 */
			warning("nikita-2443", "Error: %i", result);
			continue;
		}

		/*
		 * we are asked to exit
		 */
		if (ctx->done)
			break;

		ktxnmgrd_trace("woke up\n");

		set_comm(result ? "timed" : "run");

		/*
		 * wait timed out or ktxnmgrd was woken up by explicit request
		 * to commit something. Scan list of atoms in txnmgr and look
		 * for too old atoms.
		 */
		do {
			ctx->rescan = 0;
			for (mgr = txn_mgrs_list_front(&ctx->queue);
			     !txn_mgrs_list_end(&ctx->queue, mgr); mgr = txn_mgrs_list_next(mgr)) {
				scan_mgr(mgr);
				ktxnmgrd_trace("\tscan mgr\n");
			}
			ktxnmgrd_trace("scan finished\n");
		} while (ctx->rescan);
	}

	spin_unlock(&ctx->guard);

	ktxnmgrd_trace("exiting\n");
	complete_and_exit(&ctx->finish, 0);
	/*
	 * not reached.
	 */
	return 0;
}

void
init_ktxnmgrd_context(ktxnmgrd_context * ctx)
{
	assert("nikita-2442", ctx != NULL);

	xmemset(ctx, 0, sizeof *ctx);
	kcond_init(&ctx->startup);
	init_completion(&ctx->finish);
	kcond_init(&ctx->wait);
	spin_lock_init(&ctx->guard);
	ctx->timeout = REISER4_TXNMGR_TIMEOUT;
	txn_mgrs_list_init(&ctx->queue);
	atomic_set(&ctx->pressure, 0);
}

static const unsigned int ktxnmrgd_flags = CLONE_VM | CLONE_FS | CLONE_FILES;

int
ktxnmgrd_attach(ktxnmgrd_context * ctx, txn_mgr * mgr)
{
	int first_mgr;

	assert("nikita-2448", mgr != NULL);

	spin_lock(&ctx->guard);

	first_mgr = txn_mgrs_list_empty(&ctx->queue);

	/*
	 * attach @mgr to daemon. Not taking spin-locks, because this is early
	 * during @mgr initialization.
	 */
	mgr->daemon = ctx;
	txn_mgrs_list_push_back(&ctx->queue, mgr);

	/*
	 * daemon thread is not yet initialized
	 */
	if (ctx->tsk == NULL) {
		/*
		 * attaching first mgr, start daemon
		 */
		if (first_mgr) {
			ctx->done = 0;

			/*
			 * kernel_thread never fails.
			 */
			kernel_thread(ktxnmgrd, ctx, ktxnmrgd_flags);
		}
		/*
		 * wait until initialization completes
		 */
		kcond_wait(&ctx->startup, &ctx->guard, 0);
	}
	assert("nikita-2452", ctx->tsk != NULL);

	spin_unlock(&ctx->guard);
	return 0;
}

void
ktxnmgrd_detach(txn_mgr * mgr)
{
	ktxnmgrd_context *ctx;

	assert("nikita-2450", mgr != NULL);

	/*
	 * this is supposed to happen when @mgr is quiesced and no locking is
	 * necessary.
	 */
	ctx = mgr->daemon;
	if (ctx == NULL)
		return;

	spin_lock(&ctx->guard);
	txn_mgrs_list_remove(mgr);
	mgr->daemon = NULL;

	/*
	 * removing last mgr, stop daemon
	 */
	if (txn_mgrs_list_empty(&ctx->queue)) {
		ctx->tsk = NULL;
		ctx->done = 1;
		spin_unlock(&ctx->guard);
		kcond_signal(&ctx->wait);

		/*
		 * wait until daemon finishes
		 */
		wait_for_completion(&ctx->finish);
	} else
		spin_unlock(&ctx->guard);
}

/* wake up ktxnmgrd thread */
void
ktxnmgrd_kick(ktxnmgrd_context * ctx, ktxnmgrd_wake reason)
{
	if (ctx != NULL) {
		spin_lock(&ctx->guard);
		if (ctx->tsk != NULL) {
			ctx->duties |= reason;
			kcond_signal(&ctx->wait);
		}
		spin_unlock(&ctx->guard);

		preempt_point();
	}
}

/* Did somebody ask us to flush nodes? */
static int
need_flush(txn_mgr * tmgr)
{
	int ret;

	spin_lock_txnmgr(tmgr);
	ret = (tmgr->flush_control.nr_to_flush != 0);
	spin_unlock_txnmgr(tmgr);

	return ret;
}

/** scan one transaction manager for old atoms */
static int
scan_mgr(txn_mgr * mgr)
{
	int ret;

	reiser4_tree *tree;
	assert("nikita-2454", mgr != NULL);

	/*
	 * FIXME-NIKITA this only works for atoms embedded into super blocks.
	 */
	tree = &container_of(mgr, reiser4_super_info_data, tmgr)->tree;
	assert("nikita-2455", tree != NULL);
	assert("nikita-2456", tree->super != NULL);

	{
		long nr_submitted;

		REISER4_ENTRY(tree->super);

		ret = commit_one_atom(mgr);

		if (!ret && need_flush(mgr)) {
			ret = flush_one_atom(mgr, &nr_submitted, 
					     JNODE_FLUSH_WRITE_BLOCKS);
		}
		REISER4_EXIT(ret);
	}
}

/* prepare a request for flushing for ktxnmgrd */
int
ktxnmgr_writeback(struct super_block *s, struct writeback_control *wbc)
{
	txn_mgr *tmgr = &get_super_private(s)->tmgr;

	spin_lock_txnmgr(tmgr);

	if (tmgr->flush_control.nr_to_flush == 0) {
		tmgr->flush_control.nr_to_flush += wbc->nr_to_write * 2;
		spin_unlock_txnmgr(tmgr);
		ktxnmgrd_kick(tmgr->daemon, 0);
	} else {
		spin_unlock_txnmgr(tmgr);
		preempt_point();
	}

	return 0;
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
