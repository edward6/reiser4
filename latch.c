/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "latch.h"

void 
rw_latch_init(rw_latch_t * latch)
{
	spin_lock_init(&latch->guard);
	latch->access = 0;
	kcond_init(&latch->cond);
}

void 
rw_latch_done(rw_latch_t * latch)
{
	assert("nikita-3062", latch->access == 0);
	kcond_destroy(&latch->cond);
}

void 
rw_latch_down_read(rw_latch_t * latch)
{
	spin_lock(&latch->guard);
	while (latch->access < 0)
		kcond_wait(&latch->cond, &latch->guard, 0);
	latch->access ++;
	spin_unlock(&latch->guard);
}

void
rw_latch_down_write(rw_latch_t * latch)
{
	spin_lock(&latch->guard);
	while (latch->access != 0)
		kcond_wait(&latch->cond, &latch->guard, 0);
	latch->access = -1;
	spin_unlock(&latch->guard);
}

void
rw_latch_up_read(rw_latch_t * latch)
{
	spin_lock(&latch->guard);
	assert("nikita-3063", latch->access > 0);
	latch->access --;
	if (latch->access == 0)
		kcond_broadcast(&latch->cond);
	spin_unlock(&latch->guard);
}

void
rw_latch_up_write(rw_latch_t * latch)
{
	spin_lock(&latch->guard);
	assert("nikita-3063", latch->access == -1);
	latch->access = 0;
	kcond_broadcast(&latch->cond);
	spin_unlock(&latch->guard);
}

void
rw_latch_downgrade(rw_latch_t * latch)
{
	spin_lock(&latch->guard);
	assert("nikita-3063", latch->access == -1);
	latch->access = +1;
	kcond_broadcast(&latch->cond);
	spin_unlock(&latch->guard);
}

int
rw_latch_try_read(rw_latch_t * latch)
{
	int result;

	spin_lock(&latch->guard);
	if (latch->access < 0)
		result = -EBUSY;
	else {
		result = 0;
		latch->access ++;
	}
	spin_unlock(&latch->guard);
	return result;
}

int
rw_latch_try_write(rw_latch_t * latch)
{
	int result;

	spin_lock(&latch->guard);
	if (latch->access != 0)
		result = -EBUSY;
	else {
		result = 0;
		latch->access = -1;
	}
	spin_unlock(&latch->guard);
	return result;
}


/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
