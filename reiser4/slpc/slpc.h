/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald <jmacd@cs.berkeley.edu>
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/*  Please send questions/comments to the author, Joshua MacDonald
 *  <jmacd@cs.berkeley.edu>, or visit the web site at
 *  http://sourceforge.net/projects/skiplist for the latest updates.
 */

/*********************************************************************

    CACHE-OPTIMIZED SKIP LIST WITH NODE-LEVEL CONCURRENCY CONTROL

 *********************************************************************/

/*
 *  The skip list implemented here is parametrized by the number of
 *  keys per node.  This allows the node size to be adjusted according
 *  to the type of key and data so that an optimal node size can be
 *  selected.  The skip-list is maintained using deterministic,
 *  top-down algorithms, which will be described later.  The
 *  illustration here is for a 1-2-3 skip list, meaning that each
 *  level of the list skips one, two, or three elements of the list a
 *  level below.  Except for the root and pivot nodes, which are
 *  treated as special cases, an internal node of the 1-2-3 skip list
 *  has anywhere between two and four "down" pointers.  A "down"
 *  pointer points to a particular node in the child tree-level.  The
 *  first down pointer of a node points to the corresponding skip-list
 *  directly node below it (the picture below will help visualize),
 *  and the one, two, or three remaining down pointers point to the
 *  "skipped" nodes.  Each level of the skip list maintains pointers
 *  to the right, which allows, in particular, the leaves to be
 *  scanned in ascending order.
 *
 *  Much like a B+tree, the algorithms maintain a constant height for
 *  all leaves: exactly the same number of nodes are traversed from
 *  the root to every leaf.  The height only increases when the root
 *  node splits, and the height hold decreases when the root node
 *  underflows (is reduced to a single child).  This property is
 *  maintained through several invariants which enforce maximum and
 *  minimum node occupancies.  The algorithms split and join nodes as
 *  they traverse the skip list to preserve the height-balanced
 *  property.  A 1-2-3 skip list is structurally similar to a 2-3-4
 *  B+tree, only the algorithms differ.
 *
 *  This implementation does not support duplicates.  (It would
 *  otherwise make a decent priority queue.)
 *
 *  Each node of the skip list has a read-write spinlock used to
 *  protect access to the node.  Every algorithm that operates on the
 *  skip list acts in a single, rightward and downward pass through
 *  the skip list.  That is why the algorithms are called top-down
 *  algorithms.  There are no recursive functions and no backing-up to
 *  finish the operation, no stack is maintained.
 *
 *  Each algorithm executes a single for-loop that descends one level
 *  with each iteration.  Currently, sequential search is used at each
 *  level to locate the next child key, but that may be replaced with
 *  binary search for large node sizes.
 *
 *  The invariants are preserved using special preconditions that
 *  require occasionally splitting and joining adjacent nodes of the
 *  skip list: (1) Prior to descending for insertion, the child node
 *  must be below its maximum occupancy.  Being below maximum
 *  occupancy always leaves room for a single insertion.  If the child
 *  node is at its maximum occupancy during an insertion operation,
 *  the child node must be split into two nodes, which requires
 *  inserting a single pointer at the present location.  The top-down
 *  precondition assures that splitting will be possible by ensuring
 *  that a single entry can always be inserted during the next
 *  iteration. (2) Prior to descending for deletion, the child node
 *  must be above its minimum occupancy.  Similarly to insertion, this
 *  allows room for a single deletion during the next iteration.
 *
 *  These algorithms can operate on the skip list:
 *
 *    INSERT-KEY:    Insert a specific key
 *    SEARCH-EXACT:  Search for a specific key
 *    SEARCH-LUB:    Find the least-valued key greater than the search key
 *    SEARCH-MIN:    Find the least-valued key
 *    DELETE-KEY:    Delete a specific key
 *    DELETE-MIN:    Delete the least-valued key
 *
 *  The locking protocol used to cover the top-down algorithms is
 *  optimistic.  For search, shared locks are aquired first on the
 *  pivot, then it descends through the list aquiring the child lock
 *  before releasing the parent lock.  For insertion and deletion,
 *  shared locks are aquired on internal nodes and finally an
 *  exclusive lock is aquired on the leaf node.
 *
 *  When the need to split, join, or reorganize nodes, we know the
 *  process holds a shared lock on the present node.  The process
 *  holds a shared lock because the node is internal, and the
 *  preconditions have prepared room for expansion or contraction of
 *  the skip-list at the present node.  The present node has several
 *  children, one of which was chosen to be the next node, but it
 *  failed the top-down precondition.
 *
 *  Prior to modifying any nodes, the process must aquire exclusive
 *  locks on two nodes (in the case of split) or three nodes (in the
 *  case of join).  To upgrade the locks to exclusive locks and avoid
 *  deadlock, each node maintains a modification counter which is
 *  incremented every time the node changes.
 *
 *  In case of a split, the process releases the parent and child
 *  locks, then reaquires the parent exclusive, then reaquires the
 *  child exclusive.  Occasionally, another process will gain
 *  exclusive access before the current process, which is detected
 *  using the modification counter and causes the operation to restart
 *  from the beginning.  At this point if the upgrade is successful,
 *  the process allocates a new child and performs the split, finally
 *  releasing the present-node exclusive-lock after descending to the
 *  next-child.
 *
 *  In case of a join, the process must aquire three locks including
 *  the parent and child and either the left- or right-neighbor of the
 *  child in question.  To avoid deadlock, this is treated as two
 *  separate cases, always locking in left-to-right order: (1) when
 *  the right-child is available, lock the next-child followed by the
 *  right-child, and (2) when only the left-child is available, lock
 *  the left-child followed by the next-child to its right.
 *
 *  The pivot node exists for the sole purpose of protecting the
 *  pointer to the root node.  This greatly simplifies the code
 *  because it follows the same top-down locking steps for the root
 *  node.  On insertion, if the root node is at maximum capacity, the
 *  rules for splitting it are special: (1) the pivot lock is upgraded
 *  to exclusive, (2) the root is split and a new root is created with
 *  two children, (3) the height is incremented by one, (4) the pivot
 *  is updated with the new root, and (5) the pivot lock is released.
 *
 *  The root node does not obey by the minimum node capacity rule as
 *  other nodes do.  If the root node is a leaf, it may have from zero
 *  to its maximum capacity.  If the root node is internal, it may
 *  have from one to its maximum capacity, in other words, its minimum
 *  capacity is one.  On deletion, if the root node has only a single
 *  child, the height of the skip list is reduced by one and the pivot
 *  is updated.
 *
 *  There are four helper functions for insertion:
 *
 *    SLPC_UPGRADE_LOCKS:    Upgrades parent and child locks to exclusive
 *    SLPC_SHIFT_KEYS_RIGHT: Shift key/item pairs to the right by one slot
 *    SLPC_NODE_NEW:         Allocate a new node
 *    SLPC_SPLIT_NODE:       Divide the old contents into the two children
 *
 *  There are six helper functions for deletion:
 *
 *    SLPC_UPGRADE_3LOCKS:    Upgrades parent, left-child, and right-child
 *    SLPC_SHIFT_KEYS_LEFT:   Shift key/item pairs to the left by one slot
 *    SLPC_REDIST_RIGHT_LEFT: Move some elements out of the right into the left
 *    SLPC_REDIST_LEFT_RIGHT: Move some elements out of the left into the right
 *    SLPC_JOIN_NODES:        Concatenate two minimum capacity nodes
 *    SLPC_NODE_FREE:         Return a node to the freelist
 *
 *  The skip list data structure looks something like this:
 *
 *  AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
 *  A   SKIP LIST ANCHOR                                         A
 *  A                                                            A
 *  A                    NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN   A
 *  A                    N   PIVOT NODE                      N   A
 *  A                    N                                   N   A
 *  A                    N   RIGHT---------------------------------> NULL
 *  A   PIVOT----------->N   RW-SPINLOCK  ALWAYS   PIVOT     N   A
 *  A   KEYCOUNT=        N   COUNT=1      HAS ONE  LOCK      N   A
 *  A   HEIGHT=          N                DOWN     PROTECTS  N   A
 *  A   FREELIST->       N   KEY0 : DOWN0 POINTER, ROOT      N   A
 *  A   LEFTLEAF--       N  (DON'T    |   THE ROOT SPLITS    N   A
 *  A            |       N   CARE)    |                      N   A
 *  A            |       NNNNNNNNNNNNN|NNNNNNNNNNNNNNNNNNNNNNN   A
 *  A            |                    |                          A
 *  AAAAAAAAAAAAA|AAAAAAAAAAAAAAAAAAAA|AAAAAAAAAAAAAAAAAAAAAAAAAAA
 *               |                    |
 *               |                    V
 *               |      NNNNNNNNNNNNNNNNNNN
 *               |      N   ROOT NODE     N
 *               |      N                 N
 *               |      N   RIGHT-----------> NULL
 *               |      N   RW-SPINLOCK   N
 *               |      N   COUNT=3       N
 *               |      N                 N
 *               |      N  16 | 30 | DC | N
 *               |      N     |    |    | N
 *               |      NNNNNN|NNNN|NNNN|NN
 *               |            |    |    |
 *               |            |    |    -----------------------------------------
 *               |            |    --------------------|                        |
 *               |            V                        V                        V
 *               |      NNNNNNNNNNNNNNNNNNN      NNNNNNNNNNNNNNNNNNN      NNNNNNNNNNNNNNNNNNNNNNNN
 *               |      N   INTERNAL NODE N	 N   INTERNAL NODE N	  N   INTERNAL NODE      N
 *               |      N                 N	 N                 N	  N                      N
 *               |      N   RIGHT--------------->N   RIGHT--------------->N   RIGHT----------------> NULL
 *               |      N   RW-SPINLOCK   N	 N   RW-SPINLOCK   N	  N   RW-SPINLOCK        N
 *               |      N   COUNT=2       N	 N   COUNT=3       N	  N   COUNT=4            N
 *               |      N                 N	 N                 N	  N                      N
 *               |      N   4 | 10 |      N	 N  21 | 25 | 30 | N	  N  37 | 44 | 51 | DC | N
 *               |      N     |    |      N	 N     |    |    | N	  N     |    |    |    | N
 *               |      NNNNNN|NNNN|NNNNNNN	 NNNNNN|NNNN|NNNN|NN	  NNNNNN|NNNN|NNNN|NNNN|NN
 *               |            |    |      	       |    |    |  	        |    |    |    |
 *               |            |    V      	       V    V    V  	        V    V    V    V
 *               |            |
 *               |            |    EACH LEVEL CONSISTS OF A RIGHT-LINKED LIST WITH
 *               |            |    BETWEEN 2x AND 4x AS MANY NODES
 *               |            |
 *               |            |    |    |    |    |    |    |    |    |    |    |    |    |    |
 *               |            |    |    |    |    |    |    |    |    |    |    |    |    |    |    ... --> NULL
 *               |            |    |    |    |    |    |    |    |    |    |    |    |    |    |
 *               |            |
 *               |            V
 *               |      NNNNNNNNNNNNNNNNNNN
 *               |      N   LEAF NODE     N
 *               |      N                 N
 *               |      N   RIGHT--------------> ...
 *		 |----->N   RW-SPINLOCK   N
 *			N   COUNT=3       N
 *			N                 N
 *			N  1,V0,3,V1,4,V2 N
 *			N                 N
 *			NNNNNNNNNNNNNNNNNNN
 *
 *
 * To define a version of this code (using static linkage), you
 * must define:
 *
 *   SLPC_PARAM_NAME:    which is a suffix to distinguish function names
 *   SLPC_KEY:           which is the key data type
 *   SLPC_DATA:          which is the datum data type
 *   SLPC_MAX_COUNT:     which is the max occupancy of a node
 *   SLPC_PAD_COUNT:     which is the number of bytes to pad
 *   SLPC_TARGET_SIZE:   which is the target node size in bytes (set to width of a cache-line)
 *   SLPC_USE_SPINLOCK:  if 2 use read-write spinlock, if 1 use exclusive spinlock, if 0 use no locking
 *   SLPC_ASSERT:        assertion macro
 *   SLPC_EQUAL_TO:      equality function   ==
 *   SLPC_GREATER_THAN:  comparison function >
 *   SLPC_GREATER_EQAUL: comparison function >=
 *
 * TODO list:
 *
 * 1. Add delete_min to the testslpc2 workload.
 * 2. Implement scanning pointer/iterator
 * 3. Experiment with delete-at-empty instead of delete-at-half
 * 4. Can you use trylock to optimize the lock_upgrade proceedures?
 * 5. Think about the unlikely()/likely() macros...
 * 6. Think about spin_lock_prefetch & other prefetch issues
 */

/* To turn on debugging: */
#ifndef SLPC_DEBUG
#define SLPC_DEBUG 0
#endif

#ifndef SLPC_COUNT_RESTARTS
#define SLPC_COUNT_RESTARTS 0
#endif

#ifndef SLPC_COUNT_KEYS
#define SLPC_COUNT_KEYS 0
#endif

#ifndef SLPC_COUNT_NODES
#define SLPC_COUNT_NODES 0
#endif

#ifndef SLPC_USE_KUTSLAB
#define SLPC_USE_KUTSLAB 0
#endif

#define SLPC_INLINE __inline__

/* Fixed types used in this file:
 */
typedef int     SLPC_SIZE_T;
typedef short   SLPC_SHORT_T;
typedef char    SLPC_PAD_T;

/* Include spinlock macros.
 */
#include "slpl.h"

/* The SLPC_PARAM_NAME is appended to every type and function symbol name.
 */
#ifndef  SLPC_PARAM_NAME
#warning SLPC_PARAM_NAME is not defined
#define  SLPC_PARAM_NAME test
#endif
#define SLPC_STRING_NAME SLPC_STRINGIFY(SLPC_PARAM_NAME)

/* This data structure has several parameters that determine the node
 * size, data types, locking routines, etc.  These macros allow the
 * same code to be #included with different parameters and function
 * names.
 */
#define SLPC_PARAM(x)      SLPC_PARAM2(x,SLPC_PARAM_NAME)
#define SLPC_PARAM2(x,n)   SLPC_PARAM3(x,n)
#define SLPC_PARAM3(x,n)   x ## n

#define SLPC_STRINGIFY(x)  SLPC_STRINGIFY2(x)
#define SLPC_STRINGIFY2(x) #x

/* The unlikely macro. */
#ifndef unlikely
#if defined(__GNUC__) && __GNUC__ >= 3
#define unlikely(x) __builtin_expect((x),0)
#define likely(x)   __builtin_expect((x),1)
#else
#define unlikely(x) (x)
#define likely(x)   (x)
#endif
#endif

/* The assertion can be disabled and is always disabled by SLPC_DEBUG=0.
 */
#ifndef SLPC_ASSERT
#if SLPC_DEBUG
#define SLPC_ASSERT(x) \
    do { if (unlikely (! (x))) { printf ("%s:%d: SLPC_%s assertion failed: %s\n", __FILE__, __LINE__, SLPC_STRING_NAME, #x); \
    abort (); } } while (0)
#else
#define SLPC_ASSERT(x) (void)0
#endif
#endif

/* To avoid unnecessary headers define local NULL.
 */
#define SLPC_NULL          ((void*)0)

/* The minimum count is the max divided by two, rounding down so that
 * join is guaranteed to fit two minimum-occupancy nodes into one
 * node.  For example, a 3-4-5-6-7 Skip List allows from 3 to 7
 * entries per node.  Two 3-occupancy nodes can be combined into a
 * 6-occupancy node, but two 4-occupancy nodes could not fit a
 * 7-occupancy node.  For an even-sized node, the minimum is exactly
 * half of the maximum. */
#undef SLPC_MIN_COUNT
#define SLPC_MIN_COUNT (SLPC_MAX_COUNT - ((SLPC_MAX_COUNT+1) / 2))

/* The difference in MAX and MIN is SLPC_OVER_COUNT.  When a node
 * splits one side gets SLPC_OVER_COUNT and one gets SLPC_MIN_COUNT.
 *
 * @@ We know that (SLPC_OVER_COUNT >= SLPC_MIN_COUNT) for odd-node
 * occupancies, so insertion could favor MIN_COUNT on the
 * insertion-descending side.  Currently do not distinguish this case.
 * Could also detect ascending/descending insertions and pack full
 * nodes like Berkeley DB does. */
#undef SLPC_OVER_COUNT
#define SLPC_OVER_COUNT (SLPC_MAX_COUNT - SLPC_MIN_COUNT)

/* Currently only linear search is performed at the node.  Ideally
 * for large node sizes binary search would be compared with linear
 * search.  */
#define SLPC_LINEAR_SEARCH 1

/* Macros to access fields of each PAIR.  The PAIR consists of a KEY
 * and a data union of VALUE or DOWN-POINTER.
 */
#define SLPC_ITH_KEY(n,i)   (n)->_pairs[i]._key
#define SLPC_ITH_VALUE(n,i) (n)->_pairs[i]._dat._d_data
#define SLPC_ITH_DOWN(n,i)  (n)->_pairs[i]._dat._d_down

/* Macros to access the SLPC_PLACE_CTX nodes, commonly used in the
 * code below.  */
#define PLACE_NODE (ctx._place._node)
#define CHILD_NODE (ctx._child._node)
#define OTHER_NODE (ctx._other._node)

#undef SLPC_RESTART_OPERATION
#if SLPC_COUNT_RESTARTS
#define SLPC_RESTART_OPERATION() \
  SLPC_SPINLOCK_GET_EXCL (anchor->_rs._restart_count_lock); \
  anchor->_rs._restarts += 1; \
  SLPC_SPINLOCK_PUT_EXCL (anchor->_rs._restart_count_lock); \
  goto start_over
#else
#define SLPC_RESTART_OPERATION() \
  goto start_over
#endif

/* A node that consists of N down pointers only needs N-1 KEYS to
 * distinguish all of the children, however the code is greatly
 * simplified by carrying the additional key.  The last key of an
 * internal node is never checked, but it is copied just the same.
 * The last down pointer of a node is followed UNCONDITIONALLY.  The
 * last down key of an node is set to DONT_CARE (i.e., the pivot
 * entry, the last key of the right-most node of every internal
 * list). */
#if SLPC_DEBUG
#define SLPC_ITH_KEY_DONT_CARE(n,i)  memset (&(n)->_pairs[i]._key, 0, sizeof((n)->_pairs[i]._key))
#else
#define SLPC_ITH_KEY_DONT_CARE(n,i)  (void)0
#endif

/* Cache-line alignment: The node size is carefully sized but the
 * anchor needs alignment in case you require an array of SLPC
 * structures. @@ Would it help to align some of the members of
 * SLPC_ANCHOR?
 */
#undef SLPC_TARGETSIZE_ALIGNED
#define SLPC_TARGETSIZE_ALIGNED __attribute__((__aligned__(SLPC_TARGET_SIZE)))

/* This creates a larger anchor but splits the anchor fields into different cache lines
 * for greater concurrency. */
#undef SLPC_ANCHORFIELD_ALIGNED
#if SLPC_USE_SPINLOCK
#define SLPC_ANCHORFIELD_ALIGNED SLPC_TARGETSIZE_ALIGNED
#else
#define SLPC_ANCHORFIELD_ALIGNED
#endif

#undef SLPC_PIVOT
#define SLPC_PIVOT(a) ((SLPC_NODE*) & (a)->_pivot_node)

/* SLAB interface
 */
#undef SLPC_SLAB_CREATE
#undef SLPC_SLAB_DESTROY
#undef SLPC_SLAB_NEW
#undef SLPC_SLAB_FREE
#undef SLPC_SLAB

#if SLPC_USE_KUTSLAB

/* The KUTSLAB implementation should eventually be made compatible
 * with kmem_slab_cache, in which case this should be used.  For
 * testing/use at user level the KUTSLAB implementation is not as good
 * as the version below. */
#if SLPC_USE_SPINLOCK
#define KUT_USE_LOCKS
#define KUT_LOCK_SPINLOCK
#endif

#include <libkut/kutslab.h>

#define SLPC_SLAB_CREATE(name) kut_slab_create (SLPC_STRING_NAME "_" name, \
                                                sizeof (SLPC_NODE),        \
                                                KUT_CTOR_ONCE,             \
					        SLPC_NODE_CTOR,            \
					        SLPC_NULL)

#define SLPC_SLAB_DESTROY(slab) kut_slab_destroy (slab)

#define SLPC_SLAB      KUT_SLAB
#define SLPC_SLAB_NEW  kut_slab_new
#define SLPC_SLAB_FREE kut_slab_free

#else /* ! SLPC_USE_KUTSLAB */

/* This free-list implementation is cheaper than KUTSLAB because it
 * uses the node's right link for the free list, which you might say
 * is a data abstraction violation.  */

typedef struct SLPC_PARAM(_SLPC_PAGE_)        SLPC_PARAM(SLPC_PAGE_);
typedef struct SLPC_PARAM(_SLPC_PLNK_)        SLPC_PARAM(SLPC_PLNK_);
typedef struct SLPC_PARAM(_SLPC_SLAB_)        SLPC_PARAM(SLPC_SLAB_);

#undef  SLPC_PAGE
#define SLPC_PAGE                SLPC_PARAM(SLPC_PAGE_)
#undef  SLPC_PLNK
#define SLPC_PLNK                SLPC_PARAM(SLPC_PLNK_)
#undef  SLPC_SLAB
#define SLPC_SLAB                SLPC_PARAM(SLPC_SLAB_)

#ifndef SLPC_ALLOCATION_SIZE /* Default allocates 16k at a time. */
#define SLPC_ALLOCATION_SIZE (1 << 14)
#endif

#undef  SLPC_NODES_PER_PAGE
#define SLPC_NODES_PER_PAGE ((SLPC_ALLOCATION_SIZE/sizeof(SLPC_NODE))-1)

#ifndef SLPC_MALLOC
#define SLPC_MALLOC(sz) malloc(sz)
#define SLPC_FREE(ptr)  free(ptr)
#endif

#define SLPC_SLAB_CREATE(name)   SLPC_PARAM(slpc_slab_create_) ()
#define SLPC_SLAB_DESTROY        SLPC_PARAM(slpc_slab_destroy_)
#define SLPC_SLAB_NEW            SLPC_PARAM(slpc_slab_new_)
#define SLPC_SLAB_FREE           SLPC_PARAM(slpc_slab_free_)

#undef  SLPC_SLAB_ALLOC
#define SLPC_SLAB_ALLOC          SLPC_PARAM(slpc_slab_alloc_)
#undef  SLPC_SLAB_PREALLOC
#define SLPC_SLAB_PREALLOC       SLPC_PARAM(slpc_slab_prealloc_)

#endif

/* Forward structure declarations/macros:
 */
typedef struct SLPC_PARAM(_SLPC_ANCHOR_)      SLPC_PARAM(SLPC_ANCHOR_);
typedef struct SLPC_PARAM(_SLPC_PLACE_CTX_)   SLPC_PARAM(SLPC_PLACE_CTX_);
typedef struct SLPC_PARAM(_SLPC_NODE_CTX_)    SLPC_PARAM(SLPC_NODE_CTX_);
typedef struct SLPC_PARAM(_SLPC_NODE_)        SLPC_PARAM(SLPC_NODE_);
typedef struct SLPC_PARAM(_SLPC_SHORT_NODE_)  SLPC_PARAM(SLPC_SHORT_NODE_);
typedef struct SLPC_PARAM(_SLPC_PAIR_)        SLPC_PARAM(SLPC_PAIR_);
typedef union  SLPC_PARAM(_SLPC_UNION_)       SLPC_PARAM(SLPC_UNION_);

/* These definitions hide the parametrization of every type name.
 */
#undef  SLPC_ANCHOR
#define SLPC_ANCHOR              SLPC_PARAM(SLPC_ANCHOR_)
#undef  SLPC_PLACE_CTX
#define SLPC_PLACE_CTX           SLPC_PARAM(SLPC_PLACE_CTX_)
#undef  SLPC_NODE_CTX
#define SLPC_NODE_CTX            SLPC_PARAM(SLPC_NODE_CTX_)
#undef  SLPC_NODE
#define SLPC_NODE                SLPC_PARAM(SLPC_NODE_)
#undef  SLPC_SHORT_NODE
#define SLPC_SHORT_NODE          SLPC_PARAM(SLPC_SHORT_NODE_)
#undef  SLPC_PAIR
#define SLPC_PAIR                SLPC_PARAM(SLPC_PAIR_)
#undef  SLPC_UNION
#define SLPC_UNION               SLPC_PARAM(SLPC_UNION_)

/* These definitions hide the parametrization of every function name.
 */
#undef  SLPC_INSERT
#define SLPC_INSERT              SLPC_PARAM(slpc_insert_)
#undef  SLPC_DELETE_KEY
#define SLPC_DELETE_KEY          SLPC_PARAM(slpc_delete_key_)
#undef  SLPC_DELETE_MIN
#define SLPC_DELETE_MIN          SLPC_PARAM(slpc_delete_min_)
#undef  SLPC_DELETE_INT
#define SLPC_DELETE_INT          SLPC_PARAM(slpc_delete_int_)
#undef  SLPC_SEARCH
#define SLPC_SEARCH              SLPC_PARAM(slpc_search_)
#undef  SLPC_SEARCH_LUB
#define SLPC_SEARCH_LUB          SLPC_PARAM(slpc_search_lub_)
#undef  SLPC_SEARCH_MIN
#define SLPC_SEARCH_MIN          SLPC_PARAM(slpc_search_min_)

/* Enumerations
 */
enum _SLPC_TYPE
{
  SLPC_TYPE_LEAF     = 0,
  SLPC_TYPE_INTERNAL = 1,
  SLPC_TYPE_PIVOT    = 2,
  SLPC_TYPE_FREE     = 3,
};

enum _SLPC_RESULT
{
  SLPC_OKAY          = 0,
  SLPC_TRY_AGAIN     = 1,
  SLPC_KEY_EXISTS    = 2,
  SLPC_KEY_NOTFOUND  = 3,
};

enum _SLPC_LOCKMODE
{
  SLPC_NOLOCK        = 0,
  SLPC_SHAREDLOCK    = 1,
  SLPC_EXCLLOCK      = 2,
};

typedef enum _SLPC_TYPE     SLPC_TYPE;
typedef enum _SLPC_RESULT   SLPC_RESULT;
typedef enum _SLPC_LOCKMODE SLPC_LOCKMODE;

/*********************************************************************
			       DATA TYPES
 *********************************************************************/

/* The SLPC data type union: this pads node entries (leaf and
 * internal) to the same size.
 */
union SLPC_PARAM(_SLPC_UNION_) {
  SLPC_NODE  *_d_down;
  SLPC_DATA   _d_data;
};

/* The SLPC pair type: this holds one key and one datum (down pointer or leaf value)
 */
struct SLPC_PARAM(_SLPC_PAIR_) {
  SLPC_KEY    _key;
  SLPC_UNION  _dat;
};

/* The SLPC node:
 */
struct SLPC_PARAM(_SLPC_NODE_) {

  SLPC_SHORT_T  _count; // Number of valid pairs
  SLPC_SHORT_T  _type;  // Type of node
  SLPC_NODE    *_right; // The right neighbor or SLPC_NULL

#if SLPC_USE_SPINLOCK
  volatile
  SLPC_SHORT_T  _cycle;     // A sequence number to detect races during lock upgrade
  SLPC_SHORT_T  _magic;     // Unused field being used for DEBUG
  SLPC_SPINLOCK _node_lock; // The spinlock used to protect this node
#endif

  SLPC_PAIR     _pairs[SLPC_MAX_COUNT]; // The payload section

#if SLPC_PAD_COUNT > 0
  SLPC_PAD_T    _pad  [SLPC_PAD_COUNT]; // Pad the node to the cache-line size, it helps!
#endif
};

/* The SLPC short node: identical to the node but saves a little space
 * in the anchor, since the pivot node only ever contains a single pair.
 */
struct SLPC_PARAM(_SLPC_SHORT_NODE_) {

  SLPC_SHORT_T  _count; // Number of valid pairs
  SLPC_SHORT_T  _type;  // Type of node
  SLPC_NODE    *_right; // The right neighbor or SLPC_NULL

#if SLPC_USE_SPINLOCK
  volatile
  SLPC_SHORT_T  _cycle;     // A sequence number to detect races during lock upgrade
  SLPC_SHORT_T  _magic;     // Unused field being used for DEBUG
  SLPC_SPINLOCK _node_lock; // The spinlock used to protect this node
#endif

  SLPC_PAIR     _pairs[1];  // The payload section
};

#if SLPC_USE_KUTSLAB == 0
/* The SLPC page link: a list of allocations, aligned because it
 * precedes the nodes in a SLPC_PAGE.
 */
struct SLPC_PARAM(_SLPC_PLNK_)
{
  SLPC_PAGE  *_next; /* Next in the list of pages. */
} SLPC_TARGETSIZE_ALIGNED;

/* The SLPC page: used for allocation of a group of nodes
 */
struct SLPC_PARAM(_SLPC_PAGE_)
{
  SLPC_PLNK  _link; /* Aligned container for next page pointer. */
  SLPC_NODE  _array[SLPC_NODES_PER_PAGE];
};

/* The SLPC slab: manages allocation for one or more SLPCs.
 */
struct SLPC_PARAM(_SLPC_SLAB_)
{
  SLPC_NODE     *_free;
  SLPC_PAGE     *_pages;

#if SLPC_USE_SPINLOCK
  SLPC_SPINLOCK  _lock;
#endif
};
#endif

/* The SLPC node context: dynamic state variable combines a node
 * pointer and information about its present lock state.
 */
struct SLPC_PARAM(_SLPC_NODE_CTX_) {
  SLPC_NODE     *_node;
#if SLPC_USE_SPINLOCK
  SLPC_LOCKMODE  _lockmode;
#endif
};

/* The SLPC place context: combines parent and child node contexts as
 * well as the "other" context used in joining three nodes.
 */
struct SLPC_PARAM(_SLPC_PLACE_CTX_) {
  SLPC_NODE_CTX _place;
  SLPC_NODE_CTX _child;
  SLPC_NODE_CTX _other;
};

/* The SLPC anchor:
 */
struct SLPC_PARAM(_SLPC_ANCHOR_) {

  SLPC_SIZE_T      _height;
  SLPC_SHORT_NODE  _pivot_node;

  /* Left leaf is only used by search_min--could conditionally get rid of it--bit of a
   * space waste here. */
  SLPC_NODE    *_left_leaf  SLPC_ANCHORFIELD_ALIGNED;
  SLPC_SLAB    *_slab       SLPC_ANCHORFIELD_ALIGNED;

#if SLPC_COUNT_KEYS
  struct {
    SLPC_SIZE_T   _key_count;
#if SLPC_USE_SPINLOCK
    SLPC_SPINLOCK _key_count_lock;
#endif
  } _ks SLPC_ANCHORFIELD_ALIGNED;
#endif

#if SLPC_COUNT_RESTARTS
  struct {
    SLPC_SIZE_T   _restarts;
#if SLPC_USE_SPINLOCK
    SLPC_SPINLOCK _restart_count_lock;
#endif
  } _rs SLPC_ANCHORFIELD_ALIGNED;
#endif

#if SLPC_COUNT_NODES
  struct {
    SLPC_SIZE_T   _node_count;
#if SLPC_USE_SPINLOCK
    SLPC_SPINLOCK _node_count_lock;
#endif
  } _ns SLPC_ANCHORFIELD_ALIGNED;
#endif
};

/* Now include some helper functions
 */
#include "slph.h"

/*********************************************************************
			 SKIP LIST INSERTION
 *********************************************************************/

static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_insert_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  SLPC_PLACE_CTX ctx;
  int            depth;
  int            height;
  int            key_i;
  SLPC_RESULT    result;

 start_over:

  // Initial condition: holding shared lock on the pivot
  SLPC_CTX_INIT (&ctx, anchor);

  // With pivot held, read the height: this is how we know we've
  // reached a leaf
  height = anchor->_height;
  depth  = 0;

  // Iterative descent: each search locates a child--the next
  // place--and increases the depth
  for (; height > 0; depth += 1, height -= 1)
    {
      // Keys are not checked at depth 0 (pivot), and the last key is
      // never checked.  Therefore, effective count limits the number
      // of tests to COUNT-1
      int effective_count = (PLACE_NODE->_count-1);

      SLPC_ASSERT (PLACE_NODE->_type != SLPC_TYPE_LEAF);

      // Advance key_i until finding a key that is greater than the
      // search key: a linear search
      for (key_i = 0; key_i < effective_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
	{
	  // Do nothing: just setting key_i
	}

      // Now we know the next child.  If height == 1, then the next
      // child is a leaf so we should get an exclusive lock.  If the
      // next node is internal, be optimistic and just get a shared
      // lock.
      SLPC_CTX_LOCK_CHILD (& ctx, SLPC_ITH_DOWN (PLACE_NODE, key_i), (height == 1));

      // Now check if the child needs to split, this is the top-down
      // pre-condition
      if (CHILD_NODE->_count == SLPC_MAX_COUNT)
	{
	  SLPC_NODE *new_child;

	  // Need exclusive locks
	  if (SLPC_UPGRADE_LOCKS (& ctx) == SLPC_TRY_AGAIN)
	    {
	      SLPC_RESTART_OPERATION ();
	    }

	  // Now both place and child are exclusive: split the child
	  new_child = SLPC_SPLIT_NODE (anchor, CHILD_NODE);

	  // The pivotal case
	  if (depth == 0)
	    {
	      // Root split at the pivot:
	      SLPC_NODE *new_root;

	      SLPC_ASSERT (PLACE_NODE == SLPC_PIVOT (anchor));

	      new_root = SLPC_NODE_NEW (anchor);
	      SLPC_NODE_INIT_ROOT (new_root, CHILD_NODE, new_child);

	      // Update the pivot, still holding its lock
	      SLPC_ITH_DOWN (SLPC_PIVOT (anchor), 0) = new_root;

	      // Lock the root before switching
	      SLPC_SPINLOCK_GET_EXCL (new_root->_node_lock);
	      PLACE_NODE = new_root;

	      // The height is increased, but not for this process's
	      // traversal, since we update place to the new root.
	      // The pivot lock protects this variable.
	      anchor->_height += 1;

	      // Release the pivot lock, pivot is consistent but root
	      // lock still held exclusive
	      SLPC_SPINLOCK_PUT_EXCL (SLPC_PIVOT (anchor)->_node_lock);
	    }
	  else
	    {
	      // Non-root split: the top-down invariant is that there
	      // is at least one slot in PLACE to insert a key.
	      SLPC_ASSERT (PLACE_NODE->_count < SLPC_MAX_COUNT);

	      // Shift keys, making room for the new node at the position of key_i
	      SLPC_SHIFT_KEYS_RIGHT (PLACE_NODE, key_i);

	      // Insert the new node.  The old key at position key_i moves
	      // right, and the new key is the last key of the left-split
	      // node, which is the original child.
	      SLPC_ITH_KEY(PLACE_NODE, key_i)    = SLPC_ITH_KEY(CHILD_NODE,CHILD_NODE->_count - 1);
	      SLPC_ITH_DOWN(PLACE_NODE, key_i)   = CHILD_NODE;
	      SLPC_ITH_DOWN(PLACE_NODE, key_i+1) = new_child;
	    }

	  // If the search key resides in the right-split node,
	  // correct the child variable to new_child, and carry the
	  // lock
	  if (SLPC_GREATER_THAN(*key, SLPC_ITH_KEY(PLACE_NODE,key_i)))
	    {
	      SLPC_CTX_GIVE (& ctx._child);
	      SLPC_CTX_LOCK_CHILD (& ctx, new_child, (height == 1));
	    }
	  // See if the child lock can be downgraded from exclusive to
	  // shared for internal nodes
	  else if (height > 1)
	    {
	      SLPC_CTX_GIVE (& ctx._child);
	      SLPC_CTX_LOCK_CHILD (& ctx, CHILD_NODE, (height == 1));
	    }
	}

      // Now release the place lock, already have a lock on the child
      SLPC_CTX_DESCEND (& ctx);
    }

  // The leaf case: this statement is similar to the internal case,
  // except all keys are checked including the last one
  SLPC_ASSERT (PLACE_NODE->_type == SLPC_TYPE_LEAF);

  for (key_i = 0; key_i < PLACE_NODE->_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
    {
      // Just advancing key_i
    }

  if (key_i < PLACE_NODE->_count && SLPC_EQUAL_TO(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)))
    {
      // The key already exists: might want to overwrite...
      result = SLPC_KEY_EXISTS;
    }
  else
    {
      // Insert the new leaf pair
      SLPC_SHIFT_KEYS_RIGHT (PLACE_NODE, key_i);

      SLPC_ITH_KEY(PLACE_NODE, key_i) = *key;
      SLPC_ITH_VALUE(PLACE_NODE, key_i) = *data;

      // Update the anchor's key_count variable
#if SLPC_COUNT_KEYS
      SLPC_SPINLOCK_GET_EXCL (anchor->_ks._key_count_lock);
      anchor->_ks._key_count += 1;
      SLPC_SPINLOCK_PUT_EXCL (anchor->_ks._key_count_lock);
#endif

      SLPC_INCREMENT_CYCLE (PLACE_NODE);

      result = SLPC_OKAY;
    }

  // Release the place lock
  SLPC_CTX_GIVE (& ctx._place);
  return result;
}

/*********************************************************************
			   SKIP LIST SEARCH
 *********************************************************************/

static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_search_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  SLPC_PLACE_CTX ctx;
  int            depth;
  int            height;
  int            key_i;
  SLPC_RESULT    result;

  // Initial condition: holding shared lock on the pivot
  SLPC_CTX_INIT (&ctx, anchor);

  // With pivot held, read the height: this is how we know we've
  // reached a leaf
  height = anchor->_height;
  depth  = 0;

  // Iterative descent: each search locates a child--the next
  // place--and increases the depth
  for (; height > 0; depth += 1, height -= 1)
    {
      // Keys are not checked at depth 0 (pivot), and the last key is
      // never checked: effective count limits the number of tests to
      // COUNT-1
      int effective_count = (PLACE_NODE->_count-1);

      SLPC_ASSERT (PLACE_NODE->_type != SLPC_TYPE_LEAF);

      // Advance key_i until finding a key that is greater than the
      // search key: a linear search
      for (key_i = 0; key_i < effective_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
	{
	  // Do nothing: just setting key_i
	}

      // Now we know the next child, get a shared lock.
      SLPC_CTX_LOCK_CHILD (& ctx, SLPC_ITH_DOWN (PLACE_NODE, key_i), 0);

      // Now release the place lock, already have a lock on the child
      SLPC_CTX_DESCEND (& ctx);
    }

  // The leaf case: this statement is similar to the internal case,
  // except all keys are checked including the last one
  SLPC_ASSERT (PLACE_NODE->_type == SLPC_TYPE_LEAF);

  for (key_i = 0; key_i < PLACE_NODE->_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
    {
      // Just advancing key_i
    }

  if (key_i < PLACE_NODE->_count && SLPC_EQUAL_TO(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)))
    {
      // The key already exists:
      (*data) = SLPC_ITH_VALUE (PLACE_NODE, key_i);
      result  = SLPC_OKAY;
    }
  else
    {
      result = SLPC_KEY_NOTFOUND;
    }

  // Release the place lock
  SLPC_CTX_GIVE (& ctx._place);
  return result;
}

/* Searches for the least upper bounding key of the interval
 * [-Inf,Key), the lowest key greater than the search key.  */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_search_lub_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  SLPC_PLACE_CTX ctx;
  int            depth;
  int            height;
  int            key_i;
  SLPC_RESULT    result;

  SLPC_CTX_INIT (&ctx, anchor);

  height = anchor->_height;
  depth  = 0;

  /* The code is nearly identical to the above so I've only commented
   * the differences. */
  for (; height > 0; depth += 1, height -= 1)
    {
      int effective_count = (PLACE_NODE->_count-1);

      SLPC_ASSERT (PLACE_NODE->_type != SLPC_TYPE_LEAF);

      for (key_i = 0; key_i < effective_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
	{
	}

      SLPC_CTX_LOCK_CHILD (& ctx, SLPC_ITH_DOWN (PLACE_NODE, key_i), 0);
      SLPC_CTX_DESCEND (& ctx);
    }

  SLPC_ASSERT (PLACE_NODE->_type == SLPC_TYPE_LEAF);

  /* Here we test GREATER_EQUAL instead of GREATER_THAN so that we
   * advance past the search key. */
  for (key_i = 0; key_i < PLACE_NODE->_count && SLPC_GREATER_EQUAL(*key, SLPC_ITH_KEY (PLACE_NODE, key_i)); key_i += 1)
    {
    }

  /* The key is <= key_i in this node or else key_i ==
   * PLACE_NODE->_count.  If the second is true we need a link
   * chase. */
  if (key_i < PLACE_NODE->_count)
    {
      (*key)  = SLPC_ITH_KEY   (PLACE_NODE, key_i);
      (*data) = SLPC_ITH_VALUE (PLACE_NODE, key_i);
      result  = SLPC_OKAY;
    }
  else if (PLACE_NODE->_right == NULL)
    {
      /* The last key has no LUB. */
      result = SLPC_KEY_NOTFOUND;
    }
  else
    {
      /* Read-lock the right neighbor, unlock PLACE.  The second call
       * to SLPC_CTX_GIVE prior to return has no effect. */
      SLPC_CTX_LOCK_OTHER (& ctx, PLACE_NODE->_right, 0);
      SLPC_CTX_GIVE       (& ctx._place);

      (*key)  = SLPC_ITH_KEY   (OTHER_NODE, 0);
      (*data) = SLPC_ITH_VALUE (OTHER_NODE, 0);
      result  = SLPC_OKAY;

      SLPC_CTX_GIVE (& ctx._other);
    }

  SLPC_CTX_GIVE (& ctx._place);
  return result;
}

static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_search_min_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  SLPC_RESULT  res;
  SLPC_NODE   *node = anchor->_left_leaf;

  SLPC_SPINLOCK_GET_SHARED (node->_node_lock);

  if (node->_count == 0)
    {
      res = SLPC_KEY_NOTFOUND;
    }
  else
    {
      (*key)  = SLPC_ITH_KEY   (node, 0);
      (*data) = SLPC_ITH_VALUE (node, 0);

      res = SLPC_OKAY;
    }

  SLPC_SPINLOCK_PUT_SHARED (node->_node_lock);

  return res;
}

/*********************************************************************
			   SKIP LIST DELETION
 *********************************************************************/

/* The internal version of deletion: does the work of both delete-key
 * and delete-min.  Delete-min effectively disables the comparison
 * function and always chooses the first entry.
 */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_delete_int_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data, const int take_min_key)
{
  SLPC_PLACE_CTX ctx;
  int            depth;
  int            height;
  int            key_i;
  SLPC_RESULT    result;
  SLPC_KEY       high_key; /* High_key cannot be used in the first loop iteration (! is_pivot) */

 start_over:

  // Initial condition: holding shared lock on the pivot
  SLPC_CTX_INIT (&ctx, anchor);

  // With pivot held, read the height: this is how we know we've
  // reached a leaf
  height = anchor->_height;
  depth  = 0;

  // Iterative descent: each search locates a child--the next
  // place--and increases the depth
  for (; height > 0; depth += 1, height -= 1)
    {
      // Keys are not checked at depth 0 (pivot), and the last key is
      // never checked.  Therefore, effective count limits the number
      // of tests to COUNT-1
      int effective_count     = (PLACE_NODE->_count-1);
      int is_pivot            = (depth == 0);

      SLPC_ASSERT (PLACE_NODE->_type != SLPC_TYPE_LEAF);

      // Advance key_i until finding a key that is greater than the
      // search key: a linear search
      for (key_i = 0; !take_min_key && (key_i < effective_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i))); key_i += 1)
	{
	  // Do nothing: just setting key_i
	}

      // Now we know the next child.  If height == 1, then the next
      // child is a leaf so we should get an exclusive lock.  If the
      // next node is internal, be optimistic and just get a shared
      // lock.
      SLPC_CTX_LOCK_CHILD (& ctx, SLPC_ITH_DOWN (PLACE_NODE, key_i), (height == 1));

      // Check if the root is an internal node with only one child
      if (is_pivot)
	{
	  if (CHILD_NODE->_type == SLPC_TYPE_INTERNAL && CHILD_NODE->_count == 1)
	    {
	      // Need an exclusive lock on the pivot and root
	      if (SLPC_UPGRADE_LOCKS (& ctx) == SLPC_TRY_AGAIN)
		{
		  SLPC_RESTART_OPERATION ();
		}

	      // Decrease the height by one, including this traversal
	      // because the former root node will be freed
	      SLPC_ASSERT (PLACE_NODE == SLPC_PIVOT (anchor) && key_i == 0 && height > 1);
	      SLPC_ITH_DOWN (SLPC_PIVOT (anchor), 0) = SLPC_ITH_DOWN (CHILD_NODE, 0);
	      anchor->_height -= 1;
	      height          -= 1;

	      // Release the former root lock, free the node
	      SLPC_NODE_FREE (anchor, CHILD_NODE, & ctx._child);

	      // Get back a lock on the new child
	      SLPC_CTX_LOCK_CHILD (& ctx, SLPC_ITH_DOWN (SLPC_PIVOT(anchor), 0), (height == 1));
	    }
	}

      // Non-root case: check if the child needs to borrow or join,
      // this is the top-down pre-condition
      else
	{
	  if (CHILD_NODE->_count == SLPC_MIN_COUNT)
	    {
	      // The child needs to gain at least one entry before
	      // descending!  This time, however, we need three locks
	      // to proceed: place, left-child, and right-child.  The
	      // simplest case is to aquire key_i and key_i+1, meaning
	      // to lock the child and its right neighbor, except for
	      // the last element (key_i == PLACE_NODE->_count-1),
	      // which must use its left neighbor.  Either way around,
	      // the locking protocol must aquire exclusive locks on
	      // the children in ascending order.  UPGRADE_3LOCKS
	      // performs this task and sets the OTHER_NODE context to
	      // the third node.  The [0..count-2] case is treated
	      // separately from the [count-1] case:
	      int aquire_left = (key_i < PLACE_NODE->_count-1) ? key_i : (key_i - 1);

	      if (SLPC_UPGRADE_3LOCKS (& ctx, aquire_left) == SLPC_TRY_AGAIN)
		{
		  SLPC_RESTART_OPERATION ();
		}

	      if (key_i < PLACE_NODE->_count-1)
		{
		  // Using the child and its right neighbor
		  SLPC_NODE *rchild = OTHER_NODE;

		  SLPC_ASSERT (OTHER_NODE == SLPC_ITH_DOWN (PLACE_NODE, key_i+1));

		  if (rchild->_count == SLPC_MIN_COUNT)
		    {
		      // Both children are at minimum capacity, join
		      // them, freeing the right child and releasing
		      // its lock
		      SLPC_JOIN_NODES (anchor, CHILD_NODE, rchild, & ctx._other);

		      // Eliminate the right child key
		      SLPC_SHIFT_KEYS_LEFT (PLACE_NODE, key_i+1);
		    }
		  else
		    {
		      // Don't need to join nodes: just borrow from
		      // the right neighbor
		      SLPC_REDIST_RIGHT_LEFT (CHILD_NODE, rchild);

		      // Release the right child lock.
		      SLPC_CTX_GIVE (& ctx._other);
		    }

		  // In both cases, the child (on the left) has gained
		  // at least one new maximum entry, so update the
		  // corresponding key in PLACE_NODE accordingly.  If
		  // the last key is being updated we have to carry
		  // the high key from above: we don't know it
		  // locally.
		  if (key_i < PLACE_NODE->_count - 1)
		    {
		      SLPC_ITH_KEY (PLACE_NODE,key_i) = SLPC_ITH_KEY (CHILD_NODE, CHILD_NODE->_count - 1);
		    }
		  else
		    {
		      SLPC_ITH_KEY (PLACE_NODE,key_i) = high_key;
		    }

		  // Key_i is unchanged: the right joined the left child
		}
	      else
		{
		  // Using the left neighbor and the child
		  SLPC_NODE *lchild = OTHER_NODE;

		  int key_i_1 = key_i - 1;

		  SLPC_ASSERT (OTHER_NODE == SLPC_ITH_DOWN (PLACE_NODE, key_i_1));

		  if (lchild->_count == SLPC_MIN_COUNT)
		    {
		      // Both children are at minimum capacity, join
		      // them, freeing the child node
		      SLPC_JOIN_NODES (anchor, lchild, CHILD_NODE, & ctx._child);

		      // Shift keys: this moves the new child's max
		      // key over the (old) left child's key
		      SLPC_SHIFT_KEYS_LEFT (PLACE_NODE, key_i_1);

		      // The left child pointer was overwritten by the
		      // previous statement, when keys were shifted.
		      // Now update the correct left child pointer.
		      SLPC_ITH_DOWN (PLACE_NODE,key_i_1) = lchild;

		      // Switch lchild to child:
		      SLPC_CTX_CHILD_OTHER (& ctx);
		    }
		  else
		    {
		      // Don't need to join, just borrow
		      SLPC_REDIST_LEFT_RIGHT (lchild, CHILD_NODE);

		      // Update the left child's key in place, right
		      // child's key is unchanged
		      SLPC_ITH_KEY(PLACE_NODE,key_i_1) = SLPC_ITH_KEY(lchild, lchild->_count-1);

		      // Release the left child lock
		      SLPC_CTX_GIVE (& ctx._other);
		    }
		}

	      // See if the child lock can be downgraded from exclusive to
	      // shared for internal nodes
	      if (height > 1)
		{
		  SLPC_CTX_GIVE (& ctx._child);
		  SLPC_CTX_LOCK_CHILD (& ctx, CHILD_NODE, (height == 1));
		}
	    }
	}

      // Save the high key
      high_key = SLPC_ITH_KEY (PLACE_NODE, key_i);

      // Now have the proper child lock, release the place lock,
      // transfer place to the child
      SLPC_CTX_DESCEND (& ctx);
    }

  // The leaf case: this statement is similar to the internal case,
  // except all keys are checked including the last one
  SLPC_ASSERT (PLACE_NODE->_type == SLPC_TYPE_LEAF);

  for (key_i = 0; !take_min_key && (key_i < PLACE_NODE->_count && SLPC_GREATER_THAN(*key, SLPC_ITH_KEY (PLACE_NODE, key_i))); key_i += 1)
    {
      // Just advancing key_i
    }

  // If key_i is valid and (delete-min or exact-match)
  if (key_i < PLACE_NODE->_count && (take_min_key || SLPC_EQUAL_TO(*key, SLPC_ITH_KEY (PLACE_NODE, key_i))))
    {
      // The key exists:
      if (take_min_key && key != SLPC_NULL)
	{
	  (*key) = SLPC_ITH_KEY (PLACE_NODE, key_i);
	}

      if (data != SLPC_NULL)
	{
	  (*data) = SLPC_ITH_VALUE (PLACE_NODE, key_i);
	}

      // Shift keys, overwrite the existing entry
      SLPC_SHIFT_KEYS_LEFT (PLACE_NODE, key_i);

      SLPC_INCREMENT_CYCLE (PLACE_NODE);

      // Update the anchor's key_count variable
#if SLPC_COUNT_KEYS
      SLPC_SPINLOCK_GET_EXCL (anchor->_ks._key_count_lock);
      anchor->_ks._key_count -= 1;
      SLPC_SPINLOCK_PUT_EXCL (anchor->_ks._key_count_lock);
#endif
      result = SLPC_OKAY;
    }
  else
    {
      result = SLPC_KEY_NOTFOUND;
    }

  // Release the place lock
  SLPC_CTX_GIVE (& ctx._place);
  return result;
}

/* To delete a specific key
 */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_delete_key_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  SLPC_ASSERT (key != SLPC_NULL);
  return SLPC_DELETE_INT (anchor, key, data, 0);
}

/* To delete the minimum key
 */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_delete_min_) (SLPC_ANCHOR *anchor, SLPC_KEY *key, SLPC_DATA *data)
{
  return SLPC_DELETE_INT (anchor, key, data, 1);
}

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
