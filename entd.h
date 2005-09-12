/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Ent daemon. */

#ifndef __ENTD_H__
#define __ENTD_H__

#include "kcond.h"
#include "context.h"

#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/sched.h>	/* for struct task_struct */

/* write-back request. */
struct wbq {
	struct list_head link; /* list head of this list is in entd context */
	struct writeback_control *wbc;
	struct page *page;
	struct semaphore sem;
	int nr_entd_iters;
	unsigned int phantom:1;
};

/* ent-thread context. This is used to synchronize starting/stopping ent
 * threads. */
typedef struct entd_context {
	/*
	 * completion that is signaled by ent thread just before it
	 * terminates.
	 */
	struct completion start_finish_completion;
	/*
	 * condition variable that ent thread waits on for more work. It's
	 * signaled by write_page_by_ent().
	 */
	kcond_t wait;
	/* spinlock protecting other fields */
	spinlock_t guard;
	/* ent thread */
	struct task_struct *tsk;
	/* set to indicate that ent thread should leave. */
	int done;
	/* counter of active flushers */
	int flushers;
#if REISER4_DEBUG
	/* list of all active flushers */
	struct list_head flushers_list;
#endif
	int nr_all_requests;
	int nr_synchronous_requests;
	struct list_head wbq_list; /* struct wbq are elements of this list */
} entd_context;

extern void init_entd(struct super_block *);
extern void done_entd(struct super_block *);

extern void enter_flush(struct super_block *);
extern void leave_flush(struct super_block *);

extern int write_page_by_ent(struct page *, struct writeback_control *);
extern int wbq_available(void);
extern void ent_writes_page(struct super_block *, struct page *);

extern struct wbq *get_wbq(struct super_block *);
extern void put_wbq(struct super_block *, struct wbq *);
extern jnode *get_jnode_by_wbq(struct super_block *, struct wbq *);
/* __ENTD_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
