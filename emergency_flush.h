/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Emergency flush */

#ifndef __EMERGENCY_FLUSH_H__
#define __EMERGENCY_FLUSH_H__

#define REISER4_USE_EFLUSH (1)

#define I_GHOST (128)

#if REISER4_USE_EFLUSH

struct eflush_node;
typedef struct eflush_node eflush_node_t;
TS_HASH_DECLARE(ef, eflush_node_t);

int eflush_init(void);
int eflush_done(void);

extern int  eflush_init_at(struct super_block *super);
extern void eflush_done_at(struct super_block *super);

extern reiser4_block_nr *eflush_get(const jnode *node);
extern void eflush_del(jnode *node, int page_locked);

int emergency_flush(struct page *page);
int emergency_unflush(jnode *node);

extern spinlock_t eflushed_guard;

#else

typedef struct {
} ef_hash_table;

#define eflush_init() (0)
#define eflush_done() noop

#define eflush_init_at(super) (0)
#define eflush_done_at(super) noop

#define eflush_get(node)           (NULL)
#define eflush_del(node, pl)       noop
#define emergency_flush(page) (0)
#define emergency_unflush(node)    (0)

#endif

/* __EMERGENCY_FLUSH_H__ */
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
