/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* definitions of common constants and data-types used by
   reiser4_fs_i.h and reiser4_fs_sb.h */

#if !defined( __REISER4_H__ )
#define __REISER4_H__

#include <asm/param.h>		/* for HZ */
#include <linux/errno.h>

extern const char *REISER4_SUPER_MAGIC_STRING;
extern const int REISER4_MAGIC_OFFSET;	/* offset to magic string from the
					 * beginning of device */

/* here go tunable parameters that are not worth special entry in kernel
   configuration */

/* default number of slots in coord-by-key caches */
#define CBK_CACHE_SLOTS    (16)
/* how many elementary tree operation to carry on the next level */
#define CARRIES_POOL_SIZE        (5)
/* size of pool of preallocated nodes for carry process. */
#define NODES_LOCKED_POOL_SIZE   (5)

#define REISER4_NEW_NODE_FLAGS (COPI_LOAD_LEFT | COPI_LOAD_RIGHT | COPI_GO_LEFT)
#define REISER4_NEW_EXTENT_FLAGS (COPI_LOAD_LEFT | COPI_LOAD_RIGHT | COPI_GO_LEFT)
#define REISER4_PASTE_FLAGS (COPI_GO_LEFT)
#define REISER4_INSERT_FLAGS (COPI_GO_LEFT)

/* we are supporting reservation of disk space on uid basis */
#define REISER4_SUPPORT_UID_SPACE_RESERVATION (0)
/* we are supporting reservation of disk space for groups */
#define REISER4_SUPPORT_GID_SPACE_RESERVATION (0)
/* we are supporting reservation of disk space for root */
#define REISER4_SUPPORT_ROOT_SPACE_RESERVATION (0)

/* key allocation is Plan-A */
#define REISER4_PLANA_KEY_ALLOCATION (1)
/* key allocation follows good old 3.x scheme */
#define REISER4_3_5_KEY_ALLOCATION (0)

/* size of hash-table for znodes */
#define REISER4_ZNODE_HASH_TABLE_SIZE (1 << 13)
/* order of emergency flush hash table */
#define REISER4_EF_HASH_BITS (10)
/* size of emergency flush hash table */
#define REISER4_EF_HASH_SIZE (1 << REISER4_EF_HASH_BITS)

/* size of hash-table for jnodes */
#define REISER4_JNODE_HASH_TABLE_BITS (13)
#define REISER4_JNODE_HASH_TABLE_SIZE (1 << REISER4_JNODE_HASH_TABLE_BITS)

/* number of buckets in lnode hash-table */
#define LNODE_HTABLE_BUCKETS (1024)

/* some ridiculously high maximal limit on height of znode tree. This
    is used in declaration of various per level arrays and
    to allocate stattistics gathering array for per-level stats. */
#define REISER4_MAX_ZTREE_HEIGHT     (10)

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

#define REISER4_PANIC_MSG_BUFFER_SIZE (1024)

/* If array contains less than REISER4_SEQ_SEARCH_BREAK elements then,
   sequential search is on average faster than binary. This is because
   of better optimization and because sequential search is more CPU
   cache friendly. This number (25) was found by experiments on dual AMD
   Athlon(tm), 1400MHz.
  
   NOTE: testing in kernel has shown that binary search is more effective than
   implied by results of the user level benchmarking. Probably because in the
   node keys are separated by other data. So value was adjusted after few
   tests. More thorough tuning is needed.
*/
#define REISER4_SEQ_SEARCH_BREAK      (3)

/* don't allow tree to be lower than this */
#define REISER4_MIN_TREE_HEIGHT       (TWIG_LEVEL)

/* default file plugin */
#define REISER4_FILE_PLUGIN       (UNIX_FILE_PLUGIN_ID)

/* default directory plugin */
#define REISER4_DIR_PLUGIN        (HASHED_DIR_PLUGIN_ID)

/* default hash plugin */
#define REISER4_HASH_PLUGIN       (R5_HASH_ID)

/* default perm(ission) plugin */
#define REISER4_PERM_PLUGIN       (RWX_PERM_ID)

/* default tail policy plugin */
#define REISER4_TAIL_PLUGIN       (ALWAYS_TAIL_ID)

/* item plugin used by files by default to store stat data. */
#define REISER4_SD_PLUGIN         (STATIC_STAT_DATA_ID)

/* item plugin used by directories by default to store directory entries. */
#define REISER4_DIR_ITEM_PLUGIN       (COMPOUND_DIR_ID)

/* NOTE NIKITA this is no longer used: maximal atom size is auto-adjusted to
 * available memory. */
/* Default value of maximal atom size. Can be ovewritten by
   tmgr.atom_max_size mount option. By default infinity. */
#define REISER4_ATOM_MAX_SIZE         ((unsigned)(~0))

/* Default value of maximal atom age (in jiffies). After reaching this age
   atom will be forced to commit, either synchronously or asynchronously. Can
   be overwritten by tmgr.atom_max_age mount option. */
#define REISER4_ATOM_MAX_AGE          (600 * HZ)

/* After percentage of free memory falls below this, preventive flushing is
   started. */
#define REISER4_LOW_MEMORY (50)

/* sleeping period for ktxnmrgd */
#define REISER4_TXNMGR_TIMEOUT  (5 * HZ)

/* timeout to wait for ent thread in writepage. Default: 3 milliseconds. */
#define REISER4_ENTD_TIMEOUT (3 * HZ / 1000)

/* start complaining after that many restarts in coord_by_key().
  
   This either means incredibly heavy contention for this part of a tree, or
   some corruption or bug.
*/
#define REISER4_CBK_ITERATIONS_LIMIT  (100)

/* return -EIO after that many iterations in coord_by_key().
  
   I have witnessed more than 800 iterations (in 30 thread test) before cbk
   finished. --nikita
*/
#define REISER4_MAX_CBK_ITERATIONS    (10000)

/* read all blocks when one block on the page is read */
#define REISER4_FORMATTED_CLUSTER_READ (0)

/* put a per-inode limit on maximal number of directory entries with identical
   keys in hashed directory.
  
   Disable this until inheritance interfaces stabilize: we need some way to
   set per directory limit.
*/
#define REISER4_USE_COLLISION_LIMIT    (0)

/* global limit on number of directory entries with identical keys in hashed
   directory */
#define REISER4_GLOBAL_COLLISION_LIMIT (1024)

/* maximal number of keys lookup_multikey() can handle. */
#define REISER4_MAX_MULTI_SEARCH (4)

/* If flush finds more than FLUSH_RELOCATE_THRESHOLD adjacent dirty leaf-level blocks it
   will force them to be relocated. */
#define FLUSH_RELOCATE_THRESHOLD 64
/* If flush finds can find a block allocation closer than at most FLUSH_RELOCATE_DISTANCE
   from the preceder it will relocate to that position. */
#define FLUSH_RELOCATE_DISTANCE  64
/* Flush defers actualy BIO submission until it gathers FLUSH_QUEUE_SIZE blocks. */
#define FLUSH_QUEUE_SIZE         256

/* If we have written this much or more blocks before encountering busy jnode
   in flush list - abort flushing hoping that next time we get called
   this jnode will be clean already, and we will save some seeks. */
#define FLUSH_WRITTEN_THRESHOLD 50

/* The maximum number of nodes to scan left on a level during flush. */
#define FLUSH_SCAN_MAXNODES 10000

/* default tracing buffer size */
#define REISER4_TRACE_BUF_SIZE ( 1 << 21 )

/* what size units of IO we would like cp, etc., to use, in writing to
   reiser4. In bytes.
  
   Can be overwritten by optimal_io_size mount option.
*/
#define REISER4_OPTIMAL_IO_SIZE (64 * 1024)

/* see comments in inode.c:oid_to_uino() */
#define REISER4_UINO_SHIFT (1 << 30)

/* Mark function argument as unused to avoid compiler warnings. */
#define UNUSED_ARG __attribute__((unused))

#if ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 3)
#define NONNULL __attribute__((nonnull))
#else
#define NONNULL
#endif

/* size of VFS block */
#define VFS_BLKSIZE 512
/* number of bits in size of VFS block (512==2^9) */
#define VFS_BLKSIZE_BITS 9

#define REISER4_I reiser4_inode_data

/* implication */
#define ergo( antecedent, consequent ) ( !( antecedent ) || ( consequent ) )
/* logical equivalence */
#define equi( p1, p2 ) ( ergo( ( p1 ), ( p2 ) ) && ergo( ( p2 ), ( p1 ) ) )

#define sizeof_array(x) ((int) (sizeof(x) / sizeof(x[0])))


#define REISER4_USER_LEVEL_SIMULATION (0)
#define YOU_CAN_COMPILE_PSEUDO_CODE   (0)
#define NOT_YET                       (0)

/* Certain user-level testing requirements */
#if REISER4_USER_LEVEL_SIMULATION

#include "ulevel/ulevel.h"

#endif

#ifndef __KERNEL__
#include "build.h"
#endif

/** Reiser4 specific error codes **/

#define REISER4_ERROR_CODES_BASE 500

/* Neighbor is not available (side neighbor or parent) */
#define E_NO_NEIGHBOR  (REISER4_ERROR_CODES_BASE)

/* Node was not found in cache */
#define E_NOT_IN_CACHE (REISER4_ERROR_CODES_BASE + 1)

/* node has no free space enough for completion of balancing operation */
#define E_NODE_FULL    (REISER4_ERROR_CODES_BASE + 2)

#endif				/* __REISER4_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
