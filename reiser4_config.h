
/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* This file contains all defines useful to persons tuning/configuring
   reiser4. */

#if !defined( __REISER4_H__ )
#define __REISER4_H__

/* what size units of IO we would like cp, etc., to use, in writing to reiser4 */
#define OPTIMAL_REISER4_IO_SIZE 64 * 1024

/** number of slots in coord-by-key caches */
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

/** size of hash-table for jnodes */
#define REISER4_JNODE_HASH_TABLE_SIZE (8192)
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

#define REISER4_DIR_ITEM_PLUGIN       (COMPOUND_DIR_ID)

#define REISER4_CBK_ITERATIONS_LIMIT  (100)
/* __REISER4_H__ */
#endif

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
