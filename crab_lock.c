/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "crab_lock.h"
#include "znode.h"

void
crab_init(crab_lock_t *clock)
{
	xmemset(clock, 0, sizeof *clock);
}

int
crab_prepare(crab_lock_t *clock, znode *node)
{
	int result;

	spin_lock_znode(node);
	RLOCK_ZLOCK(&node->lock);
	if (!znode_is_wlocked(node) && ZJNODE(node)->atom == NULL) {
		clock->node = zref(node);
		clock->version = node->version;
		clock->locked = 0;
		result = 0;
	} else
		result = -E_REPEAT;
	RUNLOCK_ZLOCK(&node->lock);
	spin_unlock_znode(node);
	return result;
}

int
crab_lock(crab_lock_t *plock, crab_lock_t *clock, znode *node)
{
	int result;
	znode *parent;

	parent = plock->node;
	spin_lock_znode(parent);
	RLOCK_ZLOCK(&parent->lock);
	if (parent->version == plock->version && ZJNODE(parent)->atom == NULL) {
		spin_lock_znode(node);
		RLOCK_ZLOCK(&node->lock);
		if (!znode_is_wlocked(node) && ZJNODE(node)->atom == NULL) {
			clock->node = zref(node);
			clock->version = node->version;
			clock->locked = 1;
			RLOCK_DLOCK(node);
			result = 0;
		} else
			result = -E_REPEAT;
		RUNLOCK_ZLOCK(&node->lock);
		spin_unlock_znode(node);
	} else
		result = -E_REPEAT;
	RUNLOCK_ZLOCK(&parent->lock);
	spin_unlock_znode(parent);
	return result;
}

void
crab_unlock(crab_lock_t *clock)
{
	if (clock->locked) {
		RUNLOCK_DLOCK(clock->node);
		clock->locked = 0;
	}
}

void
crab_done(crab_lock_t *clock)
{
	if (clock->node != NULL) {
		zput(clock->node);
		clock->node = NULL;
	}
}

void
crab_move(crab_lock_t *to, crab_lock_t *from)
{
	*to = *from;
	crab_init(from);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
