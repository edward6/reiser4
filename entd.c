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

#include <linux/sched.h>	/* struct task_struct */
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/time.h>         /* INITIAL_JIFFIES */
#include <linux/backing-dev.h>  /* bdi_write_congested */

/*
 * set this to 0 if you don't want to use wait-for-flush in ->writepage(). This
 * is useful for debugging emergency flush, for example.
 */
#define USE_ENTD (0)

#define DEF_PRIORITY 12

static void entd_flush(struct super_block *super);

#define set_comm(state)					\
	snprintf(current->comm, sizeof(current->comm),	\
	         "ent:%s%s", super->s_id, (state))

static inline entd_context *
get_entd_context(struct super_block *super)
{
	return &get_super_private(super)->entd;
}

static int
entd(void *arg)
{
	struct super_block *super;
	struct task_struct *me;
	entd_context       *ctx;

	super = arg;
	/* standard kernel thread prologue */
	me = current;
	/* reparent_to_init() is done by daemonize() */
	daemonize("ent:%s", super->s_id);

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
		ctx->kicks_pending = 0;
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
	/* confine ctx->timeout within [1 .. HZ/20] */
	if (ctx->timeout > HZ / 20)
		ctx->timeout = HZ / 20;
	if (ctx->timeout < 1)
		ctx->timeout = 1;
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
#endif
	spin_unlock(&ctx->guard);
	get_current_context()->flush_started = INITIAL_JIFFIES;
}

static int get_flushers(struct super_block *super, unsigned long *flush_start)
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

static void kick_entd(struct super_block *super)
{
	entd_context    * ctx;
	assert("nikita-3109", super != NULL);

	ctx = get_entd_context(super);

	spin_lock(&ctx->guard);
	if (ctx->kicks_pending == 0)
		kcond_signal(&ctx->wait);
	++ ctx->kicks_pending;
	spin_unlock(&ctx->guard);
}

/*
 * return true if we are done with @page (it is clean), or something really
 * wrong happened and wait_for_flush() is looping.
 *
 * Used in wait_for_flush(), which see for more details.
 */
static int
is_writepage_done(jnode *node)
{
	reiser4_stat_inc(wff.iteration);
	/*
	 * if flush managed to process this node we are done.
	 */
	if (jnode_check_flushprepped(node)) {
		reiser4_stat_inc(wff.cleaned);
		return 1;
	}
	/*
	 * jnode removed from the tree (truncate or balancing)
	 */
	if (JF_ISSET(node, JNODE_HEARD_BANSHEE)) {
		reiser4_stat_inc(wff.removed);
		return 1;
	}

	return 0;
}

/*
 * return true if calling thread is either ent thread or the only flusher for
 * this file system. Used in wait_for_flush(), which see for more details.
 */
static int dont_wait_for_flush(struct super_block *super)
{
	reiser4_context * cur;
	unsigned long flush_started;

	if (!USE_ENTD)
		return 1;

	cur = get_current_context();

	if (cur->entd) {
		reiser4_stat_inc(wff.skipped_ent);
		return 1;
	}

	if (get_flushers(super, &flush_started) == 1 && 
	    cur->flush_started != INITIAL_JIFFIES) {
		reiser4_stat_inc(wff.skipped_last);
		return 1;
	}
	return 0;
}

#define WFF_MAX_ITERATIONS (3)

/*
 * This function uses some heuristic algorithm that results in @page being
 * cleaned by normal flushing (flush.c) in most cases. Reason for this is
 * whenever possible to avoid emergency flush (emergency_flush.c) that doesn't
 * perform disk layout optimization.
 *
 * Algorithm:
 *
 *  1. there is dedicated per-super block "ent" (from Tolkien's LOTR) thread
 *  used to start flushing if no other flushers are active. It is called an
 *  ent because it takes care of trees, it requires awakening, and once
 *  awakened it might do a lot.
 *
 *  2. our goal is to wait for some reasonable amount of time ("timeout") in
 *  hope that ongoing concurrent flush would process and clean @page.
 *
 *  3. specifically we wait until following happens:
 *
 *       there is flush (possibly being done by the ent) that started more
 *       than timeout ago, 
 *  
 *                              and
 *
 *       device queue is not congested.
 *
 *
 *  Intuitively this means that flush stalled, probably waiting for free
 *  memory.
 *
 *  Tricky part here is selection of timeout value. Probably it should be
 *  dynamically adjusting based on CPU load and average time it takes flush to
 *  start submitting nodes.
 *
 * Return:
 *
 *   > 0 we are done with page (it has been cleaned, or we decided we don't
 *       want to deal with it this time)
 *   < 0 some error occurred
 *     0 no luck, proceed with emergency flush
 *
 */
int
wait_for_flush(struct page *page, jnode *node, struct writeback_control *wbc)
{
	struct backing_dev_info *bdi;
	int                      flushers;
	unsigned long            flush_started;
	unsigned long            timeout;
	int                      result;
	int                      iterations;
	struct super_block      *super;

	bdi     = page->mapping->backing_dev_info;
	super   = page->mapping->host->i_sb;
	timeout = get_entd_context(super)->timeout;

	reiser4_stat_inc(wff.asked);

	result     = 0;
	iterations = 0;

	while (result == 0) {
		flushers = get_flushers(super, &flush_started);
		/*
		 * if there is no flushing going on---launch ent thread.
		 */
		if (flushers == 0) {
			reiser4_stat_inc(wff.kicked);
			kick_entd(super);
		}

		/*
		 * if memory pressure is low, do nothing
		 */
#if 1
		if (page_zone(page)->pressure < (DEF_PRIORITY - 3) << 10) {
			reiser4_stat_inc(wff.low_priority);
			result = 1;
			break;
		}
#else
		if (wbc->priority > DEF_PRIORITY - 3) {
			reiser4_stat_inc(wff.low_priority);
			result = 1;
			break;
		}
#endif
		/*
		 * we don't want to apply usual wait-for-flush logic in
		 * ->writepage() if current thread is ent or, more generally,
		 * if it is the only active flusher in this file
		 * system. Otherwise we get some thread waiting for flush to
		 * clean some pages and flush is waiting for nothing. This
		 * brings VM scanning to almost complete halt.
		 */
		if (dont_wait_for_flush(super))
			break;

		/*
		 * wait until at least one flushing thread is running for at
		 * least @timeout
		 */
		if (flushers != 0 &&
		    time_before(flush_started + timeout, jiffies))
			break;

		schedule_timeout(timeout);
		reiser4_stat_inc(wff.wait_flush);

		/*
		 * if flush managed to clean this page we are done.
		 */
		result = is_writepage_done(node);

		/*
		 * check for some weird condition to avoid stalling memory
		 * scan.
		 */
		if (++ iterations > WFF_MAX_ITERATIONS) {
			reiser4_stat_inc(wff.toolong);
			break;
		}
	}

	if (result == 0)
		result = is_writepage_done(node);

	/*
	 * at this point we are either done (result != 0), or there is flush
	 * going on for at least @timeout. If device is congested, we
	 * conjecture that flush is actively progressing (as opposed to being
	 * stalled).
	 */
	if (result == 0 && bdi_write_congested(bdi)) {
		reiser4_stat_inc(wff.skipped_congested);
		result = 1;
	}

	/*
	 * at this point either the scanning priority is low and we choose to
	 * not wait, or we flushed something, or there was a flushing thread
	 * going on for at least @timeout but nothing was sent down to the
	 * disk. Probably flush stalls waiting for memory. This shouldn't
	 * happen often for normal file system loads, because balance dirty
	 * pages ensures there are enough clean pages around.
	 */
	return result;
}

static void entd_flush(struct super_block *super)
{
	long            nr_submitted;
	int             result;
	reiser4_context txn;

	init_context(&txn, super);

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
