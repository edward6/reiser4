/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * reiser4_fs_i.h and reiser4_fs_sb.h
 */

#if !defined( __REISER4_H__ )
#define __REISER4_H__

extern const char *REISER4_SUPER_MAGIC_STRING;
extern const int REISER4_MAGIC_OFFSET; /* offset to magic string from the
					* beginning of device */

/* here go tunable parameters that are not worth special entry in kernel
   configuration */

/** number of slots in coord-by-key caches */
#define CBK_CACHE_SLOTS    (32)
/** how many elementary tree operation to carry on the next level */
#define CARRIES_POOL_SIZE        (5)
/** size of pool of preallocated nodes for carry process. */
#define NODES_LOCKED_POOL_SIZE   (5)

/** we are supporting reservation of disk space on uid basis */
#define REISER4_SUPPORT_UID_SPACE_RESERVATION (0)
/** we are supporting reservation of disk space for groups */
#define REISER4_SUPPORT_GID_SPACE_RESERVATION (0)
/** we are supporting reservation of disk space for root */
#define REISER4_SUPPORT_ROOT_SPACE_RESERVATION (0)

/** enable lock passing support */
#define REISER4_SUPPORT_PASS_LOCK (0)

/** inodes are locked by single super-block lock */
#define REISER4_USE_SB_ILOCK    (0)
/** inodes are locked by ->i_data lock */
#define REISER4_USE_I_DATA_LOCK (0)

/** key allocation is Plan-A */
#define REISER4_PLANA_KEY_ALLOCATION (1)
/** key allocation follows good old 3.x scheme */
#define REISER4_3_5_KEY_ALLOCATION (0)

/** size of hash-table for znodes */
#define REISER4_ZNODE_HASH_TABLE_SIZE (8192)

/** number of buckets in lnode hash-table */
#define LNODE_HTABLE_BUCKETS (1024)

/** some ridiculously high maximal limit on height of znode tree. This
    is used in declaration of various per level arrays and
    to allocate stattistics gathering array for per-level stats. */
#define REISER4_MAX_ZTREE_HEIGHT     (10)

/** if this is non-zero, clear content of new node, otherwise leave
    whatever may happen to be here */
#define REISER4_ZERO_NEW_NODE   (1)

/* classical balancing algorithms require to update delimiting key in a
   parent every time one deletes leftmost item in its child so that
   delimiting key is actually exactly minimal key in a tree rooted at
   the right child of this delimiting key.

   It seems this condition can be relaxed to the following: all keys in
   a tree rooted in the left child of the delimiting key are less than
   said key and all keys in the tree rooted in the right child of the
   delimiting key are greater than or equal to the said delimiting key.

   This saves one io on deletion and as far as I can see, doesn't lead
   to troubles on parent splitting and merging. */
#define REISER4_EXACT_DELIMITING_KEY (0)
#define REISER4_NON_UNIQUE_KEYS      (1)

#define REISER4_PANIC_MSG_BUFFER_SIZE (1024)

/**
 * If array contains less than REISER4_SEQ_SEARCH_BREAK elements then,
 * sequential search is on average faster than binary. This is because
 * of better optimization and because sequential search is more CPU
 * cache friendly. This number was found by experiments on dual AMD
 * Athlon(tm), 1400MHz.
 *
 */
#define REISER4_SEQ_SEARCH_BREAK      (25)

/**
 * don't allow tree to be lower than this
 */
#define REISER4_MIN_TREE_HEIGHT       (TWIG_LEVEL)


/**
 * default file plugin
 */
#define REISER4_FILE_PLUGIN       (REGULAR_FILE_PLUGIN_ID)

/**
 * default directory plugin
 */
#define REISER4_DIR_PLUGIN        (HASHED_DIR_PLUGIN_ID)

/**
 * default hash plugin
 */
#define REISER4_HASH_PLUGIN       (R5_HASH_ID)

/**
 * default perm(ission) plugin
 */
#define REISER4_PERM_PLUGIN       (RWX_PERM_ID)

/**
 * default tail policy plugin
 */
#define REISER4_TAIL_PLUGIN       (NEVER_TAIL_ID)

/**
 * item plugin used by files by default to store stat data.
 */
#define REISER4_SD_PLUGIN         (STATIC_STAT_DATA_ID)

/**
 * item plugin used by directories by default to store directory entries.
 */
#define REISER4_DIR_ITEM_PLUGIN       (COMPOUND_DIR_ID)

/**
 * start complaining after that many restarts in coord_by_key().
 *
 * This either means incredibly heavy contention for this part of a tree, or
 * some corruption or bug.
 */
#define REISER4_CBK_ITERATIONS_LIMIT  (100)

/**
 * read all blocks when one block on the page is read
 */
#define REISER4_FORMATTED_CLUSTER_READ (0)

/**
 * put a per-inode limit on maximal number of directory entries with identical
 * keys in hashed directory.
 *
 * Disable this until inheritance interfaces stabilize: we need some way to
 * set per directory limit.
 */
#define REISER4_USE_COLLISION_LIMIT    (0)

/**
 * global limit on number of directory entries with identical keys in hashed
 * directory
 */
#define REISER4_GLOBAL_COLLISION_LIMIT (1024)

/* 
 * what size units of IO we would like cp, etc., to use, in writing to
 * reiser4. In 512 byte blocks.
 *
 * Currently this is constant (64k).
 */
/* AUDIT: THIS CALCULATION IS INCORRECT. stat(2) returns optimal i/o blocksize
   in bytes */
#define REISER4_OPTIMAL_IO_SIZE( super, inode ) ((64 * 1024) >> VFS_BLKSIZE_BITS)

/**
 * The maximum number of nodes to scan left on a level during flush.
 */
#define REISER4_FLUSH_SCAN_MAXNODES 10000


/* Mark function argument as unused to avoid compiler warnings. */
#define UNUSED_ARG __attribute__( ( unused ) )

/** size of VFS block */
#define VFS_BLKSIZE 512
/** number of bits in size of VFS block (512==2^9) */
#define VFS_BLKSIZE_BITS 9

#define REISER4_I reiser4_inode_data

/** implication */
#define ergo( antecedent, consequent ) ( !( antecedent ) || ( consequent ) )
/** logical equivalence */
#define equi( p1, p2 ) ( ergo( ( p1 ), ( p2 ) ) && ergo( ( p2 ), ( p1 ) ) )

#define sizeof_array(x) ((int) (sizeof(x) / sizeof(x[0])))

/* Certain user-level testing requirements */
#if REISER4_USER_LEVEL_SIMULATION

#include "ulevel/ulevel.h"

#else

/* kernel includes/defines */

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
#include <linux/buffer_head.h>
/* for kernel_locked(), lock_kernel() etc. */
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <linux/spinlock.h>
/* following for includes are used in debug.h */
#include <linux/config.h>
#include <asm/hardirq.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/module.h>
/* for {get|put}_unaligned() in dformat.h */
#include <asm/unaligned.h>
/* For __init definitions */
#include <linux/init.h>
/* For BIO stuff */
#include <linux/bio.h>

#define no_context      ( in_interrupt() || in_irq() )
#define current_pname   ( current -> comm )
#define current_pid     ( current -> pid )
#define set_current()   current_pid
#define pthread_self()  0

#endif

#include "forward.h"

#include "reiser4_sb.h"

#ifndef __KERNEL__
#include "build.h"
#endif
#include "debug.h"

/** Define serveral inline functions for each type of spinlock. */
#define SPIN_LOCK_FUNCTIONS(NAME,TYPE,FIELD)				\
									\
static inline void spin_lock_ ## NAME (TYPE *x)				\
{									\
	assert ("nikita-1383", spin_ordering_pred_ ## NAME (x));	\
	spin_lock (& x->FIELD);						\
	ON_DEBUG( ++ lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( ++ lock_counters() -> spin_locked );			\
}									\
									\
static inline int  spin_trylock_ ## NAME (TYPE *x)			\
{									\
	if (spin_trylock (& x->FIELD)) {				\
		ON_DEBUG( ++ lock_counters() -> spin_locked_ ## NAME );	\
		ON_DEBUG( ++ lock_counters() -> spin_locked );		\
		return 1;						\
	}								\
	return 0;							\
}									\
									\
static inline void spin_unlock_ ## NAME (TYPE *x)			\
{									\
	assert( "nikita-1375",						\
		lock_counters() -> spin_locked_ ## NAME > 0 );		\
	assert( "nikita-1376",						\
		lock_counters() -> spin_locked > 0 );			\
	ON_DEBUG( -- lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( -- lock_counters() -> spin_locked );			\
	spin_unlock (& x->FIELD);						\
}										\
										\
static inline int  spin_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return spin_is_locked (& x->FIELD);					\
}										\
										\
static inline int  spin_ ## NAME ## _is_not_locked (TYPE *x)			\
{										\
	return !spin_is_locked (& x->FIELD);				        \
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

#include "kcond.h"
#include "dformat.h"
#include "key.h"
#include "kassign.h"
#include "coord.h"
#include "seal.h"
#include "tshash.h"
#include "tslist.h"
#include "plugin/plugin_header.h"
#include "plugin/item/static_stat.h"
#include "plugin/item/internal.h"
#include "plugin/item/sde.h"
#include "plugin/item/cde.h"
#include "plugin/item/extent.h"
#include "plugin/item/tail.h"
#include "plugin/file/file.h"
#include "plugin/dir/hashed_dir.h"
#include "plugin/dir/dir.h"
#include "plugin/item/item.h"
#include "plugin/node/node.h"
#include "plugin/node/node40.h"
#include "plugin/security/perm.h"

#include "plugin/oid/oid_40.h"
#include "plugin/oid/oid.h"

#include "plugin/space/bitmap.h"
#include "plugin/space/test.h"
#include "plugin/space/space_allocator.h"

#include "plugin/disk_format/disk_format_40.h"
#include "plugin/disk_format/test.h"
#include "plugin/disk_format/disk_format.h"

#include "plugin/plugin.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree_walk.h"
#include "pool.h"
#include "tree_mod.h"
#include "carry.h"
#include "carry_ops.h"
#include "tree.h"

#include "inode.h"
#include "lnode.h"
#include "super.h"
#include "page_cache.h"

#endif /* __REISER4_H__ */

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
