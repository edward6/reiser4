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
#include "vfs_ops.h"
#include "page_cache.h"

#include <linux/sched.h>	/* struct task_struct */
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/time.h>         /* INITIAL_JIFFIES */
#include <linux/backing-dev.h>  /* bdi_write_congested */

TYPE_SAFE_LIST_DEFINE(wbq, struct wbq, link);

#define DEF_PRIORITY 12

static void entd_flush(struct super_block *super);
static int entd(void *arg);

#define entd_set_comm(state)					\
	snprintf(current->comm, sizeof(current->comm),	\
	         "ent:%s%s", super->s_id, (state))

static inline entd_context *
get_entd_context(struct super_block *super)
{
	return &get_super_private(super)->entd;
}

reiser4_internal void
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

static int
entd(void *arg)
{
	struct super_block *super;
	struct task_struct *me;
	entd_context       *ctx;

	assert("vs-1655", list_empty(&current_thread_info()->generic.private_pages));

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
		int result = 0;

		if (me->flags & PF_FREEZE)
			refrigerator(PF_FREEZE/*PF_IOTHREAD*/);

		entd_set_comm(".");
		spin_lock(&ctx->guard);

		if (!ctx->rerun) {
			result = kcond_wait(&ctx->wait, &ctx->guard, 1);

			/* we are asked to exit */
			if (ctx->done) {
				spin_unlock(&ctx->guard);
				break;
			}
		} else
			ctx->rerun = 0;

		spin_unlock(&ctx->guard);
		entd_set_comm("!");
		if (result == 0)
			entd_flush(super);
		else if (result != -EINTR)
			/* some other error */
			warning("nikita-3099", "Error: %i", result);
	}

	complete_and_exit(&ctx->finish, 0);
	/* not reached. */
	return 0;
}

reiser4_internal void
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

reiser4_internal void enter_flush (struct super_block * super)
{
	entd_context * ent;

	assert ("zam-1029", super != NULL);
	ent = get_entd_context(super);

	assert ("zam-1030", ent != NULL);

	spin_lock(&ent->guard);
	ent->flushers ++;
#if REISER4_DEBUG
	flushers_list_push_front(&ent->flushers_list, get_current_context());
#endif
	spin_unlock(&ent->guard);
}

reiser4_internal void leave_flush (struct super_block * super)
{
	entd_context * ent;

	assert ("zam-1027", super != NULL);
	ent = get_entd_context(super);

	assert ("zam-1028", ent != NULL);

	spin_lock(&ent->guard);
	ent->flushers --;
	if (ent->flushers == 0 && ent->wbq_nr == 0) {
		ent->rerun = 1;
		kcond_signal(&ent->wait);
	}
#if REISER4_DEBUG
	flushers_list_remove_clean(get_current_context());
#endif
	spin_unlock(&ent->guard);
}

reiser4_internal void flush_started_io (void)
{
}

static void kick_entd(entd_context * ent)
{
	kcond_signal(&ent->wait);
}

static void entd_capture_anonymous_pages(
	struct super_block * super, struct writeback_control * wbc)
{
	spin_lock(&inode_lock);
	capture_reiser4_inodes(super, wbc);
	spin_unlock(&inode_lock);
}

static void entd_flush(struct super_block *super)
{
	long            nr_submitted = 0;
	int             result;
	reiser4_context ctx;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = NULL,
		.nr_to_write	= 32,
		.nonblocking	= 0,
	};

	init_context(&ctx, super);

	ctx.entd = 1;

	entd_capture_anonymous_pages(super, &wbc);
	result = flush_some_atom(&nr_submitted, &wbc, 0);
	if (result != 0)
		warning("nikita-3100", "Flush failed: %i", result);
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
}

static inline void init_wbq (struct wbq * rq)
{
	memset(rq, 0, sizeof(*rq));
	wbq_list_clean(rq);
}

static inline void prepare_wbq (struct wbq * rq)
{
	sema_init(&rq->sem, 0);
}

void write_page_by_ent (struct page * page, struct writeback_control * wbc)
{
	struct super_block * sb;
	entd_context * ent;
	struct wbq rq;

	sb = page->mapping->host->i_sb;
	ent = get_entd_context(sb);

	assert ("zam-1039", ent != NULL);

	/* re-dirty page */
	set_page_dirty_internal(page);
	/* unlock it to avoid deadlocks with the thread which will do actual i/o  */
	unlock_page(page);

	init_wbq(&rq);
	rq.page = page;
	rq.wbc = wbc;

	spin_lock(&ent->guard);
	prepare_wbq(&rq);

	wbq_list_push_front(&ent->wbq_list, &rq);

	if (ent->flushers == 0)
		kick_entd(ent);

	spin_unlock(&ent->guard);

	down(&rq.sem);

	/* wbq dequeued by the ent thread. */
}

/* ent should be locked */
static struct wbq * get_wbq (entd_context * ent)
{
	if (wbq_list_empty(&ent->wbq_list)) {
		spin_unlock(&ent->guard);
		return NULL;
	}
	return wbq_list_front(&ent->wbq_list);
}


void ent_writes_page (struct super_block * sb, struct page * page)
{
	entd_context * ent = get_entd_context(sb);
	struct wbq * rq;

	assert("zam-1041", ent != NULL);

	if (PageActive(page)) 
		return;

	SetPageReclaim(page);

	spin_lock(&ent->guard);

	rq = get_wbq(ent);
	if (rq == NULL)
		return;

	wbq_list_remove(rq);
	rq->wbc->nr_to_write --;
	up(&rq->sem);

	spin_unlock(&ent->guard);
}

int wbq_available (void) {
	struct super_block * sb = reiser4_get_current_sb();
	entd_context * ent = get_entd_context(sb);
	return ent->wbq_nr;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
