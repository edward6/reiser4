/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Emergency flush
 */

#ifndef __EMERGENCY_FLUSH_H__
#define __EMERGENCY_FLUSH_H__

struct eflush_node;
typedef struct eflush_node eflush_node_t;
TS_HASH_DECLARE(ef, eflush_node_t);

extern int  eflush_init_at(struct super_block *super);
extern void eflush_done_at(struct super_block *super);

extern int eflush_add(jnode *node, reiser4_block_nr *blocknr);
extern reiser4_block_nr *eflush_get(const jnode *node);
extern void eflush_del(jnode *node);

/* __EMERGENCY_FLUSH_H__ */
#endif

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
