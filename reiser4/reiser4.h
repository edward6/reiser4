
/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * reiser4_fs_i.h and reiser4_fs_sb.h
 */

#if !defined( __REISER4_H__ )
#define __REISER4_H__

/* here go tunable parameters that are not worth special entry in kernel
   configuration */

/** number of slots in point-by-key caches */
#define CBK_CACHE_SLOTS    (32)
/** how many elementary tree operation to carry on the next level */
#define CARRIES_POOL_SIZE        (10)
/** size of pool of preallocated nodes for carry process. */
#define NODES_LOCKED_POOL_SIZE   (10)

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

/** some ridiculously high maximal limit on height of znode tree. This
    is used in declaration of various per level arrays and
    to allocate stattistics gathering array for per-level stats. */
#define REISER4_MAX_ZTREE_HEIGHT     (20)

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
 * maximal number of operations that squeeze to left
 * (fs/reiser4/balance.c:squeeze_slum()) will defer.  Deferred
 * operations will be batched together.
 */
#define REISER4_SQUEEZE_OP_MAX        CARRIES_POOL_SIZE
/**
 * maximal number of nodes that slum squeezing will keep locked at a
 * time.
 */
#define REISER4_SQUEEZE_NODE_MAX      NODES_LOCKED_POOL_SIZE

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

#define REISER4_DIR_ITEM_PLUGIN       (COMPR_DIR_ITEM_ID)

#define REISER4_CBK_ITERATIONS_LIMIT  (100)

/* Mark function argument as unused to avoid compiler warnings. */
#define UNUSED_ARG __attribute__( ( unused ) )

/** number of bits in size of VFS block (512==2^9) */
#define VFS_BLKSIZE_BITS 9

/** implication */
#define ergo( antecedent, consequent ) ( !( antecedent ) || ( consequent ) )
/** logical equivalence */
#define equi( p1, p2 ) ( ergo( ( p1 ), ( p2 ) ) && ergo( ( p2 ), ( p1 ) ) )

/* Certain user-level testing requirements */
#if REISER4_USER_LEVEL_SIMULATION

#include "ulevel/ulevel.h"

#endif

#include "forward.h"

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
	spin_lock (& x->FIELD);						        \
	ON_DEBUG( ++ lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( ++ lock_counters() -> spin_locked );			\
}										\
										\
static inline int  spin_trylock_ ## NAME (TYPE *x)				\
{										\
	if (spin_trylock (& x->FIELD)) {					\
		ON_DEBUG( ++ lock_counters() -> spin_locked_ ## NAME );	\
		ON_DEBUG( ++ lock_counters() -> spin_locked );		\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
static inline void spin_unlock_ ## NAME (TYPE *x)				\
{										\
	assert( "nikita-1375",							\
		lock_counters() -> spin_locked_ ## NAME > 0 );		\
	assert( "nikita-1376",							\
		lock_counters() -> spin_locked > 0 );			\
	ON_DEBUG( -- lock_counters() -> spin_locked_ ## NAME );		\
	ON_DEBUG( -- lock_counters() -> spin_locked );			\
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
	return spin_is_not_locked (& x->FIELD);				        \
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

#include "dformat.h"
#include "oid.h"
#include "key.h"
#include "kassign.h"
#include "plugin/item/static_stat.h"
#include "plugin/item/internal.h"
#include "plugin/item/sde.h"
#include "plugin/item/cde.h"
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
#include "coords.h"
#include "znode.h"
#include "txnmgr.h"
#include "tree.h"
#include "slum.h"
#include "block_alloc.h"
#include "tree_walk.h"
#include "pool.h"
#include "tree_mod.h"
#include "carry.h"
#include "carry_ops.h"

#include "inode.h"
#include "super.h"

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
