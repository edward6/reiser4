/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* Macros used to check lock state
 */
#if SLPC_USE_SPINLOCK
#define SLPC_IS_LOCKED(x) ((x)._lockmode != SLPC_NOLOCK)
#define SLPC_IS_EXCL(x)   ((x)._lockmode == SLPC_EXCLLOCK)
#define SLPC_IS_SHARED(x) ((x)._lockmode == SLPC_SHAREDLOCK)
#endif

/* Parametrized symbols for the functions defined in this file.
 */
#undef  SLPC_UPGRADE_LOCKS
#define SLPC_UPGRADE_LOCKS       SLPC_PARAM(slpc_upgrade_locks_)
#undef  SLPC_UPGRADE_3LOCKS
#define SLPC_UPGRADE_3LOCKS      SLPC_PARAM(slpc_upgrade_3locks_)

#undef  SLPC_CTX_LOCK_CHILD
#define SLPC_CTX_LOCK_CHILD      SLPC_PARAM(slpc_ctx_lock_child_)
#undef  SLPC_CTX_LOCK_OTHER
#define SLPC_CTX_LOCK_OTHER      SLPC_PARAM(slpc_ctx_lock_other_)
#undef  SLPC_CTX_CHILD_OTHER
#define SLPC_CTX_CHILD_OTHER     SLPC_PARAM(slpc_ctx_child_other_)

#if SLPC_USE_SPINLOCK
#undef  SLPC_CTX_GIVE
#define SLPC_CTX_GIVE            SLPC_PARAM(slpc_ctx_give_)
#undef  SLPC_CTX_EXCL
#define SLPC_CTX_EXCL            SLPC_PARAM(slpc_ctx_excl_)
#undef  SLPC_CTX_SHARED
#define SLPC_CTX_SHARED          SLPC_PARAM(slpc_ctx_shared_)
#undef  SLPC_CTX_RESET
#define SLPC_CTX_RESET           SLPC_PARAM(slpc_ctx_reset_)
#else
#undef  SLPC_CTX_GIVE
#define SLPC_CTX_GIVE(x)         (void)0
#undef  SLPC_CTX_EXCL
#define SLPC_CTX_EXCL(x)         (void)0
#undef  SLPC_CTX_SHARED
#define SLPC_CTX_SHARED(x)       (void)0
#undef  SLPC_CTX_RESET
#define SLPC_CTX_RESET(x)        (void)0
#endif

#undef  SLPC_CTX_INIT
#define SLPC_CTX_INIT            SLPC_PARAM(slpc_ctx_init_)
#undef  SLPC_CTX_DESCEND
#define SLPC_CTX_DESCEND         SLPC_PARAM(slpc_ctx_descend_)
#undef  SLPC_NODE_NEW
#define SLPC_NODE_NEW            SLPC_PARAM(slpc_node_new_)
#undef  SLPC_NODE_FREE
#define SLPC_NODE_FREE           SLPC_PARAM(slpc_node_free_)
#undef  SLPC_SPLIT_NODE
#define SLPC_SPLIT_NODE          SLPC_PARAM(slpc_split_node_)
#undef  SLPC_JOIN_NODES
#define SLPC_JOIN_NODES          SLPC_PARAM(slpc_join_nodes_)
#undef  SLPC_SHIFT_KEYS_RIGHT
#define SLPC_SHIFT_KEYS_RIGHT    SLPC_PARAM(slpc_shift_keys_right_)
#undef  SLPC_SHIFT_KEYS_LEFT
#define SLPC_SHIFT_KEYS_LEFT     SLPC_PARAM(slpc_shift_keys_left_)
#undef  SLPC_REDIST_RIGHT_LEFT
#define SLPC_REDIST_RIGHT_LEFT   SLPC_PARAM(slpc_redist_right_left_)
#undef  SLPC_REDIST_LEFT_RIGHT
#define SLPC_REDIST_LEFT_RIGHT   SLPC_PARAM(slpc_redist_left_right_)
#undef  SLPC_NODE_INIT_PIVOT
#define SLPC_NODE_INIT_PIVOT     SLPC_PARAM(slpc_node_init_pivot_)
#undef  SLPC_NODE_INIT_LEAF
#define SLPC_NODE_INIT_LEAF      SLPC_PARAM(slpc_node_init_leaf_)
#undef  SLPC_NODE_INIT_ROOT
#define SLPC_NODE_INIT_ROOT      SLPC_PARAM(slpc_node_init_root_)
#undef  SLPC_NODE_CTOR
#define SLPC_NODE_CTOR           SLPC_PARAM(slpc_node_ctor_)

#undef  SLPC_DEBUG_STRUCTURE
#define SLPC_DEBUG_STRUCTURE     SLPC_PARAM(slpc_debug_structure_)

#undef  SLPC_CEIL
#define SLPC_CEIL(x,y) (((x)+(y)-1)/(y))

/* The STATIC_ASSERT idea is due to Andy Chou <acc@CS.Stanford.EDU>. */
#define STATIC_ASSERT(cond) ({ switch( -1 ) { case ( cond ): case 0: } })

/*********************************************************************
		       LOCK MAINTENENCE METHODS
 *********************************************************************/

#if SLPC_USE_SPINLOCK
/* To give the context lock:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_give_) (SLPC_NODE_CTX *nodectx)
{
  SLPC_ASSERT (SLPC_IS_LOCKED (*nodectx));
  SLPC_SPINLOCK_PUT_LOCK (SLPC_IS_EXCL (*nodectx), nodectx->_node->_node_lock);
  nodectx->_lockmode = SLPC_NOLOCK;
}

/* To get the context lock exclusive:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_excl_) (SLPC_NODE_CTX *nodectx)
{
  SLPC_ASSERT (! SLPC_IS_LOCKED (*nodectx));
  SLPC_SPINLOCK_GET_EXCL (nodectx->_node->_node_lock);
  nodectx->_lockmode = SLPC_EXCLLOCK;
}

/* To get the context lock shared:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_shared_) (SLPC_NODE_CTX *nodectx)
{
  SLPC_ASSERT (! SLPC_IS_LOCKED (*nodectx));
  SLPC_SPINLOCK_GET_SHARED (nodectx->_node->_node_lock);
  nodectx->_lockmode = SLPC_SHAREDLOCK;
}

/* To reset the context:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_reset_) (SLPC_PLACE_CTX *ctx)
{
  if (SLPC_IS_LOCKED (ctx->_place)) {
    SLPC_CTX_GIVE (&ctx->_place);
  }

  if (SLPC_IS_LOCKED (ctx->_child)) {
    SLPC_CTX_GIVE (&ctx->_child);
  }

  if (SLPC_IS_LOCKED (ctx->_other)) {
    SLPC_CTX_GIVE (&ctx->_other);
  }
}
#endif

/* To init the context:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_init_) (SLPC_PLACE_CTX *ctx, SLPC_ANCHOR *anchor)
{
  ctx->_place._node           = SLPC_PIVOT (anchor);
  ctx->_child._node           = SLPC_NULL;
  ctx->_other._node           = SLPC_NULL;

#if SLPC_USE_SPINLOCK
  ctx->_place._lockmode       = SLPC_NOLOCK;
  ctx->_child._lockmode       = SLPC_NOLOCK;
  ctx->_other._lockmode       = SLPC_NOLOCK;
#endif

  SLPC_CTX_SHARED (& ctx->_place);
}

/* To get the child context lock, maybe exclusive:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_lock_child_) (SLPC_PLACE_CTX *ctx, SLPC_NODE *child, int exclusive)
{
  ctx->_child._node = child;

#if SLPC_USE_SPINLOCK
  SLPC_ASSERT (! SLPC_IS_LOCKED (ctx->_child));
  ctx->_child._lockmode = (exclusive ? SLPC_EXCLLOCK : SLPC_SHAREDLOCK);
  SLPC_SPINLOCK_GET_LOCK (exclusive, ctx->_child._node->_node_lock);
#endif
}

/* To get the other context lock, maybe exclusive:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_lock_other_) (SLPC_PLACE_CTX *ctx, SLPC_NODE *other, int exclusive)
{
  SLPC_ASSERT (ctx->_other._node == SLPC_NULL);

  ctx->_other._node = other;

#if SLPC_USE_SPINLOCK
  SLPC_ASSERT (! SLPC_IS_LOCKED (ctx->_other));
  ctx->_other._lockmode = (exclusive ? SLPC_EXCLLOCK : SLPC_SHAREDLOCK);
  SLPC_SPINLOCK_GET_LOCK (exclusive, ctx->_other._node->_node_lock);
#endif
}

/* To switch the other context to the child context:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_child_other_) (SLPC_PLACE_CTX *ctx)
{
  SLPC_ASSERT (ctx->_other._node != SLPC_NULL);
  ctx->_child._node     = ctx->_other._node;
  ctx->_other._node     = SLPC_NULL;

#if SLPC_USE_SPINLOCK
  SLPC_ASSERT (SLPC_IS_LOCKED (ctx->_other) && ! SLPC_IS_LOCKED (ctx->_child));
  ctx->_child._lockmode = ctx->_other._lockmode;
  ctx->_other._lockmode = SLPC_NOLOCK;
#endif
}

/* To descend the context, switch child to place:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_ctx_descend_) (SLPC_PLACE_CTX *ctx)
{
#if SLPC_USE_SPINLOCK
  SLPC_NODE    *tmp_place = ctx->_place._node;
  int  tmp_place_lockmode = ctx->_place._lockmode;

  SLPC_ASSERT (SLPC_IS_LOCKED (ctx->_place) &&
	       SLPC_IS_LOCKED (ctx->_child) &&
	       !SLPC_IS_LOCKED (ctx->_other));
#endif

  SLPC_ASSERT (ctx->_place._node && ctx->_child._node);
  ctx->_place._node     = ctx->_child._node;
  ctx->_child._node     = SLPC_NULL;
  ctx->_other._node     = SLPC_NULL;

#if SLPC_USE_SPINLOCK
  ctx->_place._lockmode = ctx->_child._lockmode;
  ctx->_child._lockmode = SLPC_NOLOCK;
  SLPC_SPINLOCK_PUT_LOCK (tmp_place_lockmode == SLPC_EXCLLOCK, tmp_place->_node_lock);
#endif
}

/* The caller holds locks on both place and child, upgrade to
 * exclusive locks on both. */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_upgrade_locks_) (SLPC_PLACE_CTX *ctx)
{
  // Always requires an exclusive lock on PLACE, but we never have it
  // coming in because these operations always have the parent, which
  // is never a leaf (which might be already exclusive).  To recognize
  // the potential for concurrent modification, get the cycle count.
#if SLPC_USE_SPINLOCK
  int place_cycle = ctx->_place._node->_cycle;
  int child_cycle = ctx->_child._node->_cycle;

  SLPC_ASSERT (SLPC_IS_SHARED (ctx->_place) && SLPC_IS_LOCKED (ctx->_child));

  // Release both locks to avoid deadlock, upgrade PLACE to exclusive,
  // check if another process beat us
  SLPC_CTX_GIVE (&ctx->_place);
  SLPC_CTX_GIVE (&ctx->_child);
  SLPC_CTX_EXCL (&ctx->_place);

  if (unlikely (place_cycle != ctx->_place._node->_cycle))
    {
      SLPC_CTX_RESET (ctx);
      return SLPC_TRY_AGAIN;
    }

  // Wait for the child lock, check if another process beat us
  SLPC_CTX_EXCL (& ctx->_child);
  if (unlikely (child_cycle != ctx->_child._node->_cycle))
    {
      SLPC_CTX_RESET (ctx);
      return SLPC_TRY_AGAIN;
    }

  // Advance cycles, upgrade successful
  ctx->_place._node->_cycle += 1;
  ctx->_child._node->_cycle += 1;
#endif

  // Should never need to upgrade a leaf
  SLPC_ASSERT (ctx->_place._node->_type != SLPC_TYPE_LEAF);
  return SLPC_OKAY;
}

/* The caller holds locks on both place and child, upgrade to
 * 3 exclusive locks. */
static SLPC_INLINE SLPC_RESULT
SLPC_PARAM(slpc_upgrade_3locks_) (SLPC_PLACE_CTX *ctx, int aquire_left)
{
#if SLPC_USE_SPINLOCK
  int place_cycle = ctx->_place._node->_cycle;
  int child_cycle = ctx->_child._node->_cycle;

  SLPC_ASSERT (SLPC_IS_SHARED (ctx->_place) && SLPC_IS_LOCKED (ctx->_child));

  // Release both locks to avoid deadlock
  SLPC_CTX_GIVE (&ctx->_place);
  SLPC_CTX_GIVE (&ctx->_child);

  // Get exclusive place, check if another process beat us
  SLPC_CTX_EXCL (&ctx->_place);
  if (unlikely (place_cycle != ctx->_place._node->_cycle))
    {
      SLPC_CTX_RESET (ctx);
      return SLPC_TRY_AGAIN;
    }
#endif

  // Similar logic to upgrade_locks, but there is a special case for
  // the last entry which must aquire its left neighbor first
  if (SLPC_ITH_DOWN (ctx->_place._node, aquire_left) == ctx->_child._node)
    {
      // Wait for the child lock, then the right neighbor
      SLPC_CTX_EXCL       (& ctx->_child);
      SLPC_CTX_LOCK_OTHER (ctx, SLPC_ITH_DOWN (ctx->_place._node, aquire_left+1), 1);
    }
  else
    {
      // Wait for the left neighbor, then the child lock
      SLPC_CTX_LOCK_OTHER (ctx, SLPC_ITH_DOWN (ctx->_place._node, aquire_left), 1);
      SLPC_CTX_EXCL       (& ctx->_child);
    }

#if SLPC_USE_SPINLOCK
  // Check if another process beat us.  There is no cycle count to
  // check for the other node, since it was first read here
  if (unlikely (child_cycle != ctx->_child._node->_cycle))
    {
      SLPC_CTX_RESET (ctx);
      return SLPC_TRY_AGAIN;
    }

  // Advance cycles, upgrade successful
  ctx->_place._node->_cycle += 1;
  ctx->_child._node->_cycle += 1;
  ctx->_other._node->_cycle += 1;
#endif

  // Should never need to upgrade a leaf
  SLPC_ASSERT (ctx->_place._node->_type != SLPC_TYPE_LEAF);
  return SLPC_OKAY;
}

/*********************************************************************
			    NODE INIT METHODS
 *********************************************************************/

/** To initialize a new SLPC_NODE: first initialization
 */
static void
SLPC_PARAM(slpc_node_ctor_) (void *vnode)
{
#if SLPC_DEBUG != 0 || SLPC_USE_SPINLOCK != 0
  SLPC_NODE *node = (SLPC_NODE*) vnode;
#endif

#if SLPC_DEBUG
  node->_count  = 0;
  node->_type   = SLPC_TYPE_FREE;
  node->_right  = SLPC_NULL;
#endif

#if SLPC_USE_SPINLOCK
  node->_cycle  = 0;
  SLPC_SPINLOCK_INIT (node->_node_lock);
#endif
}

/* The SLPC SLAB implementation
 */
#if SLPC_USE_KUTSLAB == 0
static SLPC_INLINE SLPC_SLAB*
SLPC_PARAM(slpc_slab_create_) (void)
{
  SLPC_SLAB *slab = SLPC_MALLOC (sizeof (SLPC_SLAB));

  slab->_free  = SLPC_NULL;
  slab->_pages = SLPC_NULL;

  SLPC_SPINLOCK_INIT (slab->_lock);

  return slab;
}

static SLPC_INLINE void
SLPC_PARAM(slpc_slab_alloc_) (SLPC_SLAB *slab)
{
  SLPC_PAGE *page = (SLPC_PAGE*) SLPC_MALLOC (sizeof (SLPC_PAGE));
  int i;

  /* Link new nodes into slab free list */
  for (i = 0; i < SLPC_NODES_PER_PAGE; i += 1)
    {
      SLPC_NODE_CTOR (& page->_array[i]);
      page->_array[i]._right = & page->_array[i+1];
    }

  page->_array[SLPC_NODES_PER_PAGE-1]._right = slab->_free;

  // Update the free list
  slab->_free = page->_array;

  // Update the page list
  page->_link._next = slab->_pages;
  slab->_pages      = page;
}

static SLPC_INLINE int
SLPC_PARAM(slpc_slab_prealloc_) (SLPC_SLAB *slab, int maxelem)
{
  /* Compute the ceiling of maxelem / SLPC_MIN_COUNT, that's the
   * max number of leaf nodes.  Repeat for internal levels. */
  int total = 1;
  int nodes = SLPC_CEIL (maxelem, SLPC_MIN_COUNT);
  int pages, i;

  if (maxelem > SLPC_MAX_COUNT)
    {
      do
	{
	  total += nodes;
	  nodes  = SLPC_CEIL (nodes, SLPC_MIN_COUNT);
	}
      while (nodes > 1);
    }

  pages = SLPC_CEIL (total, SLPC_NODES_PER_PAGE);

  for (i = 0; i < pages; i += 1)
    {
      SLPC_SLAB_ALLOC (slab);
    }

  return total;
}

static SLPC_INLINE SLPC_NODE*
SLPC_PARAM(slpc_slab_new_) (SLPC_SLAB *slab)
{
  SLPC_NODE *node;

  // Need an exclusive lock on the slab
  SLPC_SPINLOCK_GET_EXCL (slab->_lock);

  if (unlikely (slab->_free == SLPC_NULL))
    {
      SLPC_SLAB_ALLOC (slab);
    }

  // Take one from the free list
  node = slab->_free;
  slab->_free = node->_right;

  SLPC_SPINLOCK_PUT_EXCL (slab->_lock);

  return node;
}

static SLPC_INLINE void
SLPC_PARAM(slpc_slab_free_) (SLPC_SLAB   *slab,
			     SLPC_NODE   *node)
{
  SLPC_SPINLOCK_GET_EXCL(slab->_lock);

  node->_right = slab->_free;
  slab->_free  = node;

  SLPC_SPINLOCK_PUT_EXCL(slab->_lock);
}

static SLPC_INLINE void
SLPC_PARAM (slpc_slab_destroy_) (SLPC_SLAB *slab)
{
  while (slab->_pages)
    {
      SLPC_PAGE *tmp = slab->_pages;

      slab->_pages = tmp->_link._next;

      SLPC_FREE (tmp);
    }

  SLPC_FREE (slab);
}
#endif

/* To initialize the pivot SLPC_NODE: first initialization
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_node_init_pivot_) (SLPC_NODE *node, SLPC_NODE *leaf)
{
  node->_count  = 1;
  node->_type   = SLPC_TYPE_PIVOT;
  node->_right  = SLPC_NULL;

  SLPC_ITH_KEY_DONT_CARE (node, 0);
  SLPC_ITH_DOWN (node, 0) = leaf;

#if SLPC_USE_SPINLOCK
  node->_cycle = 0;
  SLPC_SPINLOCK_INIT (node->_node_lock);
#endif
}

/* To initialize a leaf SLPC_NODE: re-initialization
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_node_init_leaf_) (SLPC_NODE *node)
{
  node->_count  = 0;
  node->_type   = SLPC_TYPE_LEAF;
  node->_right  = SLPC_NULL;
}

/* To initialize a root SLPC_NODE: re-initialization
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_node_init_root_) (SLPC_NODE *node, SLPC_NODE *child, SLPC_NODE *new_child)
{
  node->_count  = 2;
  node->_type   = SLPC_TYPE_INTERNAL;
  node->_right  = SLPC_NULL;

  SLPC_ITH_KEY           (node, 0) = SLPC_ITH_KEY (child, child->_count - 1);
  SLPC_ITH_KEY_DONT_CARE (node, 1);
  SLPC_ITH_DOWN          (node, 0) = child;
  SLPC_ITH_DOWN          (node, 1) = new_child;
}

/* To allocate a new SLPC_NODE: no initialization
 */
static SLPC_INLINE SLPC_NODE*
SLPC_PARAM(slpc_node_new_) (SLPC_ANCHOR *anchor)
{
  SLPC_NODE *node = (SLPC_NODE*) SLPC_SLAB_NEW (anchor->_slab);

#if SLPC_COUNT_NODES
  SLPC_SPINLOCK_GET_EXCL (anchor->_ns._node_count_lock);
  anchor->_ns._node_count += 1;
  SLPC_SPINLOCK_PUT_EXCL (anchor->_ns._node_count_lock);
#endif

  return node;
}

/* To free a SLPC_NODE
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_node_free_) (SLPC_ANCHOR *anchor, SLPC_NODE *node, SLPC_NODE_CTX *nodectx)
{
  // Must be holding the exclusive lock to free it, advance the cycle
#if SLPC_USE_SPINLOCK
  SLPC_ASSERT (SLPC_IS_EXCL (*nodectx));
  node->_cycle += 1;
  SLPC_CTX_GIVE (nodectx);
#endif

#if SLPC_COUNT_NODES
  SLPC_SPINLOCK_GET_EXCL (anchor->_ns._node_count_lock);
  anchor->_ns._node_count -= 1;
  SLPC_SPINLOCK_PUT_EXCL (anchor->_ns._node_count_lock);
#endif

  node->_type = SLPC_TYPE_LEAF;

  SLPC_SLAB_FREE (anchor->_slab, node);
}

/* To create a new SLPC_ANCHOR:
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_anchor_init_) (SLPC_ANCHOR *anchor, SLPC_SLAB *slab)
{
  memset (anchor, 0, sizeof (*anchor));

  STATIC_ASSERT (sizeof (SLPC_NODE) == SLPC_TARGET_SIZE);

  // Setup initial conditions
  anchor->_height     = 1;
  anchor->_slab       = slab;

#if SLPC_COUNT_KEYS
  anchor->_ks._key_count  = 0;
  SLPC_SPINLOCK_INIT (anchor->_ks._key_count_lock);
#endif

#if SLPC_COUNT_NODES
  anchor->_ns._node_count = 0;
  SLPC_SPINLOCK_INIT (anchor->_ns._node_count_lock);
#endif

#if SLPC_COUNT_RESTARTS
  anchor->_rs._restarts   = 0;
  SLPC_SPINLOCK_INIT (anchor->_rs._restart_count_lock);
#endif

  anchor->_left_leaf = SLPC_NODE_NEW (anchor);

  // There is no KEY value filled in for the pivot, but the node
  // protects root splits
  SLPC_NODE_INIT_PIVOT (SLPC_PIVOT (anchor), anchor->_left_leaf);
  SLPC_NODE_INIT_LEAF  (anchor->_left_leaf);
}

/*********************************************************************
		       NODE MAINTENENCE HELPERS
 *********************************************************************/

/* NOTE!  All of these functions assume that the nodes involved have
 * already been locked exclusive.
 */

/* This macro moves one pair between slots.  */
#define SLPC_SHIFT(fn,from,tn,to)                         \
   tn->_pairs[to] = fn->_pairs[from]

/* Shift the keys of a node right, leaving the slot [key_i] free for
 * insertion.  */
static SLPC_INLINE void
SLPC_PARAM(slpc_shift_keys_right_) (SLPC_NODE   *node,
				    int          key_i)
{
  int j;

  // Cannot shift right if maximum count
  SLPC_ASSERT (node->_count < SLPC_MAX_COUNT);

  for (j = node->_count - 1; j >= key_i; j -= 1)
    {
      // Counting downwards, move slots up one at a time
      SLPC_SHIFT(node,j,node,j+1);
    }

  node->_count += 1;
}

/* Shift the keys of a node left, removing the slot [key_i].
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_shift_keys_left_) (SLPC_NODE   *node,
				   int          key_i)
{
  // Cannot shift left if minimum count, except when height == 1, in
  // which case can shift all the way down

  for (; key_i < node->_count - 1; key_i += 1)
    {
      // Counting upwards, move slots down one at a time
      SLPC_SHIFT (node, key_i+1, node, key_i);
    }

  node->_count -= 1;
}

/* Split a node into two nodes, dividing its keys evenly.
 */
static SLPC_INLINE SLPC_NODE*
SLPC_PARAM(slpc_split_node_) (SLPC_ANCHOR *anchor,
			      SLPC_NODE   *node)
{
  SLPC_NODE* new_node = SLPC_NODE_NEW (anchor);
  int i;

  // The splitting node is full
  SLPC_ASSERT (node->_count == SLPC_MAX_COUNT);

  // The created node is the right neighbor of the splitting node
  new_node->_type      = node->_type;
  new_node->_right     = node->_right;
  new_node->_count     = SLPC_OVER_COUNT;
  node->_right         = new_node;
  node->_count         = SLPC_MIN_COUNT;

  for (i = SLPC_MAX_COUNT - 1; i >= SLPC_MIN_COUNT; i -= 1)
    {
      // Counting downwards in the splitting node, move slots to their
      // new position in the new node
      SLPC_SHIFT(node,i,new_node,i-SLPC_MIN_COUNT);
    }

  return new_node;
}

/* Reverse-split a node, replacing two nodes with one, used by delete.
 */
static SLPC_INLINE void
SLPC_PARAM(slpc_join_nodes_) (SLPC_ANCHOR   *anchor,
			      SLPC_NODE     *left,
			      SLPC_NODE     *right,
			      SLPC_NODE_CTX *right_ctx)
{
  int i, j;

  // The right node is being deleted: if either was above min count
  // the join should have been avoided by redistribution
  SLPC_ASSERT (left->_count == SLPC_MIN_COUNT &&
	       right->_count == SLPC_MIN_COUNT &&
	       left->_count + right->_count <= SLPC_MAX_COUNT);

  left->_right = right->_right;

  for (i = 0, j = left->_count; i < right->_count; i += 1, j += 1)
    {
      // Counting upwards in the deleting (right) node, move slots to
      // the end of the left node.
      SLPC_SHIFT(right,i,left,j);
    }

  left->_count += right->_count;

  SLPC_NODE_FREE (anchor, right, right_ctx);
}

/* Rebalance the keys between two nodes, moving right to left, used by
 * delete before slpc_join_nodes. */
static SLPC_INLINE void
SLPC_PARAM(slpc_redist_right_left_) (SLPC_NODE   *left,
				     SLPC_NODE   *right)
{
    int lc = left->_count;
    int rc = right->_count;
    int move = rc - ((lc + rc) >> 1);
    int i, j;

    // Balance the number of entries from right to left, must shift at
    // least one into the left (so it has room for deletion, the
    // precondition to descent)
    SLPC_ASSERT (left->_count == SLPC_MIN_COUNT &&
		 right->_count > SLPC_MIN_COUNT &&
		 move > 0);

    for (i = 0, j = lc; i < move; i += 1, j += 1)
      {
	// Counting upwards, move nodes from low-right to high-left
	SLPC_SHIFT(right,i,left,j);
      }

    for (j = 0; i < rc; i += 1, j += 1)
      {
	// Shift the remaining right slots down by the number of moved
	// slots.
	SLPC_SHIFT(right,i,right,j);
      }

    right->_count = rc - move;
    left->_count  = lc + move;
}

/* Rebalance the keys between two nodes, moving left to right, used by
 * delete before slpc_join_nodes. */
static SLPC_INLINE void
SLPC_PARAM(slpc_redist_left_right_) (SLPC_NODE   *left,
				     SLPC_NODE   *right)
{
    int lc = left->_count;
    int rc = right->_count;
    int move = lc - ((lc + rc) >> 1);
    int i, j;

    // Balance the number of entries from left to right, must shift at
    // least one into the right (so it has room for deletion, the
    // precondition to descent)
    SLPC_ASSERT (left->_count > SLPC_MIN_COUNT &&
		 right->_count == SLPC_MIN_COUNT &&
		 move > 0);

    for (i = rc-1, j = i+move; i >= 0; i -= 1, j -= 1)
      {
	// Counting downwards, shift the incoming number of
	// right-slots up to make room
	SLPC_SHIFT (right, i, right, j);
      }

    for (i = lc - move, j = 0; i < lc; i += 1, j += 1)
      {
	// Counting upwards, move slots from high-left to low-right
	SLPC_SHIFT (left, i, right, j);
      }

    right->_count = rc + move;
    left->_count = lc - move;
}

/*********************************************************************
			      DEBUG HELP
 *********************************************************************/

#if SLPC_DEBUG

#define SLPC_DONT_CARE_KEY(n,i) (((n)->_right == NULL) && ((n)->_type == SLPC_TYPE_INTERNAL || (n)->_type == SLPC_TYPE_PIVOT) && ((i) == ((n)->_count-1)))

static void
SLPC_PARAM(slpc_debug_print_) (SLPC_NODE *node)
{
  int x;

  if (node == NULL)
    {
      return;
    }

  printf ("%s node (%p) has %d entries\n",
	  (node->_type == SLPC_TYPE_LEAF ? "Leaf" :
	   (node->_type == SLPC_TYPE_INTERNAL ? "Internal" :
	    (node->_type == SLPC_TYPE_PIVOT ? "Pivot" : "Free"))),
	  node,
	  node->_count);

  for (x = 0; x < node->_count; x += 1)
    {
      if (node->_type == SLPC_TYPE_LEAF)
	{
	  printf ("[%d] = [%d]\n", x, SLPC_ITH_KEY (node, x));
	}
      else
	{
	  SLPC_NODE *child = SLPC_ITH_DOWN (node, x);

	  printf ("[%d] = [%d] {%d..%d}\n", x, SLPC_ITH_KEY (node, x),
		  SLPC_ITH_KEY (child, 0),
		  SLPC_ITH_KEY (child, child->_count-1));
	}
    }
}

static void
SLPC_PARAM(slpc_debug_structure_) (SLPC_ANCHOR *anchor)
{
  SLPC_NODE *cur_left = SLPC_PIVOT (anchor);
  SLPC_NODE *next_left = NULL;
  int l, i;

  for (l = anchor->_height; l >= 0; l -= 1, cur_left = next_left)
    {
      SLPC_KEY key;
      SLPC_KEY nkey;
      SLPC_NODE *child;

      /* Except for the last iteration, set first-left for the next
       * level. */
      if (l > 0)
	{
	  next_left  = SLPC_ITH_DOWN (cur_left, 0);
	}

      for (; cur_left; cur_left = cur_left->_right)
	{
	  key   = SLPC_ITH_KEY  (cur_left, 0);

	  //SLPC_PARAM(slpc_debug_print_) (cur_left);

	  /* Check the first child's high key. */
	  if (l > 0 && ! SLPC_DONT_CARE_KEY (cur_left, 0))
	    {
	      child = SLPC_ITH_DOWN (cur_left, 0);
	      SLPC_ASSERT (SLPC_GREATER_EQUAL (key, SLPC_ITH_KEY (child, child->_count-1)));
	    }

	  /* Check key ordering for this node. */
	  for (i = 1; i < cur_left->_count; i += 1)
	    {
	      nkey = SLPC_ITH_KEY (cur_left, i);

	      /* Ignore the DONT_CARE key, which happens at the end of
	       * an internal chain. */
	      if (! SLPC_DONT_CARE_KEY (cur_left, i))
		{
		  /* If an internal node, check the child's high key. */
		  if (l > 0)
		    {
		      child = SLPC_ITH_DOWN (cur_left, i);

		      if (l > 1)
			{
			  SLPC_ASSERT (SLPC_EQUAL_TO (nkey, SLPC_ITH_KEY (child, child->_count-1)));
			}
		      else
			{
			  SLPC_ASSERT (SLPC_GREATER_EQUAL (nkey, SLPC_ITH_KEY (child, child->_count-1)));
			}
		    }

		  SLPC_ASSERT (SLPC_GREATER_THAN (nkey, key));
		}

	      key = nkey;
	    }

	  /* Check the right link's first key is greater than this node's high key. */
	  if (cur_left->_right != NULL)
	    {
	      nkey = SLPC_ITH_KEY (cur_left->_right, 0);

	      SLPC_ASSERT (SLPC_GREATER_THAN (nkey, key));

	      key = nkey;
	    }
	}
    }

}

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
