/* Copyright 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Ent daemon. */

#include "debug.h"
#include "txnmgr.h"
#include "tree.h"
#include "entd.h"
#include "super.h"
#include "context.h"
#include "reiser4.h"
#include "vfs_ops.h"
#include "page_cache.h"
#include "inode.h"

#include <linux/sched.h>	/* struct task_struct */
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/time.h>		/* INITIAL_JIFFIES */
#include <linux/backing-dev.h>	/* bdi_write_congested */
#include <linux/wait.h>

TYPE_SAFE_LIST_DEFINE(wbq, struct wbq, link);

#define DEF_PRIORITY 12
#define MAX_ENTD_ITERS 10
#define ENTD_ASYNC_REQUESTS_LIMIT 0

static void entd_flush(struct super_block *);
static int entd(void *arg);

/*
 * set ->comm field of end thread to make its state visible to the user level
 */
#define entd_set_comm(state)					\
	snprintf(current->comm, sizeof(current->comm),	\
	         "ent:%s%s", super->s_id, (state))

/* get ent context for the @super */
static inline entd_context *get_entd_context(struct super_block *super)
{
	return &get_super_private(super)->entd;
}

/**
 * init_entd - initialize entd context and start kernel daemon
 * @super: super block to start ent thread for
 *
 * Creates entd contexts, starts kernel thread and waits until it
 * initializes.
 */
void init_entd(struct super_block *super)
{
	entd_context *ctx;

	assert("nikita-3104", super != NULL);

	ctx = get_entd_context(super);

	memset(ctx, 0, sizeof *ctx);
	spin_lock_init(&ctx->guard);
	init_waitqueue_head(&ctx->wait);

	/*
	 * prepare synchronization object to synchronize with ent thread
	 * initialization
	 */
	init_completion(&ctx->start_finish_completion);

	/* start entd */
	kernel_thread(entd, super, CLONE_VM | CLONE_FS | CLONE_FILES);

 	/* wait for entd initialization */
	wait_for_completion(&ctx->start_finish_completion);

#if REISER4_DEBUG
	flushers_list_init(&ctx->flushers_list);
#endif
	wbq_list_init(&ctx->wbq_list);
}

static void __put_wbq(entd_context * ent, struct wbq *rq)
{
	rq->wbc->nr_to_write--;
	up(&rq->sem);
}

/* ent should be locked */
static struct wbq *__get_wbq(entd_context * ent)
{
	if (wbq_list_empty(&ent->wbq_list)) {
		return NULL;
	}
	ent->nr_synchronous_requests --;
	ent->nr_all_requests --;
	return wbq_list_pop_front(&ent->wbq_list);
}

struct wbq * get_wbq(struct super_block * super)
{
	struct wbq *result;
	entd_context * ent = get_entd_context(super);

	spin_lock(&ent->guard);
	result = __get_wbq(ent);
	spin_unlock(&ent->guard);

	return result;
}

void put_wbq(struct super_block *super, struct wbq * rq)
{
	entd_context * ent = get_entd_context(super);

	spin_lock(&ent->guard);
	__put_wbq(ent, rq);
	spin_unlock(&ent->guard);
}

static void wakeup_all_wbq(entd_context * ent)
{
	struct wbq *rq;

	spin_lock(&ent->guard);
	while ((rq = __get_wbq(ent)) != NULL)
		__put_wbq(ent, rq);
	spin_unlock(&ent->guard);
}

/* ent thread function */
static int entd(void *arg)
{
	struct super_block *super;
	struct task_struct *me;
	entd_context *ent;

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
	me->journal_info = NULL;

	ent = get_entd_context(super);

	spin_lock(&ent->guard);
	ent->tsk = me;
	spin_unlock(&ent->guard);

	/* initialization is done */
	complete(&ent->start_finish_completion);

	while (!ent->done) {
		if (freezing(me))
			refrigerator();

		spin_lock(&ent->guard);

		while (ent->nr_all_requests != 0) {
			assert("zam-1043",
			       ent->nr_all_requests >=
			       ent->nr_synchronous_requests);
			if (ent->nr_synchronous_requests != 0) {
				struct wbq *rq = wbq_list_front(&ent->wbq_list);

				if (++rq->nr_entd_iters > MAX_ENTD_ITERS) {
					rq = __get_wbq(ent);
					__put_wbq(ent, rq);
					continue;
				}
			} else {
				/* endless loop avoidance. */
				ent->nr_all_requests--;
			}

			spin_unlock(&ent->guard);
			entd_set_comm("!");
			entd_flush(super);
		}

		entd_set_comm(".");

		{
			DEFINE_WAIT(__wait);

			for (;;) {
				int dontsleep;

				prepare_to_wait(&ent->wait, &__wait, TASK_UNINTERRUPTIBLE);
				spin_lock(&ent->guard);
				dontsleep = ent->done || ent->nr_all_requests != 0;
				spin_unlock(&ent->guard);
				if (dontsleep)
					break;
				schedule();
			}
			finish_wait(&ent->wait, &__wait);
		}
	}
	wakeup_all_wbq(ent);
	complete_and_exit(&ent->start_finish_completion, 0);
	/* not reached. */
	return 0;
}

/**
 * done_entd - stop entd kernel thread
 * @super: super block to stop ent thread for
 *
 * It is called on umount. Sends stop signal to entd and wait until it handles
 * it.
 */
void done_entd(struct super_block *super)
{
	entd_context *ent;

	assert("nikita-3103", super != NULL);

	ent = get_entd_context(super);

	/*
	 * prepare synchronization object to synchronize with entd
	 * completion
	 */
	init_completion(&ent->start_finish_completion);

	spin_lock(&ent->guard);
	ent->done = 1;
	spin_unlock(&ent->guard);
	wake_up(&ent->wait);

	/* wait until entd finishes */
	wait_for_completion(&ent->start_finish_completion);
}

/* called at the beginning of jnode_flush to register flusher thread with ent
 * daemon */
void enter_flush(struct super_block *super)
{
	entd_context *ent;

	assert("zam-1029", super != NULL);
	ent = get_entd_context(super);

	assert("zam-1030", ent != NULL);

	spin_lock(&ent->guard);
	ent->flushers++;
#if REISER4_DEBUG
	flushers_list_push_front(&ent->flushers_list, get_current_context());
#endif
	spin_unlock(&ent->guard);
}

/* called at the end of jnode_flush */
void leave_flush(struct super_block *super)
{
	entd_context *ent;
	int wake_up_ent;

	assert("zam-1027", super != NULL);
	ent = get_entd_context(super);

	assert("zam-1028", ent != NULL);

	spin_lock(&ent->guard);
	ent->flushers--;
	wake_up_ent = (ent->flushers == 0 && ent->nr_synchronous_requests != 0);
#if REISER4_DEBUG
	flushers_list_remove_clean(get_current_context());
#endif
	spin_unlock(&ent->guard);
	if (wake_up_ent)
		wake_up(&ent->wait);
}

#define ENTD_CAPTURE_APAGE_BURST (32l)

/* Ask as_ops->writepages() to process given page */
static jnode * capture_given_page(struct page *page)
{
	struct address_space * mapping;
	struct writeback_control wbc = {
		.bdi = NULL,
		.sync_mode = WB_SYNC_NONE,
		.older_than_this = NULL,
		.nonblocking = 0,
		.start = page->index << PAGE_CACHE_SHIFT,
		.end = page->index << PAGE_CACHE_SHIFT,
		.nr_to_write = 1,
	};
	jnode * node;

	mapping = page->mapping;
	if (mapping == NULL)
		return NULL;
	if (mapping->a_ops && mapping->a_ops->writepages)
		mapping->a_ops->writepages(mapping, &wbc);
	lock_page(page);
	node = jprivate(page);
	if (node != NULL)
		jref(node);
	unlock_page(page);
	return node;
}

jnode * get_jnode_by_wbq(struct super_block *super, struct wbq *rq)
{
	struct page * page = NULL;
	jnode * node = NULL;
	int result;

	if (rq == NULL)
		return NULL;

	assert("zam-1052", rq->page != NULL);

	page = rq->page;
	node = capture_given_page(page);
	if (node == NULL)
		return NULL;
	spin_lock_jnode(node);
	result = try_capture(node, ZNODE_WRITE_LOCK, TXN_CAPTURE_NONBLOCKING, 0);
	spin_unlock_jnode(node);
	if (result) {
		jput(node);
		return NULL;
	}
	return node;
}

static void entd_flush(struct super_block *super)
{
	reiser4_context ctx;
	struct writeback_control wbc = {
		.bdi = NULL,
		.sync_mode = WB_SYNC_NONE,
		.older_than_this = NULL,
		.nr_to_write = ENTD_CAPTURE_APAGE_BURST,
		.nonblocking = 0,
	};

	init_stack_context(&ctx, super);
	ctx.entd = 1;

	generic_sync_sb_inodes(super, &wbc);

	wbc.nr_to_write = ENTD_CAPTURE_APAGE_BURST;
	writeout(super, &wbc);
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
}

int write_page_by_ent(struct page *page, struct writeback_control *wbc)
{
	struct super_block *sb;
	entd_context *ent;
	struct wbq rq;
	int phantom;
	int wake_up_entd;

	sb = page->mapping->host->i_sb;
	ent = get_entd_context(sb);
	if (ent == NULL || ent->done)
		/* entd is not running. */
		return 0;

	phantom = jprivate(page) == NULL || !jnode_check_dirty(jprivate(page));
#if 1
	BUG_ON(page->mapping == NULL);
	/* re-dirty page */
	if (!TestSetPageDirty(page)) {
		if (mapping_cap_account_dirty(page->mapping))
			inc_page_state(nr_dirty);
	}
	/*reiser4_set_page_dirty(page);*/
	/*set_page_dirty_internal(page, phantom);*/
	/* unlock it to avoid deadlocks with the thread which will do actual i/o  */
	unlock_page(page);
#endif

	/* init wbq */
	wbq_list_clean(&rq);
	rq.nr_entd_iters = 0;
	rq.page = page;
	rq.wbc = wbc;
	rq.phantom = phantom;

	spin_lock(&ent->guard);
	wake_up_entd = (ent->flushers == 0);
	ent->nr_all_requests++;
	if (ent->nr_all_requests <=
	    ent->nr_synchronous_requests + ENTD_ASYNC_REQUESTS_LIMIT) {
		BUG_ON(1);
		spin_unlock(&ent->guard);
		if (wake_up_entd)
			wake_up(&ent->wait);
		lock_page(page);
		return 0;
	}

	sema_init(&rq.sem, 0);
	wbq_list_push_back(&ent->wbq_list, &rq);
	ent->nr_synchronous_requests++;
	spin_unlock(&ent->guard);
	if (wake_up_entd)
		wake_up(&ent->wait);
	down(&rq.sem);

	/* don't release rq until wakeup_wbq stops using it. */
	spin_lock(&ent->guard);
	spin_unlock(&ent->guard);
	if (!PageDirty(page)) {
		/* Eventually ENTD has written the page to disk. */
		return 1;
	}
	lock_page(page);
	return WRITEPAGE_ACTIVATE;
}

void ent_writes_page(struct super_block *sb, struct page *page)
{
	entd_context *ent = get_entd_context(sb);
	struct wbq *rq;

	assert("zam-1041", ent != NULL);

	if (PageActive(page) || ent->nr_all_requests == 0)
		return;

	SetPageReclaim(page);

	spin_lock(&ent->guard);
	if (ent->nr_all_requests > 0) {
		rq = __get_wbq(ent);
		if (rq != NULL)
			__put_wbq(ent, rq);
	}
	spin_unlock(&ent->guard);
}

int wbq_available(void)
{
	struct super_block *sb = reiser4_get_current_sb();
	entd_context *ent = get_entd_context(sb);
	return ent->nr_all_requests;
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
