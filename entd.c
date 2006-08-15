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
#include <linux/kthread.h>

#define DEF_PRIORITY 12
#define MAX_ENTD_ITERS 10

static void entd_flush(struct super_block *, struct wbq *);
static int entd(void *arg);

/*
 * set ->comm field of end thread to make its state visible to the user level
 */
#define entd_set_comm(state)					\
	snprintf(current->comm, sizeof(current->comm),	\
	         "ent:%s%s", super->s_id, (state))

/**
 * init_entd - initialize entd context and start kernel daemon
 * @super: super block to start ent thread for
 *
 * Creates entd contexts, starts kernel thread and waits until it
 * initializes.
 */
int init_entd(struct super_block *super)
{
	entd_context *ctx;

	assert("nikita-3104", super != NULL);

	ctx = get_entd_context(super);

	memset(ctx, 0, sizeof *ctx);
	spin_lock_init(&ctx->guard);
	init_waitqueue_head(&ctx->wait);
#if REISER4_DEBUG
	INIT_LIST_HEAD(&ctx->flushers_list);
#endif
	/* lists of writepage requests */
	INIT_LIST_HEAD(&ctx->todo_list);
	INIT_LIST_HEAD(&ctx->done_list);
	/* start entd */
	ctx->tsk = kthread_run(entd, super, "ent:%s", super->s_id);
	if (IS_ERR(ctx->tsk))
		return PTR_ERR(ctx->tsk);
	return 0;
}

static void __put_wbq(entd_context *ent, struct wbq *rq)
{
	up(&rq->sem);
}

/* ent should be locked */
static struct wbq *__get_wbq(entd_context * ent)
{
	struct wbq *wbq;

	if (list_empty_careful(&ent->todo_list))
		return NULL;

	ent->nr_todo_reqs --;
	wbq = list_entry(ent->todo_list.next, struct wbq, link);
	list_del_init(&wbq->link);
	return wbq;
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
	entd_context *ent;
	int done = 0;

	super = arg;
	/* do_fork() just copies task_struct into the new
	   thread. ->fs_context shouldn't be copied of course. This shouldn't
	   be a problem for the rest of the code though.
	 */
	current->journal_info = NULL;

	ent = get_entd_context(super);

	while (!done) {
		try_to_freeze();

		spin_lock(&ent->guard);
		while (ent->nr_todo_reqs != 0) {
			struct wbq *rq, *next;

			assert("", list_empty_careful(&ent->done_list));

			/* take request from the queue head */
			rq = __get_wbq(ent);
			assert("", rq != NULL);
			ent->cur_request = rq;
			spin_unlock(&ent->guard);

			entd_set_comm("!");
			entd_flush(super, rq);

			iput(rq->mapping->host);
			up(&(rq->sem));

			/*
			 * wakeup all requestors and iput their inodes
			 */
			spin_lock(&ent->guard);
			list_for_each_entry_safe(rq, next, &ent->done_list, link) {
				list_del_init(&(rq->link));
				ent->nr_done_reqs --;
				spin_unlock(&ent->guard);

				assert("", rq->written == 1);
				iput(rq->mapping->host);
				up(&(rq->sem));
				spin_lock(&ent->guard);
			}
		}
		spin_unlock(&ent->guard);

		entd_set_comm(".");

		{
			DEFINE_WAIT(__wait);

			do {
				prepare_to_wait(&ent->wait, &__wait, TASK_INTERRUPTIBLE);
				if (kthread_should_stop()) {
					done = 1;
					break;
				}
				if (ent->nr_todo_reqs != 0)
					break;
				schedule();
			} while (0);
			finish_wait(&ent->wait, &__wait);
		}
	}
	spin_lock(&ent->guard);
	BUG_ON(ent->nr_todo_reqs != 0);
	spin_unlock(&ent->guard);
	wakeup_all_wbq(ent);
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
	assert("zam-1055", ent->tsk != NULL);
	kthread_stop(ent->tsk);
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
	list_add(&get_current_context()->flushers_link, &ent->flushers_list);
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
	wake_up_ent = (ent->flushers == 0 && ent->nr_todo_reqs != 0);
#if REISER4_DEBUG
	list_del_init(&get_current_context()->flushers_link);
#endif
	spin_unlock(&ent->guard);
	if (wake_up_ent)
		wake_up(&ent->wait);
}

#define ENTD_CAPTURE_APAGE_BURST SWAP_CLUSTER_MAX

static void entd_flush(struct super_block *super, struct wbq *rq)
{
	reiser4_context ctx;
	int tmp;

	init_stack_context(&ctx, super);
	ctx.entd = 1;
	ctx.gfp_mask = GFP_NOFS;

	rq->wbc->range_start = page_offset(rq->page);
	rq->wbc->range_end = rq->wbc->range_start +
		(ENTD_CAPTURE_APAGE_BURST << PAGE_CACHE_SHIFT);
	tmp = rq->wbc->nr_to_write;
	rq->mapping->a_ops->writepages(rq->mapping, rq->wbc);

	if (rq->wbc->nr_to_write > 0) {
		rq->wbc->range_start = 0;
		rq->wbc->range_end = LLONG_MAX;
		generic_sync_sb_inodes(super, rq->wbc);
	}
	rq->wbc->nr_to_write = ENTD_CAPTURE_APAGE_BURST;
	writeout(super, rq->wbc);

	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
}

/**
 * write_page_by_ent - ask entd thread to flush this page as part of slum
 * @page: page to be written
 * @wbc: writeback control passed to reiser4_writepage
 *
 * Creates a request, puts it on entd list of requests, wakeups entd if
 * necessary, waits until entd completes with the request.
 */
int write_page_by_ent(struct page *page, struct writeback_control *wbc)
{
	struct super_block *sb;
	struct inode *inode;
	entd_context *ent;
	struct wbq rq;

	assert("", PageLocked(page));
	assert("", page->mapping != NULL);

	sb = page->mapping->host->i_sb;
	ent = get_entd_context(sb);
	assert("", ent && ent->done == 0);

	/*
	 * we are going to unlock page and ask ent thread to write the
	 * page. Re-dirty page before unlocking so that if ent thread fails to
	 * write it - it will remain dirty
	 */
	set_page_dirty_internal(page);

	/*
	 * pin inode in memory, unlock page, entd_flush will iput. We can not
	 * iput here becasue we can not allow delete_inode to be called here
	 */
	inode = igrab(page->mapping->host);
	unlock_page(page);
	if (inode == NULL)
		/* inode is getting freed */
		return 0;

	/* init wbq */
	INIT_LIST_HEAD(&rq.link);
	rq.magic = WBQ_MAGIC;
	rq.wbc = wbc;
	rq.page = page;
	rq.mapping = inode->i_mapping;
	rq.node = NULL;
	rq.written = 0;
	sema_init(&rq.sem, 0);

	/* add request to entd's list of writepage requests */
	spin_lock(&ent->guard);
	ent->nr_todo_reqs++;
	list_add_tail(&rq.link, &ent->todo_list);
	if (ent->nr_todo_reqs == 1)
		wake_up(&ent->wait);

	spin_unlock(&ent->guard);

	/* wait until entd finishes */
	down(&rq.sem);

	/*
	 * spin until entd thread which did up(&rq.sem) does not need rq
	 * anymore
	 */
	spin_lock(&ent->guard);
	spin_unlock(&ent->guard);

	if (rq.written)
		/* Eventually ENTD has written the page to disk. */
		return 0;
	return 0;
}

int wbq_available(void)
{
	struct super_block *sb = reiser4_get_current_sb();
	entd_context *ent = get_entd_context(sb);
	return ent->nr_todo_reqs;
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
