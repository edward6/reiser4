/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* This data structure is used to allocate nodesof the SLPC data structures.
 */

#ifndef __SLPL_H__
#define __SLPL_H__

#if SLPC_USE_SPINLOCK

#define KUT_LOCK_SPINLOCK

#include <libkut/kutlock.h>

#if SLPC_USE_SPINLOCK == 2

typedef rwlock_t SLPC_SPINLOCK;

#define SLPC_SPINLOCK_INIT(x)        rwlock_init(&(x))
#define SLPC_SPINLOCK_GET_EXCL(x)    write_lock(&(x))
#define SLPC_SPINLOCK_PUT_EXCL(x)    write_unlock(&(x))
#define SLPC_SPINLOCK_GET_SHARED(x)  read_lock(&(x))
#define SLPC_SPINLOCK_PUT_SHARED(x)  read_unlock(&(x))

#else

typedef spinlock_t SLPC_SPINLOCK;

#define SLPC_SPINLOCK_INIT(x)        spin_lock_init(&(x))
#define SLPC_SPINLOCK_GET_EXCL(x)    spin_lock(&(x))
#define SLPC_SPINLOCK_PUT_EXCL(x)    spin_unlock(&(x))
#define SLPC_SPINLOCK_GET_SHARED(x)  spin_lock(&(x))
#define SLPC_SPINLOCK_PUT_SHARED(x)  spin_unlock(&(x))

#endif

#define SLPC_SPINLOCK_GET_LOCK(b,y)  do{if(b){ SLPC_SPINLOCK_GET_EXCL(y); }else{ SLPC_SPINLOCK_GET_SHARED(y); } } while (0)
#define SLPC_SPINLOCK_PUT_LOCK(b,y)  do{if(b){ SLPC_SPINLOCK_PUT_EXCL(y); }else{ SLPC_SPINLOCK_PUT_SHARED(y); } } while (0)
#define SLPC_INCREMENT_CYCLE(node)   (node)->_cycle += 1

#else

#define SLPC_SPINLOCK_INIT(x)        (void)0
#define SLPC_SPINLOCK_GET_EXCL(x)    (void)0
#define SLPC_SPINLOCK_PUT_EXCL(x)    (void)0
#define SLPC_SPINLOCK_GET_SHARED(x)  (void)0
#define SLPC_SPINLOCK_PUT_SHARED(x)  (void)0
#define SLPC_SPINLOCK_GET_LOCK(b,y)  (void)0
#define SLPC_SPINLOCK_PUT_LOCK(b,y)  (void)0
#define SLPC_INCREMENT_CYCLE(node)   (void)0

#endif

#endif  /* __SLPL_H__ */


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
