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
#include "type_safe_list.h"

TYPE_SAFE_LIST_DECLARE(wbq);

/* write-back request. */
struct wbq {
	struct writeback_control * wbc;
	struct page * page;
	struct semaphore sem;
	wbq_list_link link;
}; 

typedef struct entd_context {
	kcond_t             startup;
	struct completion   finish;
	kcond_t             wait;
	spinlock_t          guard;
	struct task_struct *tsk;
	int                 done;
	int                 rerun;
	unsigned long       last_flush;
	int                 flushers;
	unsigned long       timeout;
	kcond_t             flush_wait;
#if REISER4_DEBUG
	flushers_list_head  flushers_list;
#endif
	int                 wbq_nr;
	wbq_list_head       wbq_list;
} entd_context;

extern void init_entd_context(struct super_block *super);
extern void done_entd_context(struct super_block *super);

extern void enter_flush(struct super_block *super);
extern void leave_flush(struct super_block *super);
extern void flush_started_io(void);

extern void write_page_by_ent(struct page *, struct writeback_control *);
extern int  wbq_available (void);
extern void ent_writes_page (struct super_block *, struct page *);
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
