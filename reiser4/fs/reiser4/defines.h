/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Universal header file.
 */

/* $Id$ */

#if !defined( __REISER4_DEFINES_H__ )
#define __REISER4_DEFINES_H__

/** number of bits in size of VFS block (512==2^9) */
#define VFS_BLKSIZE_BITS 9
/** mark function argument as unused */
#define UNUSED_ARG __attribute__( ( unused ) )

/** implication */
#define ergo( antecedent, consequent ) ( !( antecedent ) || ( consequent ) )
/** logical equivalence */
#define equi( p1, p2 ) ( ergo( ( p1 ), ( p2 ) ) && ergo( ( p2 ), ( p1 ) ) )

/* REISER4_USER_LEVEL_SIMULATION macro should be defined on cc command
   line */
#if REISER4_USER_LEVEL_SIMULATION

/* user-level simulation header */

#include "ulevel.h"

#define REISER4_DEBUG_MODIFY 0 /* this significantly slows down testing, but we should run
				* our testsuite through with this every once in a
				* while. */

#else

/* kernel headers */

/* for error codes in enums */
#include <asm/errno.h>
/* for size_t */
#include <linux/types.h>
/* for __u?? */
#include <asm/types.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/fs.h>
/* for kernel_locked(), lock_kernel() etc. */
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

/* following for includes are used in debug.h */
#include <linux/config.h>
#include <asm/hardirq.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/sched.h>

#define no_context      ( in_interrupt() || in_irq() )
#define current_pname   ( current -> comm )
#define current_pid     ( current -> pid )

/* REISER4_USER_LEVEL_SIMULATION */
#endif

#include "forward.h"

#include "reiser4.h"

#include "reiser4_i.h"
#include "reiser4_sb.h"

#include "build.h"
#include "debug.h"

#define sizeof_array(x) ((int) (sizeof(x) / sizeof(x[0])))

/** Define serveral inline functions for each type of spinlock. */
#define SPIN_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
static inline void spin_lock_ ## NAME (TYPE *x)					\
{										\
	assert ("nikita-1383", spin_ordering_pred_ ## NAME (x));		\
	spin_lock (& x->FIELD);							\
	ON_DEBUG( ++ get_lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( ++ get_lock_counters() -> spin_locked );			\
}										\
										\
static inline int  spin_trylock_ ## NAME (TYPE *x)				\
{										\
	if (spin_trylock (& x->FIELD)) {					\
		ON_DEBUG( ++ get_lock_counters() -> spin_locked_ ## NAME );	\
		ON_DEBUG( ++ get_lock_counters() -> spin_locked );		\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
static inline void spin_unlock_ ## NAME (TYPE *x)				\
{										\
	assert( "nikita-1375",							\
		get_lock_counters() -> spin_locked_ ## NAME > 0 );		\
	assert( "nikita-1376",							\
		get_lock_counters() -> spin_locked > 0 );			\
	ON_DEBUG( -- get_lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( -- get_lock_counters() -> spin_locked );			\
	spin_unlock (& x->FIELD);						\
}										\
										\
static inline int  spin_ ## NAME ## _is_locked (TYPE *x)			\
{										\
	return spin_is_locked (& x->FIELD);					\
}										\
										\
static inline int  spin_ ## NAME ## _is_not_locked (TYPE *x)			\
{										\
	return spin_is_not_locked (& x->FIELD);					\
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

#include "dformat.h"
#include "oid.h"
#include "key.h"
#include "kassign.h"
#include "plugin/item/static_stat.h"
#include "plugin/item/internal.h"
#include "plugin/item/directory_entry.h"
#include "plugin/item/compressed_de.h"
#include "plugin/item/extent.h"
#include "plugin/file/file.h"
#include "plugin/dir/hashed_dir.h"
#include "plugin/dir/dir.h"
#include "plugin/item/item.h"
#include "plugin/node/node.h"
#include "plugin/node/node40.h"
#include "tshash.h"
#include "tslist.h"
#include "plugin/types.h"
#include "slum.h"
#include "coords.h"
#include "znode.h"
#include "txnmgr.h"
#include "tree.h"
#include "block_alloc.h"
#include "tree_walk.h"
#include "pool.h"
#include "treemod.h"
#include "do_carry.h"
#include "carry_ops.h"

#include "inode.h"
#include "super.h"

/* __REISER4_DEFINES_H__ */
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
