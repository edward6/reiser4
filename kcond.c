/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Kernel condition variables implemenation.
 *
 * This is simplistic (90 LOC mod comments) condition variable
 * implementation. Condition variable is the most natural "synchronization
 * object" in some circumstances.
 *
 * Each CS text-book on multi-threading should discuss condition
 * variables. Also see man/info for:
 *
 *                 pthread_cond_init(3),
 *                 pthread_cond_destroy(3), 
 *                 pthread_cond_signal(3), 
 *                 pthread_cond_broadcast(3),
 *                 pthread_cond_wait(3), 
 *                 pthread_cond_timedwait(3).
 *
 * Or Chapter 4 of
 * http://docs.sun.com:80/ab2/@LegacyTocView?toc=SUNWab_45_3%3A%2Fsafedir%2Fspace3%2Fcoll2%2FSUNWabsdk%2Ftoc%2FMTP%3AMTP%3Aclose;bt=&Ab2Lang=C&Ab2Enc=iso-8859-1
 *
 * See comments in kcond_wait().
 *
 */

#include "reiser4.h"

/** 
 * initialize condition variable. Initializer for global condition variables
 * is macro in kcond.h 
 */
kcond_t *kcond_init( kcond_t *cvar /* cvar to init */ )
{
	assert( "nikita-1868", cvar != NULL );

	xmemset( cvar, 0, sizeof *cvar );
	spin_lock_init( &cvar -> lock );
	cvar -> queue = NULL;
	return cvar;
}

/**
 * destroy condition variable.
 */
int kcond_destroy( kcond_t *cvar /* cvar to destroy */ )
{
	return kcond_are_waiters( cvar ) ? -EBUSY : 0;
}

/**
 * Wait until condition variable is signalled. Call this with @lock locked.
 * If @signl is true, then sleep on condition variable will be interruptible
 * by signals. -EINTR is returned if sleep were interrupted by signal and 0
 * otherwise.
 *
 * kcond_t is just a queue protected by spinlock. Whenever thread is going to
 * sleep on the kcond_t it does the following:
 *
 *  (1) prepares "queue link" @qlink which is semaphore constructed locally on
 *  the stack of the thread going to sleep.
 *
 *  (2) takes @cvar spinlock
 *
 *  (3) adds @qlink to the @cvar queue of waiters
 * 
 *  (4) releases @cvar spinlock
 * 
 *  (5) sleeps on semaphore constructed at step (1)
 *
 * When @cvar will be signalled or broadcasted all semaphors enqueued to the
 * @cvar queue will be upped and kcond_wait() will return.
 *
 * By use of local semaphore for each waiter we avoid races between going to
 * sleep and waking up---endemic plague of condition variables.
 *
 * For example, should kcond_broadcast() come in between steps (4) and (5) it
 * would call up() on semaphores already in a queue and hence, down() in the
 * step (5) would return immediately.
 *
 */
int kcond_wait( kcond_t *cvar /* cvar to wait for */, 
		spinlock_t *lock /* lock to use */, 
		int signl /* if 0, ignore signals during sleep */ )
{
	kcond_queue_link_t qlink;
	int result;
  
	assert( "nikita-1869", cvar != NULL );
	assert( "nikita-1870", lock != NULL );
	assert( "nikita-1871", check_spin_is_locked( lock ) );

	spin_lock( &cvar -> lock );
	qlink.next = cvar -> queue;
	cvar -> queue = &qlink;
	init_MUTEX_LOCKED( &qlink.wait );
	spin_unlock( &cvar -> lock );
	spin_unlock( lock );

	result = 0;
	if( signl )
		result = down_interruptible( &qlink.wait );
	else
		down( &qlink.wait );
	/*
	 * wait until kcond_{broadcast|signal} finishes. Otherwise down()
	 * could interleave with up() in such a way that, that kcond_wait()
	 * would exit and up() would see garbage in a semaphore.
	 */
	spin_lock( &cvar -> lock );
	spin_unlock( &cvar -> lock );
	spin_lock( lock );
	return result;
}

/**
 * Signal condition variable: wake up one waiter, if any.
 */
int kcond_signal( kcond_t *cvar /* cvar to signal */ )
{
	kcond_queue_link_t *queue_head;

	assert( "nikita-1872", cvar != NULL );

	spin_lock( &cvar -> lock );

	queue_head = cvar -> queue;
	if( queue_head != NULL ) {
		cvar -> queue = queue_head -> next;
		up( &queue_head -> wait );
	}
	spin_unlock( &cvar -> lock );
	return 1;
}

/**
 * Broadcast condition variable: wake up all waiters.
 */
int kcond_broadcast( kcond_t *cvar /* cvar to broadcast */ )
{
	kcond_queue_link_t *queue_head;

	assert( "nikita-1875", cvar != NULL );

	spin_lock( &cvar -> lock );

	for( queue_head = cvar -> queue ; 
	     queue_head != NULL ; queue_head = queue_head -> next )
		up( &queue_head -> wait );

	cvar -> queue = NULL;
	spin_unlock( &cvar -> lock );
	return 1;
}

/** true if there are threads sleeping on @cvar */
int kcond_are_waiters( kcond_t *cvar /* cvar to query */ )
{
	assert( "nikita-1877", cvar != NULL );
	return cvar -> queue != NULL;
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
