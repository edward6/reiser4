/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#if !defined(__FS_REISER4_CRAB_LOCK_H__)
#define __FS_REISER4_CRAB_LOCK_H__

#include "lock.h"

typedef struct {
	znode *node;
	int    locked;
	__u64  version;
} crab_lock_t;

void crab_init    (crab_lock_t *clock);
int  crab_prepare (crab_lock_t *clock, znode *node);
int  crab_lock    (crab_lock_t *parent, crab_lock_t *child, znode *node);
void crab_unlock  (crab_lock_t *clock);
void crab_done    (crab_lock_t *clock);

void crab_move    (crab_lock_t *to, crab_lock_t *from);

/* __FS_REISER4_CRAB_LOCK_H__ */
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
