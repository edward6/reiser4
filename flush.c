/* Copyright 2001 by Hans Reiser, licensing governed by reiser4/README */

/* The design document for this file is at www.namesys.com/flush-alg.html */

/* Note on abbreviation: "squeeze and allocate" == "squalloc" */

#include "reiser4.h"

typedef struct flush_scan     flush_scan;
typedef struct flush_position flush_position;

#define FLUSH_RELOCATE_THRESHOLD 64
#define FLUSH_RELOCATE_DISTANCE  64

/* The flush_scan data structure maintains the state of an in-progress flush
 * scan on a single level of the tree. */
struct flush_scan {
	/* The current number of nodes on this level. */
	unsigned  size;

	/* There may be a maximum number of nodes for a scan on any single level.  When
	 * going leftward, it is determined by REISER4_FLUSH_SCAN_MAXNODES (see
	 * reiser4.h).  When going rightward, it is determined by the number of nodes
	 * required to reach FLUSH_RELOCATE_THRESHOLD. */
	unsigned max_size;

	/* Direction: LEFT_SIDE or RIGHT_SIDE. */
	sideof direction;

	/* True if some condition stops the search (e.g., we found a clean
	 * node before reaching max size, we found a node belonging to another
	 * atom). */
	int       stop;

	/* The current scan position, referenced. */
	jnode    *node;

	/* When the position is unformatted, its parent and coordinate. */
	lock_handle parent_lock;
	new_coord  parent_coord;
};

/* An encapsulation of the current flush point and all the parameters that are passed
 * through the entire flush routine. */
struct flush_position {
	jnode                *point;
	lock_handle           point_lock;
	lock_handle           parent_lock;
	new_coord            parent_coord;
	reiser4_blocknr_hint  preceder;
	unsigned              left_scan_count;
	unsigned              right_scan_count;
	int                   batch_relocate;
	int                   parent_first_broken;
	int                   squalloc_count;
	struct bio           *bio;
};

typedef enum {
	SCAN_EVERY_LEVEL,
	SCAN_FIRST_LEVEL,
	SCAN_NEVER
} flush_scan_config;

static void          flush_scan_init              (flush_scan *scan);
static void          flush_scan_done              (flush_scan *scan);
static void          flush_scan_set_current       (flush_scan *scan, jnode *node, unsigned add_size);
static int           flush_scan_common            (flush_scan *scan);
static int           flush_scan_finished          (flush_scan *scan);
static int           flush_scan_extent            (flush_scan *scan, int skip_first);
static int           flush_scan_formatted         (flush_scan *scan);
static int           flush_scan_left              (flush_scan *scan, jnode *node);
static int           flush_scan_right_upto        (flush_scan *scan, jnode *node, __u32 *res_count, __u32 limit);

static int           flush_left_relocate          (jnode *node, const new_coord *parent_coord);

static int           flush_extents                (flush_position *pos);
static int           flush_enqueue_point          (flush_position *pos);
static int           flush_allocate_point         (flush_position *pos);
static int           flush_finish                 (flush_position *pos);
static void          flush_parent_first_broken    (flush_position *pos);

static int           flush_lock_leftpoint         (jnode                  *start_node,
						   lock_handle            *start_lock,
						   flush_scan_config  scan_config,
						   flush_position         *flush_pos);

static int           flush_find_rightmost           (const new_coord *parent_coord, reiser4_block_nr *pblk);
static int           flush_find_preceder            (jnode *node, new_coord *parent_coord, reiser4_block_nr *pblk);

static int           squalloc_leftpoint             (flush_position *pos);
static int           squalloc_leftpoint_end_of_twig (flush_position *pos);
static int           squalloc_update_leftpoint      (flush_position *pos);
static int           squalloc_parent_first          (flush_position *pos);
static int           squalloc_parent_first_recursive (flush_position *pos, znode *child, new_coord *coord);

static int           squalloc_children            (flush_position *pos);
/*static*/ int       squalloc_right_neighbor      (znode *left, znode *right, reiser4_blocknr_hint *preceder);
static int           squalloc_right_twig          (znode *left, znode *right, reiser4_blocknr_hint *preceder);
static int           squalloc_right_twig_cut      (new_coord * to, reiser4_key * to_key, znode *left);
static int           squeeze_right_leaf           (znode *right, znode *left);
static int           shift_one_internal_unit      (znode *left, znode *right);

flush_scan_config    flush_scan_get_config        (jnode *node, int flags);

static int           jnode_lock_parent_coord      (jnode *node,
						   new_coord *coord,
						   lock_handle *parent_lh,
						   znode_lock_mode mode);
static int           jnode_is_allocated           (jnode *node);
static int           znode_get_utmost_if_dirty    (znode *node, lock_handle *right_lock, sideof side);
static int           znode_same_parents           (znode *a, znode *b);

static int           flush_pos_init               (flush_position *pos);
static void          flush_pos_done               (flush_position *pos);
static void          flush_pos_stop               (flush_position *pos);
static int           flush_pos_valid              (flush_position *pos);
static int           flush_pos_unformatted        (flush_position *pos);
static int           flush_pos_to_child           (flush_position *pos);
static int           flush_pos_to_parent          (flush_position *pos);
static void          flush_pos_set_point          (flush_position *pos, jnode *node);
static void          flush_pos_release_point      (flush_position *pos);

#define FLUSH_IS_BROKEN 0
#define FLUSH_DEBUG     1

/* This is the main entry point for flushing a jnode, called by the transaction manager
 * when an atom closes (to commit writes) and called by the VM under memory pressure (to
 * early-flush dirty blocks).
 *
 * Two basic steps are performed: first the "leftpoint" of the input jnode is located,
 * which is found by scanning leftward past dirty nodes and upward as long as the parent
 * is dirty or the child is being relocated.  A config option determines whether
 * left-scanning is performed at higher levels.  The "leftpoint" is the node we will
 * process first.  The comments for flush_lock_leftpoint() will describe this in greater
 * detail.
 *
 * After finding the initial leftpoint, squalloc_leftpoint is called to squeeze and
 * allocate the subtree rooted at the leftpoint in a parent-first traversal, then it
 * proceeds to squeeze and allocate the leftpoint of the subtree to its right, and so on.
 *
 * During squeeze and allocate, nodes are scheduled for writeback and their jnodes are set
 * to the "clean" state (as far as the atom is concerned).
 */
int jnode_flush (jnode *node, int flags)
{
	int ret;
	flush_position flush_pos;
	flush_scan_config scan_config = flush_scan_get_config (node, flags);
	flush_scan right_scan;

	if (FLUSH_IS_BROKEN) {
		jnode_set_clean (node);
		return 0;
	}

	//if (FLUSH_DEBUG && jnode_is_formatted (node)) { print_znode ("flush this znode:", JZNODE (node)); }
	if (FLUSH_DEBUG) print_tree_rec ("parent_first", current_tree, REISER4_NODE_CHECK);
	assert ("jmacd-5012", jnode_check_dirty (node));

	flush_scan_init (& right_scan);

	if ((ret = flush_pos_init (& flush_pos))) {
		return ret;
	}

	if (jnode_is_allocated (node)) {
		/* If the node has already been through the allocate process, we have
		 * decided whether it is to be relocated or overwritten (or if it is a new
		 * block, it has an initial allocation). */

		/* Note: in this case, we may set_point for an unformatted node, which
		 * will result in flush_pos_unformatted returning false -- its okay
		 * though since we don't go through the squalloc pass. */
		flush_pos_set_point (& flush_pos, node);

		if ((ret = flush_enqueue_point (& flush_pos))) {
			goto failed;
		}
	} else {

		/* Locate the leftpoint of the node to flush, which is found by scanning
		 * leftward and recursing upward as long as the neighbor or parent is
		 * dirty. */
		if ((ret = flush_lock_leftpoint (node, NULL, scan_config, & flush_pos))) {
			goto failed;
		}

		/* If we have not scanned past enough nodes to reach the threshold,
		 * continue counting to the right.  This makes an approximation, if it has
		 * not found enough nodes during the left-scan then it only counts nodes
		 * to the right of the argument node.  This avoids counting nodes to the
		 * right on internal levels, but this counts in the most-likely place
		 * first.  The alternative would be to run squalloc_leftpoint with a "just
		 * count up-to" argument, but I think this would be more costly and not
		 * less effective in almost all cases. */
		if (flush_pos.left_scan_count < FLUSH_RELOCATE_THRESHOLD) {

			/* Have to release the flush_pos lock temporarily due to possible conflict. */
			done_lh (& flush_pos.point_lock);

			if ((ret = flush_scan_right_upto (& right_scan, node, & flush_pos.right_scan_count,
							  FLUSH_RELOCATE_THRESHOLD - flush_pos.left_scan_count))) {
				goto failed;
			}

			if (jnode_is_formatted (flush_pos.point)) {
				if ((ret = longterm_lock_znode (& flush_pos.point_lock, JZNODE (flush_pos.point), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
					goto failed;
				}
			}

			flush_pos.batch_relocate = (flush_pos.right_scan_count +
						    flush_pos.left_scan_count) >= FLUSH_RELOCATE_THRESHOLD;
		} else {
			flush_pos.batch_relocate = 1;
		}

		/* Squeeze and allocate in parent-first order, scheduling writes and
		 * cleaning jnodes.  The algorithm is described in more detail below.
		 * This processes the subtree rooted at leftpoint and then continues
		 * to the right of leftpoint. */
		if ((ret = squalloc_leftpoint (& flush_pos))) {
			goto failed;
		}
	}

	/* Perform batch write. */
	ret = flush_finish (& flush_pos);

	if (FLUSH_DEBUG) print_tree_rec ("parent_first", current_tree, REISER4_NODE_CHECK);
   failed:

	flush_pos_done (& flush_pos);
	flush_scan_done (& right_scan);
	return ret;
}

/* This should be a plugin or mount option.  This sets the default scan_left policy
 * according to flush-alg.html. */
flush_scan_config flush_scan_get_config (jnode *node, int flags)
{
	if (flags & JNODE_FLUSH_MEMORY && jnode_get_level (node) == LEAF_LEVEL) {
		/* The conservative approach. */
		return SCAN_FIRST_LEVEL;
	}

	/* The expansive approach. */
	assert ("jmacd-8812", (flags & JNODE_FLUSH_COMMIT) || jnode_get_level (node) != LEAF_LEVEL);
	return SCAN_EVERY_LEVEL;
}

/********************************************************************************
 * FLUSH_LOCK_LEFTPOINT
 ********************************************************************************/

/* This function is called on @start_node, which is where the search for the leftpoint
 * begins.  The leftpoint is the node we are going to allocate first.  This procedure is
 * recursive (up the tree), although it could be re-written less clearly using an
 * iterative algorithm.  The proceedure is fairly straight-forward, but is complicated by
 * maintaining as few locks as possible at any given time.
 *
 * If the @start_node is passed in locked (it may not be), the lock will be released as
 * soon as it becomes unnecessary.
 *
 * If @scan_left is true, a scan-left operation is performed starting from @start_node,
 * which proceeds to the left until a clean node is found.  The @end_node is found at the
 * end of this leftward scan.  If the @end_node is formatted then a read-lock is acquired
 * on @end_node, except in the case where @start_node is the same as @end_node and
 * @start_node was already locked, in which case the lock would be redundent.
 *
 * If @scan_left is false, then we do not scan left.  This happens after a previous
 * leftpoint has been squeezed and allocated and we have reached a new sub-tree to the
 * right of the previous leftpoint.  We may then ascend to a higher level.  In this case,
 * @end_node == @start_node.
 *
 * Once the @end_node is locked, the parent is read-locked.  If at the root level,
 * jnode_lock_parent_coord returns the above root node; in that case stop recursion.
 *
 * With both the child (end) node and the parent node locked, decide whether to relocate
 * the child node.  If the child node will be relocated, then dirty the parent (because
 * child node's location will change).
 *
 * If the parent is dirty, recursively repeat process (at the parent level) until the
 * leftmost, higest, dirty node is found, which we call the leftpoint.  Returns with only
 * the leftpoint locked (and referenced), unless the node is unformatted, in which case
 * only a reference is returned.
 */
static int flush_lock_leftpoint (jnode                  *start_node,
				 lock_handle            *start_lock,
				 flush_scan_config       scan_config,
				 flush_position         *pos)
{
	int ret = 0;
	jnode *end_node;
	znode *parent_node;
	flush_scan level_scan;
	new_coord parent_coord;
	lock_handle end_lock;
	lock_handle parent_lock;
	assert ("jmacd-5013", jnode_check_dirty (start_node));
	assert ("jmacd-5014", ! jnode_is_allocated (start_node));

	flush_scan_init (& level_scan);
	init_lh (& parent_lock);
	init_lh (& end_lock);

	if (scan_config != SCAN_NEVER) {
		/* Scan start_node's level for the leftmost dirty neighbor. */
		if ((ret = flush_scan_left (& level_scan, start_node))) {
			goto failure;
		}

		end_node = level_scan.node;
		pos->left_scan_count += level_scan.size;

	} else {
		/* No scanning, only upward. */
		end_node = start_node;
		pos->left_scan_count += 1;
	}

	assert ("jmacd-5015", jnode_check_dirty (end_node));
	assert ("jmacd-5016", ! jnode_is_allocated (end_node));
	assert ("jmacd-5017", txn_same_atom_dirty (start_node, end_node));

	/* If start_node is not the same as end_node, start_lock is no longer
	 * needed.  Release it. */
	if (end_node != start_node && start_lock != NULL) {
		done_lh (start_lock);
		start_lock = NULL;
	}

	/* Now (start_lock == NULL) implies end_lock is needed (unless it is
	 * unformatted).  This lock is being taken so that the parent can be
	 * locked.  Is it absolutely necessary that the child be locked in
	 * order to lock the parent?  Maybe not. */
	if (start_lock == NULL &&
	    jnode_is_formatted (end_node) &&
	    (ret = longterm_lock_znode (& end_lock, JZNODE (end_node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
		goto failure;
	}

	/* Check for the root, then stop recursion. */
	if (jnode_is_formatted (end_node) && znode_is_root (JZNODE (end_node))) {
		goto root_case;
	}

	/* Read lock the parent.  If this node is the root, we'll get the
	 * above_root node back.  This may cause atom fusion. */
	if ((ret = jnode_lock_parent_coord (end_node, & parent_coord, & parent_lock, ZNODE_READ_LOCK))) {
		goto failure;
	}

	parent_node = parent_lock.node;

	/* This policy should be a plugin.  I am concerned that breaking these
	 * things into plugins will result in duplicated effort.  Returns 0
	 * for no-relocate, 1 for relocate, < 0 for error. */
	if ((ret = flush_left_relocate (end_node, & parent_coord)) < 0) {
		goto failure;
	}

	/* If relocating the child, artificially dirty the parent right now. */
	if (ret == 1) {

		/* The parent may need to be captured, as well.  Probably should break
		 * these two statments into a subroutine, because it might want to be used
		 * in flush_extents. */
		if (! txn_same_atom_dirty (end_node, ZJNODE (parent_node))) {
			done_lh (& parent_lock);

			/* Note: This is a case where a "read-modify-write" request makes sense. */
			if ((ret = jnode_lock_parent_coord (end_node, & parent_coord, & parent_lock, ZNODE_WRITE_LOCK))) {
				goto failure;
			}
		}

		znode_set_dirty (parent_node);
	}

	/* If the parent is dirty, it needs to be squeezed also, recurse upwards */
	if (znode_is_dirty (parent_node) && ! jnode_is_allocated (ZJNODE (parent_node))) {

		/* Release lock at this level before going upward. */
		done_lh (start_lock ? start_lock : & end_lock);

		/* Modify scan_config for the recursive call. */
		if (scan_config == SCAN_FIRST_LEVEL) {
			scan_config = SCAN_NEVER;
		}

		/* Recurse upwards. */
		if ((ret = flush_lock_leftpoint (ZJNODE (parent_node), & parent_lock, scan_config, pos))) {
			goto failure;
		}

	} else {
		/* End the recursion, setup flush_pos. */
	root_case:

		if (jnode_is_unformatted (end_node)) {
			/* If end_node is unformatted, then we need the parent position in
			 * squalloc_leftpoint, so keep it. */
			pos->parent_coord = parent_coord;
			move_lh (& pos->parent_lock, & parent_lock);

		} else {
			flush_pos_set_point (pos, end_node);

			/* Get a write lock at end_node.  Release any locks we might hold
			 * first, they are all read locks on this level. */
			done_lh (start_lock ? start_lock : & end_lock);
			done_lh (& pos->point_lock);

			/* We still hold the parent lock, so this is LOPRI.  The parent
			 * lock could be released here, but in a few cases we need the
			 * parent locked again soon after, which is expensive in the
			 * unformatted node case. */
			if ((ret = longterm_lock_znode (& pos->point_lock, JZNODE (end_node), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
				jput (end_node);
				goto failure;
			}
		}
	}

	assert ("jmacd-2030", ret == 0);
 failure:
	done_lh            (& parent_lock);
	done_lh            (& end_lock);
	flush_scan_done (& level_scan);

	return ret;
}

/********************************************************************************
 * (RE-) LOCATION POLICIES
 ********************************************************************************/

/* This implements the leftward should_relocate() policy, which is described in
 * flush-alg.html.  This is called at the end of each scan-left on a level while searching
 * for the leftpoint node.  This implements the is-it-close-enough-to-its-preceder? test
 * for relocation. */
static int flush_should_relocate (const reiser4_block_nr *pblk,
				  const reiser4_block_nr *nblk)
{
	reiser4_block_nr dist;

	assert ("jmacd-7710", *pblk != 0 && *nblk != 0);
	assert ("jmacd-7711", ! blocknr_is_fake (pblk));
	assert ("jmacd-7712", ! blocknr_is_fake (nblk));

	/* Distance is the absolute value. */
	dist = (*pblk > *nblk) ? (*pblk - *nblk) : (*nblk - *pblk);

	/* First rule: If the block is less than 64 blocks away from its preceder block,
	 * do not relocate. */
	if (dist <= FLUSH_RELOCATE_DISTANCE) {
		return 0;
	}

	return 1;
}

/* FIXME: comment */
static int flush_left_relocate  (jnode *node, const new_coord *parent_coord)
{
	int ret;
	new_coord coord;
	reiser4_block_nr pblk = 0;
	reiser4_block_nr nblk = 0;

	assert ("jmacd-8989", ! jnode_is_root (node));

	/* New nodes are treated as if they are being relocated. */
	if (JF_ISSET (node, ZNODE_ALLOC)) {
		return 1;
	}

	/* Find the preceder. */
	ncoord_dup (& coord, parent_coord);

	if ((ret = flush_find_preceder (node, & coord, & pblk))) {
		return ret;
	}

	/* If (pblk == 0) then the preceder isn't allocated either: relocate. */
	if (pblk == 0) {
		return 1;
	}

	nblk = *jnode_get_block (node);

	return flush_should_relocate (& pblk, & nblk);
}

/* Find the block number of the parent-first preceder of a node.  If the node is a
 * leftmost child, then return its parent's block.  If the node is a leaf, return its left
 * neighbor's block.  Otherwise, call flush_find_rightmost to find the rightmost
 * descendent. */
static int flush_find_preceder (jnode *node, new_coord *parent_coord, reiser4_block_nr *pblk)
{
	/* If ncoord_prev_unit returns 1, its the leftmost coord of its parent. */
	if (ncoord_prev_unit (parent_coord) == 1) {
		/* Not leftmost of parent, don't relocate */
		*pblk = *znode_get_block (parent_coord->node);
		return 0;
	}

	/* If at the leaf level, its the rightmost child of the parent_coord... */
	if (jnode_get_level (node) == LEAF_LEVEL) {
		/* Get the neighbors block number, if its real. */
		return item_utmost_child_real_block (parent_coord, RIGHT_SIDE, pblk);
	}

	/* Non-leftmost internal node case. */
	return flush_find_rightmost (parent_coord, pblk);
}

/* Find the rightmost descendant block number of @node.  Set preceder->blk to
 * that value.  This can never be called on a leaf node, the leaf case is
 * handled by flush_preceder_hint. */
static int flush_find_rightmost (const new_coord *parent_coord, reiser4_block_nr *pblk)
{
	int ret;
	znode *parent = parent_coord->node;
	jnode *child;
	new_coord child_coord;

	assert ("jmacd-2043", znode_get_level (parent) >= TWIG_LEVEL);

	if (znode_get_level (parent) == TWIG_LEVEL) {
		/* End recursion, we made it all the way. */

		/* Get the rightmost block number of this coord, which is the
		 * child to the left of the starting node.  If the block is a
		 * unallocated or a hole, the preceder is set to 0. */
		return item_utmost_child_real_block (parent_coord, RIGHT_SIDE, pblk);
	}

	/* Get the child if it is in memory. */
	if ((ret = item_utmost_child (parent_coord, RIGHT_SIDE, & child))) {
		return ret;
	}

	/* If the child is not in memory, set preceder remains set to zero. */
	if (child == NULL) {
		return 0;
	}

	/* If the child is unallocated, there's no reason to continue, all its
	 * children must be unallocated. */
	if (! blocknr_is_fake (jnode_get_block (child))) {

		assert ("jmacd-2061", jnode_is_formatted (child));

		/* The child is in memory, recurse. */
		ncoord_init_last_unit (& child_coord, JZNODE (child));

		ret = flush_find_rightmost (& child_coord, pblk);
	}

	jput (child);
	return ret;
}

/********************************************************************************
 * SQUEEZE AND ALLOCATE
 ********************************************************************************/

/* This function squeezes and allocates starting at the leftpoint of a region to flush.
 * First, the sub-tree rooted at the leftpoint is squeezed and allocated in parent-first
 * order (squalloc_parent_first).  After squeezing that leftpoint, this functions moves to
 * the right sibling of the original leftpoint and, so long as it is dirty/in the same
 * atom, it repeats the process.
 *
 * After squeezing and allocating a sub-tree rooted at the leftpoint we go to the right,
 * which may involve changing parents.  When a new parent is reached we must update the
 * current leftpoint, since the new parent may be dirty.  (The old parent is probably
 * clean, or else it would have been the leftpoint, not is child.)  Updating the leftpoint
 * is handled in squalloc_update_leftpoint, which is basically a call to
 * flush_lock_leftpoint with scanning-left disabled.
 *
 * There is special treatment of unformatted leftpoints and the leaf-level in general in
 * this function.  The leftpoint may be an unformatted node if the extent is being
 * overwritten instead of relocated (i.e., its parent is clean).  The special treatment is
 * mainly a performance concern -- getting the parent of an unformatted node is relatively
 * expensive (it requires search-by-key), so we iterate rightward along the twig-level for
 * extents, then along the leaf-level again for formatted leftpoints of the leaf-level.
 * When we reach the end of a sequence of formatted leaves, we must check the parent since
 * there may be an unformatted node to the right.  When we reach the end of a twig-level
 * we first check for a dirty twig-sibling, then for a dirty node at the leaf-level to the
 * right.  This is handled by squalloc_leftpoint_end_of_twig.
 *
 * This proceedure simply enters a do-loop to handle both the unformatted and formatted
 * cases.  The squalloc_leftpoint_end_of_twig and sqalloc_update_leftpoint functions
 * update the loop variables and return to this loop.
 */
static int squalloc_leftpoint (flush_position *pos)
{
	int ret;
	lock_handle right_lock;

	init_lh (& right_lock);

	/* flush_lock_leftpoint sets the initial conditions for this loop. */

	/* Call the appropriate squalloc_parent_first() or flush_extents() method on node,
	 * then continue to the right. */
	while (flush_pos_valid (pos)) {

		if (flush_pos_unformatted (pos)) {
			/* Extent case.  The parent is locked and the parent_coord is kept
			 * up-to-date during the unformatted case of this loop. */
			assert ("jmacd-5591", ncoord_is_existing_item (& pos->parent_coord) != 0);

			/* Unformatted node case: we would not be called at this level if
			 * the parent was dirty, instead squalloc_twig would have been
			 * called, therefore the extent was previously allocated and all
			 * we have to do is issue flushes and add to the overwrite set. */
			if ((ret = flush_extents (pos))) {
				goto exit;
			}

			/* If parent_coord->node is unset following the call to
			 * flush_extents, it indicates that no more flushing was
			 * required. */
			if ((ret = flush_pos_valid (pos)) == 0) {
				goto exit;
			}

			/* Flush extent returns when it reaches the end of its node or
			 * when it reaches a formatted node pointer. */
			if (ncoord_is_existing_item (& pos->parent_coord) == 0) {
				/* End of twig node.  This call updates the state of this loop for
				 * the next iteration at the leftpoint of the sub-tree to the
				 * right. */
				if ((ret = squalloc_leftpoint_end_of_twig (pos))) {
					goto exit;
				}

			} else {
				/* Reached an internal node, go back down to the leaf level. */
				if ((ret = flush_pos_to_child (pos))) {
					goto exit;
				}
			}

		} else {

			/* Reset the counter. */
			pos->squalloc_count = 0;

			/* Formatted node case.  Squeeze and allocate this node. */
			if ((ret = squalloc_parent_first (pos))) {
				goto exit;
			}

			/* Get the right neighbor if it is dirty. */
			if ((ret = znode_get_utmost_if_dirty (JZNODE (pos->point), & right_lock, RIGHT_SIDE)) != 0) {

				/* ENOENT means the right neighbor is not in memory or not
				 * found.  If at the leaf level we have to check for an
				 * adjacent unformatted node. */
				if (! (ret == -ENOENT && jnode_get_level (pos->point) == LEAF_LEVEL)) {
					/* ENOENT and ENAVAIL are not exits, they are stops. */
					if (ret == -ENOENT || ret == -ENAVAIL) { ret = 0; }
					goto exit;
				}

				/* We're at the leaf level. */
				assert ("jmacd-7665", jnode_get_level (pos->point) == LEAF_LEVEL);

 				/* Get the parent coordinate. */
				if ((ret = flush_pos_to_parent (pos))) {
					goto exit;
				}

				/* ncoord_next_item returns 0 if there are no more items. */
				if (ncoord_next_item (& pos->parent_coord) == 0) {

					/* End of twig node.  This call updates the state of this
					 * loop for the next iteration at the leftpoint of the
					 * sub-tree to the right.  There is a slight optimization
					 * possible, which is that we've already checked the leaf
					 * level sibling and it is NULL, meaning we don't have to
					 * check the leaf level in end_of_twig. */
					if ((ret = squalloc_leftpoint_end_of_twig (pos))) {
						goto exit;
					}

				} else if (item_is_extent_n (& pos->parent_coord)) {

					/* Extent case -- continue loop. */

				} else {

					/* Formatted leaf neighbor is not in memory. */
					assert ("jmacd-7088", item_is_internal_n (& pos->parent_coord));
					ret = 0;
					goto exit;
				}

			} else {
				/* Advance to the right sibling of this znode. */
				int same_parents = znode_same_parents (right_lock.node, JZNODE (pos->point));

				flush_pos_set_point (pos, ZJNODE (right_lock.node));

				done_lh (& pos->point_lock);
				move_lh (& pos->point_lock, & right_lock);

				if (! same_parents) {

					/* We have a new parent, so call squalloc_update_leftpoint () to
					 * find the next leftpoint in parent first order. */
					if ((ret = squalloc_update_leftpoint (pos))) {
						goto exit;
					}
				}
			}
		}

	}

	ret = 0;
 exit:
	done_lh (& right_lock);
	return ret;
}

/* This function is called when squalloc_leftpoint reaches the end of a twig node.  Next
 * we check the twig's right neighbor.  If the right twig is clean, check its leftmost
 * child.  If either the right twig or its leftmost child are dirty, call
 * squalloc_update_leftpoint appropriately to find the next leftpoint for parent first
 * traversal.  If neither is dirty, stop this squalloc.
 */
static int squalloc_leftpoint_end_of_twig (flush_position *pos)
{
	int ret;
	lock_handle right_lock;

	assert ("jmacd-8861", flush_pos_unformatted (pos));
	assert ("jmacd-8862", ncoord_is_existing_item (& pos->parent_coord) == 0);

	init_lh (& right_lock);

	/* Not using znode_get_utmost_if_dirty to get the parent's right sibling since the parent's
	 * sibling may be clean while its leftmost child is dirty. */
	ret = reiser4_get_right_neighbor (& right_lock, pos->parent_lock.node, ZNODE_READ_LOCK, 0);

	/* Done with the old twig, point -- possibly set a new one. */
	flush_pos_stop (pos);

	if (ret != 0) {
		if (ret == -ENOENT || ret == -ENAVAIL) { ret = 0; }
		goto exit;
	}

	/* See if the right twig is dirty. */
	if (znode_is_dirty (right_lock.node)) {
		/* If the twig is dirty, don't need to check the child.  Set the twig
		 * itself to pos->point then call update_leftpoint to find the actual
		 * leftpoint node of the new sub-tree. */

		flush_pos_set_point (pos, ZJNODE (right_lock.node));
		move_lh (& pos->point_lock, & right_lock);

		/* Fallthrough to update_leftpoint. */
	} else {
		jnode *child;

		/* See if the left child is in memory. */
		ncoord_init_first_unit (& pos->parent_coord, right_lock.node);

		if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child))) {
			goto exit;
		}

		if (child == NULL || ! jnode_check_dirty (child)) {
			/* If it is not dirty and the twig is not dirty, then the
			 * squalloc_leftpoint loop should just terminate. */
			ret = 0;
			goto exit;
		} else if (jnode_is_formatted (child)) {

			/* If a formatted child is dirty and the twig is not dirty, update
			 * leftpoint starting from the child. */
			if ((ret = longterm_lock_znode (& pos->point_lock, JZNODE (child), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
				goto exit;
			}

			/* Fallthrough to update_leftpoint. */
			flush_pos_set_point (pos, child);

		} else {
			/* If an unformatted child is dirty, then just continue at the
			 * twig level. */
			move_lh (& pos->parent_lock, & right_lock);
			ret = 0;
			goto exit;
		}
	}

	/* Update leftpoint -- find the highest dirty ancestor of (*node) using
	 * flush_lock_leftpoint (without any leftward scan). */
	ret = squalloc_update_leftpoint (pos);

 exit:
	done_lh (& right_lock);
	return ret;
}

/* Called at the end of a leftpoint subtree to update for the next iteration of
 * squalloc_leftpoint loop.  This is a basically a call to flush_lock_leftpoint with
 * left-scanning disabled.  Returns with the squalloc_leftpoint loop variables set for the
 * next iteration of squalloc_leftpoint.
 */
static int squalloc_update_leftpoint (flush_position *pos)
{
	int ret;

	assert ("jmacd-8551", ! flush_pos_unformatted (pos));

	/* This call updates pos->point to its own leftpoint. */
	if ((ret = flush_lock_leftpoint (pos->point, & pos->point_lock, SCAN_NEVER, pos))) {
		return ret;
	}

	/* FIXME: Need to decide to dirty the parent of the new leftpoint here?  Or set
	 * parent_first_broken? */

	assert ("jmacd-8552", ! flush_pos_unformatted (pos));

	return 0;
}

/* Squeeze and allocate, parent first -- This is initially called on the leftpoint of a
 * subtree to flush, then it recurses downward over that subtree.
 *
 * If the node is not dirty, halt recursion.
 *
 * If the node is allocated, return immediately.  Otherwise, allocate it, then allocate
 * its children.
 *
 * Once the down-recursive case has been applied, see if the right neighbor is dirty and
 * if so, attempt to squeeze it into this node.  If the item being squeezed from left to
 * right is and internal node, then the right-squeezing halts while the shifted subtree is
 * recursively processed.  Once the shifted-right subtree is squeezed and allocated,
 * squeezing from the left continues.
 */
static int squalloc_parent_first (flush_position *pos)
{
	int ret;
	lock_handle right_lock;
	int squeeze;

	assert ("jmacd-1048", jnode_is_formatted (pos->point));
	assert ("jmacd-1050", ! node_is_empty (JZNODE (pos->point)));
	assert ("jmacd-1051", znode_is_write_locked (JZNODE (pos->point)));
	assert ("jmacd-1052", ! flush_pos_unformatted (pos));

        /* Stop recursion if its not dirty, meaning don't allocate children either.
         * Children might be dirty but there is an overwrite below this level or else this
         * node would be dirty.  Stop recursion if the node is not yet allocated. */
        if (! jnode_is_dirty (pos->point) || jnode_is_allocated (pos->point)) {
		flush_parent_first_broken (pos);
                return 0;
        }

	/* Allocate it now (parent first). */
	if ((ret = flush_allocate_point (pos))) {
		return ret;
	}

	/* Recursive case: handles all children. */
	if ((jnode_get_level (pos->point) > LEAF_LEVEL) && (ret = squalloc_children (pos))) {
		return ret;
	}

        /* Now @node and all its children (recursively) are squeezed and allocated.  Next,
	 * we will squeeze this node's right neighbor into this one, allocating as we go.
	 * First, we must check that the right node is in the same atom without requesting
	 * a long term lock.  (Because the long term lock request will FORCE it into the
	 * same atom...).  The "again" label is used to repeat the following steps, as
	 * long as node's right neighbor is completely squeezed into this one. */

	init_lh (& right_lock);

 right_again:
	if ((ret = znode_get_utmost_if_dirty (JZNODE (pos->point), & right_lock, RIGHT_SIDE))) {
		/* A return value of -ENOENT means the neighbor is not dirty,
		 * not in the same atom, or not in memory. */
		if (ret == -ENOENT || ret == -ENAVAIL) {
			ret = 0;
			goto enqueue;
		}
		goto cleanup;
	}

	assert ("jmacd-1052", ! node_is_empty (right_lock.node));

 squeeze_again:
	/* Squeeze from the right until an internal node is shifted.  At that
	 * point, we recurse downwards and squeeze its children.  When the
	 * recursive case is finished, continue squeezing at this level. */
	squeeze = squalloc_right_neighbor (JZNODE (pos->point), right_lock.node, & pos->preceder);

	switch (squeeze) {
	case SUBTREE_MOVED: {
		/* Unit of internal item has been shifted, now allocate and
		 * squeeze that subtree (now the last item of this node). */
		new_coord crd;
		znode *child;

		ncoord_init_last_unit (& crd, JZNODE (pos->point));

		assert ("vs-442", item_is_internal_n (& crd));

		/* FIXME: Only want this child if it is in the same atom! */
		spin_lock_dk (current_tree);
		child = child_znode_n (& crd, 1/*set delim key*/);
		spin_unlock_dk (current_tree);
		if (IS_ERR (child)) {
			ret = PTR_ERR (child);
			goto cleanup;
		}

		/* Skipping this node if it is dirty means that we won't
		 * squeeze and allocate any of its dirty grand-children.  Oh
		 * well! */
		if (! znode_check_dirty (child)) {
			flush_parent_first_broken (pos);
			goto squeeze_again;
		}

		/* Recursive case: single formatted child. */
		if ((ret = squalloc_parent_first_recursive (pos, child, NULL))) {
			goto cleanup;
		}
		goto squeeze_again;
	}
	case SQUEEZE_SOURCE_EMPTY: {
		/* Right neighbor was squeezed completely into @node, try to
		 * squeeze with the new right neighbor.  Release the
		 * right_lock first. */
		assert ("vs-444", node_is_empty (right_lock.node));
		done_lh (& right_lock);
		goto right_again;
	}
	default:

		/* Either an error occured or SQUEEZE_TARGET_FULL (or
		 * can't-squeeze-right, e.g., end-of-level or
		 * unformatted-to-the-right) */
		assert ("vs-443", squeeze == SQUEEZE_TARGET_FULL || squeeze < 0);
		ret = ((squeeze < 0) ? squeeze : 0);
	}
 enqueue:
	if (ret == 0) {
		ret = flush_enqueue_point (pos);
	}

	if (FLUSH_DEBUG) print_tree_rec ("parent_first", current_tree, REISER4_NODE_CHECK);
 cleanup:
	done_lh (& right_lock);
	return ret;
}

/* Called on a non-leaf-level znode to process its current children in the
 * squalloc traversal.  This @node was allocated immediately prior to this
 * call.
 *
 * For extent children: allocate extents "in place", meaning to expand the
 * current record within the parent node.
 *
 * For node children: call squalloc_parent_first (recursively).
 */
static int squalloc_children (flush_position *pos)
{
	int ret;
	new_coord crd;

	ncoord_init_first_unit (& crd, JZNODE (pos->point));

	/* Assert the pre-condition for the following do-loop, essentially
	 * stating that the node is not empty. */
	assert ("jmacd-2000", ! ncoord_is_after_rightmost (& crd));

	/* Do ... while not the last unit. */
	do {
		if (item_is_extent_n (& crd)) {
			if ((ret = allocate_extent_item_in_place_n (& crd, & pos->preceder))) {
				return ret;
			}
		} else if (item_is_internal_n (& crd)) {
			/* Get the child of this node pointer, check for
			 * error, skip if it is not dirty. */
			znode *child;

			spin_lock_dk (current_tree);
			child = child_znode_n (& crd, 1);
			spin_unlock_dk (current_tree);

			if (IS_ERR (child)) { return PTR_ERR (child); }

			if (! znode_check_dirty (child)) {
				flush_parent_first_broken (pos);
				continue;
			}

			/* Recursive call: since we release the lock on this node it is
			 * possible that the coordinate will change, therefore pass the
			 * coordinate to be updated. */
			if ((ret = squalloc_parent_first_recursive (pos, child, & crd))) {
				return ret;
			}

		} else {
			warning ("jmacd-2001", "Unexpected item type above leaf level");
			print_znode ("node", JZNODE (pos->point));
			return -EIO;
		}

	} while (! ncoord_next_unit (& crd));

	return 0;
}

/* This procedure takes care of descending the flush_position->point to @child, releasing
 * the parent lock and getting the child lock.  Then it calls the squalloc_parent_first
 * routine, recursively, and then it re-acquires the parent lock, returning
 * flush_position->point back to the parent.  It is conceivable that the parent can change
 * during this period, so the @coord argument is returned to allow it to change.
 */
static int squalloc_parent_first_recursive (flush_position *pos, znode *child, new_coord *coord)
{
	int ret;

	assert ("jmacd-2329", ! flush_pos_unformatted (pos));

	/* Relase pos->point reference (pos->point_lock still holds a reference). */
	flush_pos_release_point (pos);

	/* Lock the child. */
	{
		/* Have to move the pos->point lock out of the way, it is being replaced. */
		lock_handle save_lock;
		init_lh (& save_lock);
		move_lh (& save_lock, & pos->point_lock);

		ret = longterm_lock_znode (& pos->point_lock, child, ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI);

		/* Now finished with the old pos->point lock, have a new one (or else we
		 * didn't get the child lock and we lose the point_lock, doesn't matter). */
		done_lh (& save_lock);

		if (ret != 0) { return ret; }
	}

	/* Reference the child, set it to pos->point. */
	pos->point = jref (ZJNODE (child));

	/* Perform the recursive step. */
	if ((ret = squalloc_parent_first (pos))) {
		return ret;
	}

	assert ("jmacd-8122", child == JZNODE (pos->point));
	/* FIXME: This can be violated: I thought I fixed it once, but this code is more
	 * broken than that. */
	assert ("jmacd-8123", ! znode_is_root (JZNODE (pos->point)));

	/* Lock the parent. */
	{
		/* Have to move the pos->point lock out of the way, it is being replaced. */
		lock_handle save_lock;
		init_lh (& save_lock);
		move_lh (& save_lock, & pos->point_lock);

		ret = jnode_lock_parent_coord (pos->point, coord, & pos->point_lock, ZNODE_WRITE_LOCK);

		done_lh (& save_lock);

		/* Release pos->point reference again. */
		flush_pos_release_point (pos);

		if (ret == 0) { pos->point = jref (ZJNODE (pos->point_lock.node)); }
	}

	return ret;
}

/* Squeeze and allocate the right neighbor.  This is called after @left and
 * its current children have been squeezed and allocated already.  This
 * proceedure's job is to squeeze and items from @right to @left.
 *
 * If at the leaf level, use the squeeze_everything_left memcpy-optimized
 * version of shifting (squeeze_right_leaf).
 *
 * If at the twig level, extents are allocated as they are shifted from @right
 * to @left (squalloc_right_twig).
 *
 * At any other level, shift one internal item and return to the caller
 * (squalloc_parent_first) so that the shifted-subtree can be processed in
 * parent-first order.
 *
 * When unit of internal item is moved, squeezing stops and SUBTREE_MOVED is
 * returned.  When all content of @right is squeezed, SQUEEZE_SOURCE_EMPTY is
 * returned.  If nothing can be moved into @left anymore, SQUEEZE_TARGET_FULL
 * is returned.
 */
/*static*/ int squalloc_right_neighbor (znode    *left,
					znode    *right,
					reiser4_blocknr_hint *preceder)
{
	int ret;

	assert ("vs-425", !node_is_empty (left));

	if (node_is_empty (right)) {
		/* This is possible.  FIXME: VS has promised to write an explaination... */
		return SQUEEZE_SOURCE_EMPTY;
	}

	switch (znode_get_level (left)) {
	case LEAF_LEVEL:
		/* Use the memcpy-optimzied shift.
		 *
		 * FIXME: This can't be used if there is, e.g., an encryption plugin.
		 */
		ret = squeeze_right_leaf (right, left);
		break;

	case TWIG_LEVEL:
		/* Shift with extent allocating until either an internal item
		 * is encountered or everything is shifted or no free space
		 * left in @left */
		ret = squalloc_right_twig (left, right, preceder);
		break;

	default:
		/* All other levels contain items of internal type only. */
		ret = shift_one_internal_unit (left, right);
		break;

	}

	assert ("jmacd-2011", ret < 0 || ret == SQUEEZE_SOURCE_EMPTY || ret == SQUEEZE_TARGET_FULL  || ret == SUBTREE_MOVED);

	if (ret == SQUEEZE_SOURCE_EMPTY) {
		reiser4_stat_flush_add (squeezed_completely);
	}

	return ret;
}

/* Shift as much as possible from @right to @left using the memcpy-optimized
 * shift_everything_left.  @left and @right are formatted neighboring nodes on
 * leaf level. */
static int squeeze_right_leaf (znode * right, znode * left)
{
	int ret;
	carry_pool pool;
	carry_level todo;

	init_carry_pool (& pool);
	init_carry_level (& todo, & pool);

	ret = shift_everything_left (right, left, & todo);

	spin_lock_dk (current_tree);
	update_znode_dkeys (left, right);
	spin_unlock_dk (current_tree);

	if (ret >= 0) {
		/* Carry is called to update delimiting key or to remove empty
		 * node. */
		ret = carry (& todo, NULL /* previous level */);
	}

	done_carry_pool (& pool);

	if (ret < 0) {
		return ret;
	}

	return node_is_empty (right) ? SQUEEZE_SOURCE_EMPTY : SQUEEZE_TARGET_FULL;
}

/* Copy as much of the leading extents from @right to @left, allocating
 * unallocated extents as they are copied.  Returns SQUEEZE_TARGET_FULL or
 * SQUEEZE_SOURCE_EMPTY when no more can be shifted.  If the next item is an
 * internal item it calls shift_one_internal_unit and may then return
 * SUBTREE_MOVED. */
static int squalloc_right_twig (znode    *left,
				znode    *right,
				reiser4_blocknr_hint *preceder)
{
	int ret = 0;
	new_coord coord;
	reiser4_key stop_key;

	assert ("jmacd-2008", ! node_is_empty (right));

	ncoord_init_first_unit (&coord, right);

	/* Initialize stop_key to detect if any extents are copied.  After
	 * this loop loop if stop_key is still equal to *min_key then nothing
	 * was copied (and there is nothing to cut). */
	stop_key = *min_key ();

	while (item_is_extent_n (&coord)) {

		if ((ret = allocate_and_copy_extent_n (left, &coord, preceder, &stop_key)) < 0) {
			return ret;
		}

		if (ret == SQUEEZE_TARGET_FULL) {
			/* Could not complete with current extent item. */
			break;
		}

		assert ("jmacd-2009", ret == SQUEEZE_CONTINUE);

		/* ncoord_next_item returns 0 if there are no more items. */
		if (ncoord_next_item (&coord) == 0) {
			ret = SQUEEZE_SOURCE_EMPTY;
			break;
		}
	}

	if (!keyeq (&stop_key, min_key ())) {
		int cut_ret;

		/* @coord is set to the first unit that does not have to be
		 * cut or after last item in the node.  If we are positioned
		 * at the coord of a unit, it means the extent processing
		 * stoped in the middle of an extent item, the last unit of
		 * which was not copied.  Cut everything before that point. */
		if (ncoord_is_existing_unit (& coord)) {
			ncoord_prev_unit (& coord);
		}

		/* Helper function to do the cutting. */
		if ((cut_ret = squalloc_right_twig_cut (&coord, &stop_key, left))) {
			return cut_ret;
		}
	}

	if (node_is_empty (right)) {
		/* The whole right node was copied into @left. */
		assert ("vs-464", ret == SQUEEZE_SOURCE_EMPTY);
		return ret;
	}

	ncoord_init_first_unit (&coord, right);

	if (! item_is_internal_n (&coord)) {
		/* There is no space in @left anymore. */
		assert ("vs-433", item_is_extent_n (&coord));
		assert ("vs-465", ret == SQUEEZE_TARGET_FULL);
		return ret;
	}

	return shift_one_internal_unit (left, right);
}

/* squalloc_right_twig helper function, cut a range of extent items from
 * cut node to->node from the beginning up to coord @to. */
static int squalloc_right_twig_cut (new_coord * to, reiser4_key * to_key, znode * left)
{
	new_coord from;
	reiser4_key from_key;

	ncoord_init_first_unit (&from, to->node);
	item_key_by_ncoord (&from, &from_key);

	return cut_node_n (&from, to, &from_key, to_key,
			   NULL /* smallest_removed */, DELETE_DONT_COMPACT, left);
}

/* Shift first unit of first item if it is an internal one.  Return
 * SQUEEZE_TARGET_FULL if it fails to shift an item, otherwise return
 * SUBTREE_MOVED.
 */
static int shift_one_internal_unit (znode * left, znode * right)
{
	int ret;
	carry_pool pool;
	carry_level todo;
	new_coord coord;
	int size, moved;

	ncoord_init_first_unit (&coord, right);

	assert ("jmacd-2007", item_is_internal_n (&coord));

	init_carry_pool (&pool);
	init_carry_level (&todo, &pool);

	size = item_length_by_ncoord (&coord);
	ret  = node_shift_n (left, &coord, left, SHIFT_LEFT,
			     1/* delete @right if it becomes empty*/,
			     0/* move coord */,
			     &todo);

	/* If shift returns positive, then we shifted the item. */
	assert ("vs-423", ret <= 0 || size == ret);
	moved = (ret > 0);

	if (moved) {
		/* Carry is called to update delimiting key or to remove empty node.
		 * FIXME: Nikita: Note how this comment disagrees with the code? */
		znode_set_dirty (left);
		znode_set_dirty (right);
		spin_lock_dk (current_tree);
		update_znode_dkeys (left, right);
		spin_unlock_dk (current_tree);

		ret = carry (&todo, NULL /* previous level */);
	}

	done_carry_pool (&pool);

	if (ret != 0) {
		/* Shift or carry operation failed. */
		return ret;
	}

	return moved ? SUBTREE_MOVED : SQUEEZE_TARGET_FULL;
}

/********************************************************************************
 * ALLOCATE INTERFACE
 ********************************************************************************/

/* Return true if @node has already been processed by the squeeze and allocate
 * process.  This implies the block address has been finalized for the
 * duration of this atom (or it is clean and will remain in place).  If this
 * returns true you may use the block number as a hint. */
static int jnode_is_allocated (jnode *node)
{
	/* It must be relocated or wandered.  New allocations are set to relocate. */
	return JF_ISSET (node, ZNODE_RELOC) ||
	       JF_ISSET (node, ZNODE_WANDER);
}

/* This is called when the parent-first flush traversal skips a node in parent-first
 * order, signifying that flush_allocate() must re-find the parent-first preceder.  If we
 * have decided to batch-relocate, however, we ignore this setting because we will
 * relocate anyway. */
static void flush_parent_first_broken (flush_position *pos)
{
	pos->parent_first_broken = 1;
}

/* FIXME: comment */
static int flush_alloc_block (reiser4_blocknr_hint *preceder, jnode *node, reiser4_block_nr max_dist)
{
	int ret;
	reiser4_block_nr blk;
	reiser4_block_nr len = 1;
	int is_root;

	assert ("jmacd-1233", jnode_is_formatted (node));

	preceder->max_dist = max_dist;

	if ((ret = reiser4_alloc_blocks (preceder, & blk, & len))) {
		return ret;
	}

	is_root = znode_is_root (JZNODE (node));

	/* FIXME: free old location if not fake? */

	/* WARNING: UGLY, TEMPORARY */
	{
		lock_handle parent_lock;

		init_lh (& parent_lock);

		if (! is_root) {
			new_coord ncoord;
			tree_coord tcoord;

			if ((ret = jnode_lock_parent_coord (node, & ncoord, & parent_lock, ZNODE_WRITE_LOCK))) {
				goto out;
			}

			ncoord_to_tcoord (& tcoord, & ncoord);

			internal_update (& tcoord, blk);

		} else {
			znode *fake = zget (current_tree, &FAKE_TREE_ADDR, NULL, 0 , GFP_KERNEL);

			if (IS_ERR (fake)) { ret = PTR_ERR(fake); goto out; }

			ret = longterm_lock_znode (& parent_lock, fake, ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI);

			if (ret != 0) { goto out; }

			spin_lock_tree (current_tree);
			current_tree->root_block = blk;
			spin_unlock_tree (current_tree);
		}
	out:
		done_lh (& parent_lock);
		if (ret != 0) { return ret; }
	}

	if ((ret = znode_rehash (JZNODE (node), & blk))) {
		return ret;
	}

	return 0;
}

/* FIXME: comment */
static int flush_allocate_point (flush_position *pos)
{
	jnode *node = pos->point;
	int ret;

	if (JF_ISSET (node, ZNODE_ALLOC) || jnode_is_root (node)) {
		/* No need to decide with new nodes, they are treated the same as
		 * relocate. If the root node is dirty, relocate. */
		JF_SET (node, ZNODE_RELOC);

	} else if (pos->squalloc_count == 0) {

		/* This indicates the node has a clean parent, as it is the root of a
		 * parent-first traversal. */
		JF_SET (node, ZNODE_WANDER);

	} else if (pos->batch_relocate != 0) {

		/* We have enough nodes to relocate no matter what. */
		JF_SET (node, ZNODE_RELOC);
	}

	/* An actual decision may need to be made.  Update the preceder first, if
	 * necessary. */
	if (! JF_ISSET (node, ZNODE_WANDER) && pos->parent_first_broken) {
		lock_handle parent_lock;
		new_coord parent_coord;
		reiser4_block_nr pblk;

		init_lh (& parent_lock);

		assert ("jmacd-7988", jnode_is_unformatted (node) || znode_is_any_locked (JZNODE (node)));
			
		if ((ret = jnode_lock_parent_coord (node, & parent_coord, & parent_lock, ZNODE_READ_LOCK)) ||
		    (ret = flush_find_preceder (node, & parent_coord, & pblk))) {
		}
			
		done_lh (& parent_lock);

		if (ret != 0) { return ret; }

		pos->preceder.blk = pblk;
	}

	/* Assume successive allocations will be in parent-first order, unless "broken" is
	 * set again. */
	pos->parent_first_broken = 0;

	/* The decision may still need to be made, but now the preceder is correct. */
	if (! JF_ISSET (node, ZNODE_RELOC) && ! JF_ISSET (node, ZNODE_WANDER)) {

		reiser4_block_nr dist;
		reiser4_block_nr nblk = *jnode_get_block (node);

		assert ("jmacd-6172", blocknr_is_fake (! & nblk));
		assert ("jmacd-6173", blocknr_is_fake (& pos->preceder.blk));
		assert ("jmacd-6174", pos->preceder.blk != 0);

		dist = nblk < pos->preceder.blk ? nblk : pos->preceder.blk;

		if (dist <= 1) {
			/* Can't get any closer than this. */
			JF_SET (node, ZNODE_WANDER);
		} else {
			/* See if we can find a closer block (forward direction only). */ 
			if ((ret = flush_alloc_block (& pos->preceder, node, dist)) && (ret != -ENOSPC)) {
				return ret;
			}

			if (ret == 0) {
				/* Got a better allocation. */
				JF_SET (node, ZNODE_RELOC);
			} else if (dist < FLUSH_RELOCATE_DISTANCE) {
				/* The present allocation is good enough. */
				JF_SET (node, ZNODE_WANDER);
			} else {
				/* Otherwise, try to relocate to the best position. */
				goto best_reloc;
			}
		}
	} else if (JF_ISSET (node, ZNODE_RELOC)) {
		/* Just do the best relocation we can. */
	best_reloc:

		if ((ret = flush_alloc_block (& pos->preceder, node, 0ULL))) {
			return ret;
		}

	} else {
		/* Else this is the new preceder. */
		pos->preceder.blk = *jnode_get_block (node);
	}

	return 0;
}

/* This enqueues the current flush point into the developing "struct bio" queue. */
static int flush_enqueue_point (flush_position *pos)
{
	int ret;
	struct bio_vec *bvec;

	assert ("jmacd-1771", jnode_is_allocated (pos->point));
	assert ("jmacd-1772", jnode_check_dirty (pos->point));

	/* If we reach the threshold, flush a batch immediately. */
	if (pos->bio->bi_vcnt == FLUSH_RELOCATE_THRESHOLD) {

		if ((ret = flush_finish (pos))) {
			return ret;
		}

		assert ("jmacd-6611", pos->bio == NULL);

		if ((pos->bio = bio_alloc (GFP_NOFS, FLUSH_RELOCATE_THRESHOLD)) == NULL) {
			return -ENOMEM;
		}
	}

	bvec = & pos->bio->bi_io_vec[pos->bio->bi_vcnt++];

	bvec->bv_page   = pos->point->pg;
	bvec->bv_len    = 0;
	bvec->bv_offset = 0;

	jnode_set_clean (pos->point);

	return 0;
}

/* FIXME: comment */
static int flush_finish (flush_position *pos)
{
	assert ("jmacd-7711", pos->bio != NULL && pos->bio->bi_vcnt != 0);

	/* FIXME: finish this struct bio. */
	bio_put (pos->bio);
	pos->bio = NULL;
	return 0;
}

/* Called with @coord set to an extent that _may_ need to be flushed.  The parent is
 * (likely) clean or else we would have tried to squeeze and called squalloc_twig instead.
 * This is expected to flush all extents until end-of-node or an internal-item is found.
 *
 * Returns with @coord not set to any item to indicate that no more flushing was
 * required. */
static int flush_extents (flush_position *pos UNUSED_ARG)
{
	/* FIXME: */
	/* call flush_parent_first_broken in here? */

	return 0;
}

/********************************************************************************
 * JNODE INTERFACE
 ********************************************************************************/

/* Lock a node (if formatted) and then get its parent locked, set the child's
 * coordinate in the parent.  If the child is the root node, the above_root
 * znode is returned but the coord is not set.  This function may cause atom
 * fusion, but it is only used for read locks (at this point) and therefore
 * fusion only occurs when the parent is already dirty. */
static int jnode_lock_parent_coord (jnode *node,
				    new_coord *coord,
				    lock_handle *parent_lh,
				    znode_lock_mode parent_mode)
{
	int ret;

	assert ("jmacd-2060", jnode_is_unformatted (node) || znode_is_any_locked (JZNODE (node)));

	if (jnode_is_unformatted (node)) {

		/* Unformatted node case: Generate a key for the extent entry,
		 * search in the tree using ncoord_by_key, which handles
		 * locking for us. */
		struct inode *ino = node->pg->mapping->host;
		reiser4_key   key;
		file_plugin  *fplug = inode_file_plugin (ino);
		loff_t        loff = node->pg->index << PAGE_CACHE_SHIFT;

		assert ("jmacd-1812", coord != NULL);

		if ((ret = fplug->key_by_inode (ino, loff, & key))) {
			return ret;
		}

		if ((ret = ncoord_by_key (current_tree, & key, coord, parent_lh, parent_mode, FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL, 0)) != CBK_COORD_FOUND) {
			return ret;
		}

	} else {
		/* Formatted node case: */
		assert ("jmacd-2061", ! znode_is_root (JZNODE (node)));

		if ((ret = reiser4_get_parent (parent_lh, JZNODE (node), parent_mode, 1))) {
			return ret;
		}

		/* Make the child's position "hint" up-to-date.  (Unless above
		 * root, which caller must check.) */
		if ((coord != NULL) &&
		    (ret = find_child_ptr_n (parent_lh->node, JZNODE (node), coord))) {
			return ret;
		}
	}

	return 0;
}

/* Get the right neighbor of a znode locked provided a condition is met.  The
 * neighbor must be dirty and a member of the same atom.  If there is no right
 * neighbor or the neighbor is not in memory, -ENOENT is returned.  If there
 * is a neighbor but it is not dirty or not in the same atom, -ENAVAIL is
 * returned. */
static int znode_get_utmost_if_dirty (znode *node, lock_handle *lock, sideof side)
{
	znode *neighbor;
	int go;
	int ret;

	assert ("jmacd-6334", znode_is_connected (node));

	spin_lock_tree (current_tree);
	neighbor = side == RIGHT_SIDE ? node->right : node->left;
	if (neighbor != NULL) {
		zref (neighbor);
	}
	spin_unlock_tree (current_tree);

	if (neighbor == NULL) {
		return -ENOENT;
	}

	if (! (go = txn_same_atom_dirty (ZJNODE (node), ZJNODE (neighbor)))) {
		ret = -ENAVAIL;
		goto fail;
	}

	if ((ret = reiser4_get_neighbor (lock, node, ZNODE_WRITE_LOCK, side == LEFT_SIDE ? GN_GO_LEFT : 0))) {
		/* May return -ENOENT or -ENAVAIL. */
		goto fail;
	}

	/* Can't assert is_dirty here, even though we checked it above,
	 * because there is a race when the tree_lock is released. */
        if (! znode_is_dirty (lock->node)) {
		done_lh (lock);
		ret = -ENAVAIL;
	}

 fail:
	if (neighbor != NULL) {
		zput (neighbor);
	}
	return ret;
}

/* Return true if two znodes have the same parent.  This is called with both nodes
 * write-locked (for squeezing) so no tree lock is needed. */
static int znode_same_parents (znode *a, znode *b)
{
	assert ("jmacd-7011", znode_is_write_locked (a));
	assert ("jmacd-7012", znode_is_write_locked (b));

	/* The assumption here could be broken if the "hint" actually becomes one... */
	return a->ptr_in_parent_hint.node == b->ptr_in_parent_hint.node;
}

/********************************************************************************
 * FLUSH SCAN LEFT
 ********************************************************************************/

/* Initialize the flush_scan data structure. */
static void flush_scan_init (flush_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
	init_lh (& scan->parent_lock);
}

/* Release any resources held by the flush scan, e.g., release locks, free memory, etc. */
static void flush_scan_done (flush_scan *scan)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}
	done_lh (& scan->parent_lock);
}

/* Returns true if leftward flush scanning is finished. */
static int flush_scan_finished (flush_scan *scan)
{
	return scan->stop || scan->size >= scan->max_size;
}

/* Return true if the scan should continue to the left.  Go left if the node
 * is not allocated, dirty, and in the same atom as the current scan position.
 * If not, deref the "left" node and stop the scan. */
static int flush_scan_goto (flush_scan *scan, jnode *tonode)
{
	int go;

	go = ! jnode_is_allocated (tonode) && txn_same_atom_dirty (scan->node, tonode);

	if (! go) {
		jput (tonode);
		scan->stop = 1;
	}

	return go;
}

/* Set the current scan->node, refcount it, increment size, and deref previous current. */
static void flush_scan_set_current (flush_scan *scan, jnode *node, unsigned add_size)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}

	scan->node  = node;
	scan->size += add_size;
}

/* Return true if going left. */
static int flush_scanning_left (flush_scan *scan)
{
	return scan->direction == LEFT_SIDE;
}

/* Performs leftward scanning starting from an unformatted node and its parent coordinate */
static int flush_scan_extent_coord (flush_scan *scan, new_coord *coord)
{
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist; /* FIXME: is u64 right? */
	struct inode *ino = NULL;
	struct page *pg;
	int ret, allocated, incr;

	assert ("jmacd-1404", ! flush_scan_finished (scan));
	assert ("jmacd-1405", jnode_get_level (scan->node) == LEAF_LEVEL);
	assert ("jmacd-1406", jnode_is_unformatted (scan->node));

	scan_index = jnode_get_index (scan->node);

	assert ("jmacd-7889", item_is_extent_n (coord));

	extent_get_inode_n (coord, & ino);

	if (ino == NULL) {
		scan->stop = 1;
		goto exit;
	}
	
	do {
		/* If not allocated, the entire extent must be dirty and in the same atom.
		 * (Actually, I'm not sure this is properly enforced, but it should be the
		 * case since one atom must write the parent and the others must read
		 * it). */
		allocated  = extent_is_allocated_n (coord);
		unit_index = extent_unit_index_n (coord);
		unit_width = extent_unit_width_n (coord);

		assert ("jmacd-7187", unit_width > 0);
		assert ("jmacd-7188", scan_index >= unit_index);
		assert ("jmacd-7189", scan_index <= unit_index + unit_width - 1);

		if (flush_scanning_left (scan)) {
			scan_max  = unit_index;
			scan_dist = scan_index - unit_index;
			incr      = -1;
		} else {
			scan_max  = unit_index + unit_width - 1;
			scan_dist = scan_max - unit_index;
			incr      = +1;
		}

		/* If the extent is allocated we have to check each of its blocks.  In the
		 * debugging case: verify that all unallocated nodes are present and in
		 * the proper atom. */
		if (scan_max != scan_index) {
			if (allocated || REISER4_DEBUG) {

				do {
					pg = find_get_page (ino->i_mapping, scan_index + incr);

					if (pg == NULL) {
						assert ("jmacd-3550", allocated);
						break;
					}

					neighbor = jnode_of_page (pg);

					page_cache_release (pg);

					info ("scan index %lu: node %p(atom=%p,dirty=%u,allocated=%u) neighbor %p(atom=%p,dirty=%u,allocated=%u)\n",
					      scan_index+incr,
					      scan->node, scan->node->atom, jnode_check_dirty (scan->node), jnode_is_allocated (scan->node),
					      neighbor, neighbor->atom, jnode_check_dirty (neighbor), jnode_is_allocated (neighbor));

					assert ("jmacd-3551", allocated || (! jnode_is_allocated (neighbor) && txn_same_atom_dirty (neighbor, scan->node)));

					if (! flush_scan_goto (scan, neighbor)) {
						break;
					}

					flush_scan_set_current (scan, neighbor, 1);

					scan_index += incr;
				} while (scan_max != scan_index);

			} else {

				/* Optimized case for unallocated extents, skip to the end. */
				pg = find_get_page (ino->i_mapping, scan_max);

				if (pg == NULL) {
					warning ("jmacd-8337", "unallocated node not in memory!");
					ret = -EIO;
					goto exit;
				}

				neighbor = jnode_of_page (pg);

				page_cache_release (pg);

				assert ("jmacd-3551", ! jnode_is_allocated (neighbor) && txn_same_atom_dirty (neighbor, scan->node));
				
				flush_scan_set_current (scan, neighbor, scan_dist);

				scan_index  = scan_max;
			}
		}

		/* Continue as long as there are more extent units. */
	} while (ncoord_sideof_unit (coord, scan->direction) == 0 && item_is_extent_n (coord));

	ret = 0;
 exit:
	if (ino != NULL) { iput (ino); }
	return ret;
}

/* Performs leftward scanning starting from an unformatted node.  Skip_first indicates
 * that the scan->node is set to a formatted node and we are interested in continuing at
 * the neighbor if it is unformatted. */
static int flush_scan_extent (flush_scan *scan, int skip_first)
{
	int ret;
	lock_handle first_lock;
	jnode *child;

	if (skip_first) {
		init_lh (& first_lock);

		if ((ret = longterm_lock_znode (& first_lock, JZNODE (scan->node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
			return ret;
		}
	}

	ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, ZNODE_READ_LOCK);

	if (skip_first) {
		done_lh (& first_lock);
	}

	if (ret != 0) {
		return ret;
	}

	for (;; skip_first = 0) {
		if (skip_first == 0) {
			if ((ret = flush_scan_extent_coord (scan, & scan->parent_coord))) {
				return ret;
			}

			if (flush_scan_finished (scan)) {
				break;
			}

		} else {
			ncoord_sideof_unit (& scan->parent_coord, scan->direction);
		}

		if (ncoord_is_after_sideof_unit (& scan->parent_coord, scan->direction)) {

			lock_handle save_lock;

			init_lh (& save_lock);
			move_lh (& save_lock, & scan->parent_lock);

			ret = znode_get_utmost_if_dirty (scan->parent_lock.node, & scan->parent_lock, scan->direction);

			done_lh (& save_lock);

			if (ret == -ENOENT) { scan->stop = 1; break; }

			if (ret != 0) { return ret; }

			ncoord_init_sideof_unit (& scan->parent_coord, scan->parent_lock.node, sideof_reverse (scan->direction));
		}

		if (! item_is_extent_n (& scan->parent_coord) && skip_first) {
			scan->stop = 1;
			break;
		}
				
		if ((ret = item_utmost_child (& scan->parent_coord, sideof_reverse (scan->direction), & child))) {
			return ret;
		}

		if (! flush_scan_goto (scan, child)) {
			break;
		}

		flush_scan_set_current (scan, child, 1);

		if (jnode_is_formatted (child)) {
			break;
		}
	}

	done_lh (& scan->parent_lock);

	return 0;
}

/* Performs left- or rightward scanning starting from a formatted node. */
static int flush_scan_formatted (flush_scan *scan)
{
	/* Follow left pointers under tree lock as long as:
	 *
	 * - node->left/right is non-NULL
	 * - node->left/right is connected, dirty
	 * - node->left/right belongs to the same atom
	 * - scan has not reached maximum size
	 */
	znode *neighbor;

	assert ("jmacd-1401", ! flush_scan_finished (scan));

	do {
		/* Node should be connected. */
		znode *node = JZNODE (scan->node);

		assert ("jmacd-1402", znode_is_connected (node));

		/* Lock the tree, check & reference left sibling. */
		spin_lock_tree (current_tree);

		/* It may be that a node is inserted or removed between a node
		 * and its left sibling while the tree lock is released, but
		 * the left boundary does not need to be precise. */
		if ((neighbor = flush_scanning_left (scan) ? node->left : node->right) != NULL) {
			zref (neighbor);
		}

		spin_unlock_tree (current_tree);

		/* If left is NULL, need to continue using parent. */
		if (neighbor == NULL) {
			break;
		}

		/* Check the condition for going left, break if it is not met,
		 * release left reference. */
		if (! flush_scan_goto (scan, ZJNODE (neighbor))) {
			break;
		}

		/* Advance the flush_scan state to the left. */
		flush_scan_set_current (scan, ZJNODE (neighbor), 1);

	} while (! flush_scan_finished (scan));

	/* If neighbor is NULL then we reached the end of a formatted region, or else the
	 * sibling is out of memory, now check for an extent to the left (as long as
	 * LEAF_LEVEL). */
	if (neighbor == NULL && jnode_get_level (scan->node) == LEAF_LEVEL && ! flush_scan_finished (scan)) {
		return flush_scan_extent (scan, 1);
	}

	scan->stop = 1;
	return 0;
}

/* Performs leftward scanning starting from either kind of node.  Counts the starting node. */
static int flush_scan_left (flush_scan *scan, jnode *node)
{
	scan->max_size  = REISER4_FLUSH_SCAN_MAXNODES;
	scan->direction = LEFT_SIDE;

	flush_scan_set_current (scan, jref (node), 1);

	return flush_scan_common (scan);
}

/* Performs rightward scanning... Does not count the starting node. */
static int flush_scan_right_upto (flush_scan *scan, jnode *node, __u32 *res_count, __u32 limit)
{
	int ret;

	scan->max_size  = limit;
	scan->direction = RIGHT_SIDE;

	flush_scan_set_current (scan, jref (node), 0);

	if ((ret = flush_scan_common (scan)) == 0) {
		(*res_count) = scan->size;
	}

	return ret;
}

/* Performs left or right scanning. */
static int flush_scan_common (flush_scan *scan)
{
	int ret;

	/* Continue until we've scanned far enough. */
	do {
		/* Choose the appropriate scan method and go. */
		if (jnode_is_unformatted (scan->node)) {
			ret = flush_scan_extent (scan, 0);
		} else {
			ret = flush_scan_formatted (scan);
		}

		if (ret != 0) {
			return ret;
		}

	} while (ret == 0 && ! flush_scan_finished (scan));

	return ret;
}

/********************************************************************************
 * FLUSH POS HELPERS
 ********************************************************************************/

/* Initialize the fields of a flush_position. */
static int flush_pos_init (flush_position *pos)
{
	pos->point = NULL;
	pos->left_scan_count = 0;
	pos->right_scan_count = 0;
	pos->batch_relocate = 0;
	pos->parent_first_broken = 0;
	pos->squalloc_count = 0;

	blocknr_hint_init (& pos->preceder);
	init_lh (& pos->point_lock);
	init_lh (& pos->parent_lock);

	if ((pos->bio = bio_alloc (GFP_NOFS, FLUSH_RELOCATE_THRESHOLD)) == NULL) {
		return -ENOMEM;
	}

	return 0;
}

/* Release any resources of a flush_position. */
static void flush_pos_done (flush_position *pos)
{
	flush_pos_stop (pos);
	blocknr_hint_done (& pos->preceder);

	if (pos->bio != NULL) {
		bio_put (pos->bio);
		pos->bio = NULL;
	}
}

/* Reset the point and parent. */
static void flush_pos_stop (flush_position *pos)
{
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_lh (& pos->point_lock);
	done_lh (& pos->parent_lock);
}

/* FIXME: comments. */
static int flush_pos_to_child (flush_position *pos)
{
	int ret;
	jnode *child;

	assert ("jmacd-6078", flush_pos_unformatted (pos));

	/* Get the child if it is memory, lock it, unlock the parent. */
	if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child))) {
		return ret;
	}

	if (child == NULL) {
		flush_pos_stop (pos);
		return 0;
	}

	assert ("jmacd-8861", jnode_is_formatted (child));

	if ((ret = longterm_lock_znode (& pos->point_lock, JZNODE (child), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
		return ret;
	}

	/* And keep going... */
	done_lh (& pos->parent_lock);
	pos->point = jref (child);
	return 0;
}

/* FIXME: comments. */
static int flush_pos_to_parent (flush_position *pos)
{
	int ret;

	/* Lock the parent, find the coordinate. */
	if ((ret = jnode_lock_parent_coord (pos->point, & pos->parent_coord, & pos->parent_lock, ZNODE_READ_LOCK))) {
		return ret;
	}

	/* When this is called, we have already tried the sibling link of the znode in
	 * question, therefore we are not interested in saving ->point. */
	done_lh (& pos->point_lock);
	jput (pos->point);
	pos->point = NULL;
	return 0;
}

static int flush_pos_valid (flush_position *pos)
{
	/* FIXME: more asserts. */
	return pos->point != NULL || pos->parent_lock.node != NULL;
}

static int flush_pos_unformatted (flush_position *pos)
{
	/* FIXME: more asserts. */
	return pos->point == NULL;
}

static void flush_pos_release_point (flush_position *pos)
{
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
}

static void flush_pos_set_point (flush_position *pos, jnode *node)
{
	flush_pos_release_point (pos);
	pos->point = jref (node);
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
