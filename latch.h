/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __LATCH_H__
#define __LATCH_H__

#include "kcond.h"

typedef struct rw_latch {
	spinlock_t guard;
	int        access;
	kcond_t    cond;
} rw_latch_t;

extern void rw_latch_init(rw_latch_t * latch);
extern void rw_latch_done(rw_latch_t * latch);
extern void rw_latch_down_read(rw_latch_t * latch);
extern void rw_latch_down_write(rw_latch_t * latch);
extern void rw_latch_up_read(rw_latch_t * latch);
extern void rw_latch_up_write(rw_latch_t * latch);
extern void rw_latch_downgrade(rw_latch_t * latch);

/* __LATCH_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
