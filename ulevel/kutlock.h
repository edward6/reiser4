/* Copyright (C) 2001, 2002 Hans Reiser.  All rights reserved.
 */

/* This file, part of KUTLIB (Kernel-User Test Library), provides the
 * Linux 2.4 spinlocks that can be linked into a user level
 * application for testing purposes.  The code in this directory was
 * taken from Linux 2.4.4 (atomic.h, spinlock.h, rwlock.h) and then
 * cleaned of a few unnecessary kernel dependencies (page.h, etc.).
 * The spinlock helper functions are taken out of semaphore.c. */

#ifndef __KUTLOCK_H__
#define __KUTLOCK_H__

#ifdef KUT_LOCK_SPINLOCK

#include "spinlock.h"

static __inline__ int
spin_is_not_locked (spinlock_t *s)
{
	/* Use the error checking locks */
	(void) s;
	return 1;
}

#define print_spin_lock(p, s) noop

#elif defined (KUT_LOCK_POSIX)

#include <errno.h>
#include <pthread.h>

typedef pthread_mutex_t spinlock_t;

#define SPIN_LOCK_UNLOCKED ( ( spinlock_t ) PTHREAD_MUTEX_INITIALIZER )

static __inline__ void
spin_lock_init (spinlock_t *s)
{
	pthread_mutex_init (s, NULL);
}

static __inline__ void
spin_lock (spinlock_t *s)
{
	pthread_mutex_lock (s);
}

static __inline__ void
spin_unlock (spinlock_t *s)
{
	pthread_mutex_unlock (s);
}

static __inline__ int
spin_trylock (spinlock_t *s)
{
	return pthread_mutex_trylock (s) == 0;
}

static __inline__ int
spin_is_locked (const spinlock_t *s)
{
	if (pthread_mutex_trylock ((spinlock_t *) s) != 0) {
		return 1;
	}

	pthread_mutex_unlock ((spinlock_t *) s);
	return 0;
}

static __inline__ int
spin_is_not_locked (spinlock_t *s)
{
	/* Use the error checking locks */
	(void) s;
	return 1;
}

static inline int atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

#define print_spin_lock(p, s) noop

#elif defined (KUT_LOCK_ERRORCHECK)

#if 0
#define spinlock_t realspinlock_t
#define spin_lock realspin_lock
#define spin_unlock realspin_unlock
#define spin_trylock realspin_trylock
#include "spinlock.h"
#undef spinlock_t
#undef spin_lock_init
#undef spin_lock
#undef spin_unlock
#undef spin_trylock
#undef spin_is_locked
#endif

#define realspin_lock pthread_mutex_lock
#define realspin_unlock pthread_mutex_unlock

#include <pthread.h>

typedef struct
{
	pthread_mutex_t  _real;
	pthread_mutex_t  _guard;
	int              _locked;
	pthread_t        _tid;
	int              _busy;
	__u32            _calls;
	__u32            _free;
	__u64            _sleep;
	__u64            _max_sleep;
} spinlock_t;

#undef SPIN_LOCK_UNLOCKED
#define SPIN_LOCK_UNLOCKED (spinlock_t) { ._real = PTHREAD_MUTEX_INITIALIZER, ._guard = PTHREAD_MUTEX_INITIALIZER, ._locked = 0, ._tid = 0, ._busy = 0, ._calls = 0, ._free = 0, ._sleep = 0ull, ._max_sleep = 0ull }

static __inline__ void
spin_lock_init (spinlock_t *s)
{
	pthread_mutex_init (& s->_real, NULL);
	pthread_mutex_init (& s->_guard, NULL);
	s->_locked = 0;
	s->_tid    = 0;
	s->_busy   = 0;
	s->_calls  = 0ull;
	s->_free   = 0ull;
	s->_sleep  = 0ull;
	s->_max_sleep = 0ull;
}

static __inline__ void
spin_lock (spinlock_t *s)
{
	__u64 sleep_start;
	__u64 sleep_end;
	__u64 sleep;

	pthread_t me = pthread_self ();

	pthread_mutex_lock (& s->_guard);
	if (s->_locked && s->_tid == me) {
		SPINLOCK_BUG ("recursive spinlock attempted");
	}

	s->_calls  += 1ull;
	if (!s->_busy)
		s->_free   += 1ull;
	s->_busy    = 1;
	pthread_mutex_unlock (& s->_guard);

	rdtscll (sleep_start);
	pthread_mutex_lock (& s->_real);
	rdtscll (sleep_end);

	pthread_mutex_lock (& s->_guard);
	sleep = sleep_end - sleep_start;
	s->_sleep += sleep;
	if (sleep > s->_max_sleep)
		s->_max_sleep = sleep;
	if (s->_locked) {
		SPINLOCK_BUG ("spinlock was locked by someone else");
	}
	s->_locked = 1;
	s->_tid    = me;
	pthread_mutex_unlock (& s->_guard);
}

static __inline__ void
spin_unlock (spinlock_t *s)
{
	pthread_t me = pthread_self ();

	pthread_mutex_lock (& s->_guard);
	if (! s->_locked) {
		SPINLOCK_BUG ("spinlock not locked");
	}
	if (s->_tid != me) {
		SPINLOCK_BUG ("spinlock not locked by me");
	}
	s->_locked = 0;
	s->_busy   = 0;
	s->_tid    = 0;
	pthread_mutex_unlock (& s->_real);
	pthread_mutex_unlock (& s->_guard);
}

static __inline__ int
spin_trylock (spinlock_t *s)
{
	pthread_t me;
	int   got_it;

	me     = pthread_self ();
	got_it = (pthread_mutex_trylock (& s->_real) == 0);

	pthread_mutex_lock (& s->_guard);
	if (got_it && s->_locked) {
		SPINLOCK_BUG ("spinlock was locked by someone else");
	}
	if (s->_locked && s->_tid == me) {
		if (got_it) {
			SPINLOCK_BUG ("recursive spinlock_trylock succeeded");
		}
	}
	if (got_it) {
		s->_locked = 1;
		s->_tid = me;
	}
	pthread_mutex_unlock (& s->_guard);
	return got_it;
}

/* This is intended for use in assertions--why else would you check is_locked and not use
 * trylock instead?  However, it returns false if the lock is unlocked. */
static __inline__ int
spin_is_locked (const spinlock_t *s)
{
	pthread_t me = pthread_self ();
	int ret;

	pthread_mutex_lock (& ((spinlock_t * ) s)->_guard);
	if (s->_locked && s->_tid != me) {
		SPINLOCK_BUG ("invalid spinlock assertion");
	}
	ret = s->_locked;
	pthread_mutex_unlock (& ((spinlock_t * ) s)->_guard);
	return ret;
}

/* This can only be used in assertions--it states that the spinlock is not held by the
 * caller's thread. */
static __inline__ int
spin_is_not_locked (spinlock_t *s)
{
	pthread_t me = pthread_self ();
	int ret;

	pthread_mutex_lock (& s->_guard);
	if (s->_locked && s->_tid == me) {
		/* false if this thread holds the lock */
		SPINLOCK_BUG ("invalid spinlock assertion");
	} else {
		/* true otherwise, even if someone else holds the lock */
		ret = 1;
	}
	pthread_mutex_unlock (& s->_guard);
	return ret;
}

static inline int atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

#else

#error #define KUT_LOCK_SPINLOCK or KUT_LOCK_POSIX

#endif

#endif

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
