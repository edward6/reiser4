/* The design document for this file is in flush.alg.  It needs to be rewritten and put on the web. */

/* Note on abbreviation: "squeeze and allocate" == "squalloc" */

#include "reiser4.h"

typedef struct flush_scan flush_scan;

/* The flush_scan data structure maintains the state of an in-progress flush scan.  FIXME: comment */
struct flush_scan {
	/* The current number of nodes on this level. */
	unsigned  size;

	/* True if some condition stops the search (e.g., we found a clean
	 * node before reaching max size). */
	int       stop;

	/* The current scan position. */
	jnode    *node;
};

static void          flush_scan_init               (flush_scan *scan);
static void          flush_scan_cleanup            (flush_scan *scan);
static int           flush_scan_left_finished      (flush_scan *scan);
static int           flush_scan_left_extent        (flush_scan *scan, jnode *node);
static int           flush_scan_left_formatted     (flush_scan *scan, znode *node);
static void          flush_scan_set_current        (flush_scan *scan, jnode *node);
static int           flush_scan_left               (flush_scan *scan, jnode *node);

static int           flush_preceder_hint          (jnode *gda, const tree_coord *parent_coord, reiser4_blocknr_hint *preceder);
static int           flush_preceder_rightmost     (const tree_coord *parent_coord, reiser4_blocknr_hint *preceder);
static int           flush_should_relocate        (jnode *node, const tree_coord *parent_coord, unsigned long blocks_dirty_at_this_level);

static int           flush_lock_greatest_dirty_ancestor (jnode                *start,
							 lock_handle  *start_lock,
							 jnode               **gda,
							 lock_handle  *gda_lock,
							 reiser4_blocknr_hint *preceder);

static int           squalloc_parent_first              (jnode *gda, reiser4_blocknr_hint *preceder);
static int           squalloc_children                  (znode *node, reiser4_blocknr_hint *preceder);
/*static*/ int       squalloc_right_neighbor            (znode *left, znode *right, reiser4_blocknr_hint *preceder);
static int           squeeze_leaves                     (znode *right, znode *left);
static int           squalloc_twig                      (znode *left, znode *right, reiser4_blocknr_hint *preceder);
static int           squalloc_twig_cut_copied           (tree_coord * to, reiser4_key * to_key);
static int           shift_one_internal_unit            (znode *left, znode *right);

static int           jnode_lock_parent_coord      (jnode *node,
						   tree_coord *coord,
						   lock_handle *parent_lh,
						   znode_lock_mode mode);
static jnode*        jnode_get_neighbor_in_memory (jnode *node, unsigned long node_index);
static int           jnode_is_allocated           (jnode *node);
static int           jnode_allocate_flush         (jnode *node, reiser4_blocknr_hint *preceder);

/* FIXME: Explain who calls....

 * This is the main entry point for flushing a jnode.  Two basic steps are
 * performed: first the "leftpoint" of the input jnode is
 * located, then the FIXME ()X)(X)() rooted at that ancestor is squeezed and
 * allocated in a parent-first traversal.  During squeeze and allocate, nodes
 * are scheduled for writeback and their jnodes are set to the "clean" state
 * as far as the atom is concerned.
 */
int jnode_flush (jnode *node)
{
	int ret;
	jnode *gda;                    /* gda == greatest dirty ancestor, jref'd when set. */
	lock_handle  gda_lock; /* if gda is formatted, a write lock */
	reiser4_blocknr_hint preceder; /* hint for block allocation */

	assert ("jmacd-5012", jnode_is_dirty (node));

	/* If the node has already been through the allocate process, we have
	 * decided whether it is to be relocated or overwritten (or if it was
	 * created, it has its initial allocation). */
	if (jnode_is_allocated (node)) {
		/* FIXME: just like in jnode_allocate_flush(). */
		jnode_set_clean (node);
		return 0;
	}

	gda = NULL;
	preceder.blk = 0;
	init_lh (& gda_lock);

	/* Locate the greatest dirty ancestor of the node to flush, which
	 * found by recursing upward as long as the parent is dirty and
	 * leftward along each level as long as the left neighbor is dirty. */
	if ((ret = flush_lock_greatest_dirty_ancestor (node, NULL, & gda, & gda_lock, & preceder))) {
		goto failed;
	}

	/* Squeeze and allocate in parent-first order, scheduling writes and
	 * cleaning jnodes.  The algorithm is described in more detail
	 * below. */
	if ((ret = squalloc_parent_first (gda, & preceder))) {
		goto failed;
	}

   failed:

	if (gda != NULL) {
		jput (gda);
	}
	done_lh (& gda_lock);
	return ret;
}

/********************************************************************************
 * FLUSH_LOCK_GREATEST_DIRTY_ANCESTOR
 ********************************************************************************/

/* FIXME: Call GDA the "leftpoint".  That which we are going to allocate first. */

/* This function is called on @start_node, which is where the search for the
 * greatest dirty ancestor begins.  This procedure is recursive (up the tree),
 * although it could be re-written less clearly using an iterative algorithm.
 * The proceedure is fairly straight-forward, but is complicated by
 * maintaining as few locks as possible at any given time.
 *
 * If the @start_node is passed in locked (it may not be), the lock will be
 * released as soon as it becomes unnecessary.
 *
 * A scan_left operation is performed starting from @start_node, which
 * proceeds to the left until a clean node is found.  The @end_node is found
 * at the end of this leftward scan.  If the @end_node is formatted then a
 * read-lock is acquired on @end_node, except in the case where @start_node is
 * the same as @end_node and @start_node was already locked, in which case the
 * lock would be redundent.
 *
 * Once the @end_node is locked, the parent is read-locked.  If at the root
 * level, get_parent_lock returns the above root node; in that case stop
 * recursion.
 *
 * With both the child (end) node and the parent node locked, decide whether
 * to relocate the child node.  If the child node will be relocated, then
 * dirty the parent (because child node's location will change).
 *
 * If the parent is dirty, recursively repeat process (at the parent level)
 * until the leftmost, higest, dirty node is found, which we call the greatest
 * dirty ancestor.  Returns with only the greatest dirty ancestor locked (and
 * referenced), unless the node is unformatted, in which case only a reference
 * is returned.
 */
static int flush_lock_greatest_dirty_ancestor (jnode                *start_node,
					       lock_handle  *start_lock,
					       jnode               **gda,
					       lock_handle  *gda_lock,
					       reiser4_blocknr_hint *preceder)
{
	int ret;
	jnode *end_node;
	znode *parent_node;
	flush_scan level_scan;
	tree_coord parent_coord;
	lock_handle end_lock;
	lock_handle parent_lock;
	assert ("jmacd-5013", jnode_is_dirty (start_node));
	assert ("jmacd-5014", ! jnode_is_allocated (start_node));

	flush_scan_init (& level_scan);
	init_lh        (& parent_lock);
	init_lh        (& end_lock);

	/* Scan start_node's level for the leftmost dirty neighbor. */
	if ((ret = flush_scan_left (& level_scan, start_node))) {
		goto failure;
	}

	end_node = level_scan.node;

	assert ("jmacd-5015", jnode_is_dirty (end_node));
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

	/* Read lock the parent.  If this node is the root, we'll get the
	 * above_root node back.  This may cause atom fusion. */
	if ((ret = jnode_lock_parent_coord (end_node, & parent_coord, & parent_lock, ZNODE_READ_LOCK))) {
		goto failure;
	}

	parent_node = parent_lock.node;

	/* This policy should be a plugin.  I am concerned that breaking these
	 * things into plugins will result in duplicated effort.  Returns 0
	 * for no-relocate, 1 for relocate, < 0 for error. */
	if ((ret = flush_should_relocate (end_node, & parent_coord, level_scan.size)) < 0) {
		goto failure;
	}

	/* If relocating the child, artificially dirty the parent right now.
	 * The ZNODE_RELOC/WANDER bit is set for the child at the left end of
	 * a region.  In the squalloc traversal below we will use this value
	 * to infer the status of its neighbors. */
	if (ret == 1) {
		JF_SET (end_node, ZNODE_RELOC);
		znode_set_dirty (parent_node);
	} else {
		JF_SET (end_node, ZNODE_WANDER);
	}

	/* If the parent is dirty, it needs to be squeezed also, recurse upwards */
	if (znode_is_dirty (parent_node)) {

		/* Release lock at this level before going upward. */
		done_lh (start_lock ? start_lock : & end_lock);
 
		/* Recurse upwards. */
		if ((ret = flush_lock_greatest_dirty_ancestor (ZJNODE (parent_node), & parent_lock, gda, gda_lock, preceder))) {
			goto failure;
		}

	} else {
		/* End the recursion, get a write lock at the highest level. */

		/* The preceder hint may be set at this point without taking
		 * any additional locks (if GDA is not allocated).  If it is
		 * possible, do so, otherwise the preceder_hint is initialized
		 * when the first unallocated node is encountered in the
		 * sqalloc_parent_first traversal. */
		if ((ret = flush_preceder_hint (end_node, & parent_coord, preceder))) {
			goto failure;
		}

		(*gda) = jref (end_node);

		/* Release any locks we might hold first, they are all read
		 * locks on this level, and parent lock is not needed. */
		done_lh (& parent_lock);
		done_lh (start_lock ? start_lock : & end_lock);

		/* We aren't holding any other locks (I think), so make it HIPRI. */
		if (jnode_is_formatted (end_node) &&
		    (ret = longterm_lock_znode (gda_lock, JZNODE (end_node), ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI))) {
			jput (end_node);
			goto failure;
		}
	}

	assert ("jmacd-2030", ret == 0);
 failure:
	done_lh           (& parent_lock);
	done_lh           (& end_lock);
	flush_scan_cleanup (& level_scan);

	return ret;
}

/********************************************************************************
 * (RE-) LOCATION POLICIES
 ********************************************************************************/

/* This relocation policy is:
 *
 *   (leftmost_of_parent && (is_leaf || leftmost_child_is_relocated))
 *
 * This may become a plugin.  Also, do not relocate the root node.
 *
 * There may be several options that dicate to always relocate, such as
 * "let_repacker_optimize" or "read_4_or_5_optimize".
 *
 * The blocks_dirty_at_this_level parameter may also be used (if > 1MB,
 * relocate), but it just a lower bound, since we did not scan to the right.
 *
 * Also want to add this: if the current block == the hint then it is in the
 * optimal position don't relocate.  (The current hint is write-optimized,
 * thus not set correctly for this "optimality" concern, see comment in
 * preceder_hint below).
 */
static int flush_should_relocate (jnode *node, const tree_coord *parent_coord, unsigned long blocks_dirty_at_this_level UNUSED_ARG)
{
	int ret;
	int is_leftmost;
	common_item_plugin *iplug;
	int is_dirty;
	jnode *left_child;
	znode *parent = parent_coord->node;
	tree_coord coord;

	/* Don't relocate the root node. */
	if (znode_above_root (parent)) {
		return 0;
	}

	dup_coord (& coord, parent_coord);

	/* If coord_prev_unit returns 1, its the leftmost coord of this node,
	 * otherwise... */
	if (! (is_leftmost = coord_prev_unit (& coord))) {
		/* Not leftmost of parent, don't relocate */
		return 0;
	}

	/* If at the leaf level... */
	if (jnode_get_level (node) == LEAF_LEVEL) {
		/* Leftmost leaf, relocate */
		return 1;
	}

	/* Reset coord to the leftmost child of parent, get its plugin. */
	coord_first_unit (& coord, parent);

	iplug = item_plugin_by_coord (& coord);

	assert ("jmacd-2049", iplug->utmost_child_dirty != NULL);

	/* Ask the item: is your left child dirty? */
	if ((ret = iplug->utmost_child_dirty (& coord, LEFT_SIDE, & is_dirty))) {
		assert ("jmacd-2051", ret < 0);
		return ret;
	}

	return is_dirty;
}

/* Called to initialize a hint for block allocation to begin from.  If the
 * preceder was previously initialized, this call does nothing.  Should set
 * the "preceder" block to a block location left of (i.e., preceding) the
 * block to be allocated (GDA).
 *
 * If the parent coord is passed in, then this is being called while the
 * parent is still locked.  Otherwise, this routine takes the parent lock and
 * possibly more (see below).
 *
 * This may become a plugin.
 *
 * If the GDA is a non-leftmost leaf node, return its left neighbhor's block
 * number.  This can be computed whether the left neighbor is in memory or
 * not.
 *
 * If the GDA is a leftmost node, return its parent block number, which is
 * definetly available.
 *
 * If the GDA is an non-leftmost internal node, its preceder is its left
 * neighbor's rightmost descendant, but these nodes are not required to be in
 * memory.  (Further, we're heading in the lo-priority direction.)
 *
 * If they are not in memory, use the rightmost descendant's block number that
 * is in memory.
 *
 * The current code sets the preceder hint for write-optimization, but this
 * could be changed.  Instead of returning if the preceder is already
 * initialized, compute a new value each time based on this algorithm.  The
 * current code uses the same hint for all allocations in a sub-tree rooted at
 * the GDA.
 */
static int flush_preceder_hint (jnode *node,
				const tree_coord *parent_coord,
				reiser4_blocknr_hint *preceder)
{
	int ret;
	int is_leftmost;
	int is_leaf;
	znode *parent;
	tree_coord coord;
	common_item_plugin *iplug;
	lock_handle parent_lock;
	/* If the current node is already allocated, don't set the hint yet. */
	if (jnode_is_allocated (node)) {
		return 0;
	}

	/* If the preceder is already initialized, return. */
	if (preceder->blk != 0) {
		/* FIXME: This is write-optimized.  Add another option? */
		return 0;
	}

	init_lh (& parent_lock);

	/* If the parent coord is set, we already have the parent locked. */
	if (parent_coord != NULL) {
		parent = parent_coord->node;
		dup_coord (& coord, parent_coord);
	} else {
		/* Otherwise, lock the parent, get the coordinate.  This may
		 * cause atom fusion (if the parent is already dirty), which
		 * is unnecessary. */
		if ((ret = jnode_lock_parent_coord (node, & coord, & parent_lock, ZNODE_READ_LOCK))) {
			return ret;
		}
	}

	/* Node can't be the root node here, since it is always allocated. */
	assert ("jmacd-2300", ! znode_above_root (parent));

	/* If coord_prev_unit returns 1, its the leftmost coord of this node. */
	is_leftmost = coord_prev_unit (& coord);
	is_leaf     = jnode_get_level (node) == LEAF_LEVEL;

	/* Non-lefmost leaf case: set left-of-node block number */
	if (is_leaf && ! is_leftmost) {
		
		iplug = item_plugin_by_coord (& coord);

		assert ("jmacd-2040", iplug->utmost_child_real_block != NULL);

		/* Get the rightmost block number of this coord, which is the
		 * child to the left of node.  If the block is unallocated or a
		 * hole, preceder is set to 0. */
		ret = iplug->utmost_child_real_block (& coord, RIGHT_SIDE, & preceder->blk);

	} else if (is_leftmost) {
		/* Leftmost case: parent must be allocated or else we would
		 * have set the hint at a higher level. */

		assert ("jmacd-2041", jnode_is_allocated (ZJNODE (parent)));

		preceder->blk = *znode_get_block (parent);
		ret = 0;

	} else {

		/* Non-leftmost internal node case. */
		ret = flush_preceder_rightmost (& coord, preceder);
	}

	done_lh (& parent_lock);
	return ret;
}

/* Find the rightmost descendant block number of @node.  Set preceder->blk to
 * that value.  This can never be called on a leaf node, the leaf case is
 * handled by flush_preceder_hint. */
static int flush_preceder_rightmost (const tree_coord *parent_coord, reiser4_blocknr_hint *preceder)
{
	int ret;
	znode *parent = parent_coord->node;
	common_item_plugin *iplug;
	jnode *child;
	tree_coord child_coord;

	iplug = item_plugin_by_coord (parent_coord);

	assert ("jmacd-2043", znode_get_level (parent) >= TWIG_LEVEL);

	if (znode_get_level (parent) == TWIG_LEVEL) {
		/* End recursion, we made it all the way. */

		assert ("jmacd-2042", iplug->utmost_child_real_block != NULL);

		/* Get the rightmost block number of this coord, which is the
		 * child to the left of the starting node.  If the block is a
		 * unallocated or a hole, the preceder is set to 0. */
		return iplug->utmost_child_real_block (parent_coord, RIGHT_SIDE, & preceder->blk);
	}

	/* Recurse downwards case: ABOVE TWIG LEVEL. */
	assert ("jmacd-2045", iplug->utmost_child != NULL);

	/* Get the child if it is in memory. */
	if ((ret = iplug->utmost_child (parent_coord, RIGHT_SIDE, & child))) {
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
		coord_last_unit (& child_coord, JZNODE (child));

		ret = flush_preceder_rightmost (& child_coord, preceder);
	}

	jput (child);
	return ret;
}

/********************************************************************************
 * SQUEEZE AND ALLOCATE
 ********************************************************************************/

/* Squeeze and allocate, parent first -- This is initially called on the
 * greatest dirty ancestor of a subtree to flush, then it recurses downward
 * over that subtree.
 *
 * If the node is not dirty or is unformatted, halt recursion.  An unformatted
 * node will only be reached if its parent is clean (i.e, it was its own
 * greatest dirty ancestor).
 *
 * If the node is allocated, skip it.  Otherwise, allocate it.
 *
 * Once the down-recursive case has been applied, see if the right neighbor is
 * dirty and if so, attempt to squeeze it into this node.  If the item being
 * squeezed from left to right is and internal node, then the right-squeezing
 * halts while the shifted subtree is recursively processed.  Once the
 * shifted-right subtree is squeezed and allocated, squeezing from the left
 * continues.
 */
static int squalloc_parent_first (jnode *node, reiser4_blocknr_hint *preceder)
{
	int ret, goright;
	znode *right;
	lock_handle right_lock;
	int squeeze;

        /* Stop recursion if its not dirty, meaning don't allocate children
         * either.  Children might be dirty but there is an overwrite below
         * this level or else this node would be dirty. */
        if (! jnode_is_dirty (node)) {
                return 0;
        }

	/* We got here because the unformatted node is not being relocated.
	 * Otherwise the parent would be dirty (and this recursive function
	 * does not descend to unformatted nodes).  Since the node is in the
	 * overwrite set, there's no allocation to do. */
	if (jnode_is_unformatted (node)) {
		return 0;
	}

	assert ("jmacd-1050", ! node_is_empty (JZNODE (node)));
	assert ("jmacd-1051", znode_is_write_locked (JZNODE (node)));

	/* If the node is already allocated, skip it. */
        if (jnode_is_allocated (node)) {
		return 0;
	}

	/* Set the preceder hint, if not already done. */
	if ((ret = flush_preceder_hint (node, NULL, preceder))) { return ret; }

	/* Allocate it, parent first. */
	if ((ret = jnode_allocate_flush (node, preceder))) { return ret; }

	/* Recursive case: handles all children. */
	if ((jnode_get_level (node) > LEAF_LEVEL) &&
	    (ret = squalloc_children (JZNODE (node), preceder))) {
		return ret;
	}

        /* Now @node and all its children (recursively) are squeezed and
	 * allocated.  Next, we will squeeze this node's right neighbor into
	 * this one, allocating as we go.  First, we must check that the right
	 * node is in the same atom without requesting a long term lock.
	 * (Because the long term lock request will FORCE it into the same
	 * atom...).  The "again" label is used to repeat the following steps,
	 * as long as node's right neighbor is squeezed into this one. */

 again:
	spin_lock_tree (current_tree);
	right = zref (JZNODE (node)->right);
	spin_unlock_tree (current_tree);

	goright = txn_same_atom_dirty (node, ZJNODE (right));

	if (! goright) {
		return 0;
	}

	/* Get long term lock on right neighbor. */
	init_lh (& right_lock);

	if ((ret = reiser4_get_right_neighbor (& right_lock, JZNODE (node), ZNODE_WRITE_LOCK, 0))) {
		goto cleanup;
	}

	/* Can't assert is_dirty here, even though we checked it above,
	 * because there is a race when the tree_lock is released. */
        if (! znode_is_dirty (right_lock.node)) {
		ret = 0;
		goto cleanup;
	}

	assert ("jmacd-1052", ! node_is_empty (right_lock.node));

	/* Squeeze from the right until an internal node is shifted.  At that
	 * point, we recurse downwards and squeeze its children.  When the
	 * recursive case is finished, continue squeezing at this level. */
	while ((squeeze = squalloc_right_neighbor (JZNODE (node), right_lock.node, preceder)) == SUBTREE_MOVED) {
		/* Unit of internal item has been shifted, now allocate and
		 * squeeze that subtree (now the last item of this node). */
		tree_coord crd;
		znode *child;
		
		init_coord (& crd);
		coord_last_unit (& crd, JZNODE (node));

		assert ("vs-442", item_plugin_by_coord (&crd)->down_link);

		child = child_znode (& crd, 1/*set delim key*/);
		if (IS_ERR (child)) { return PTR_ERR (child); }

		/* Skipping this node if it is dirty means that we won't
		 * squeeze and allocate any of its dirty grand-children.  Oh
		 * well! */
		if (! znode_is_dirty (child)) { continue; }

		/* Recursive case: single formatted child. */
		if ((ret = squalloc_parent_first (ZJNODE (child), preceder))) {
			return ret;
		}
	}

	if (squeeze == SQUEEZE_SOURCE_EMPTY) {
		/* Right neighbor was squeezed completely into @node, try to
		 * squeeze with the new right neighbor.  Release the
		 * right_lock first. */
		assert ("vs-444", node_is_empty (right_lock.node));
		done_lh (& right_lock);
		goto again;
	}

	/* Either an error occured or SQUEEZE_TARGET_FULL (or
	 * can't-squeeze-right, e.g., end-of-level or
	 * unformatted-to-the-right) */ 
	assert ("vs-443", squeeze == SQUEEZE_TARGET_FULL || squeeze < 0);
	ret = ((squeeze < 0) ? squeeze : 0);

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
static int squalloc_children (znode *node, reiser4_blocknr_hint *preceder)
{
	int ret;
	tree_coord crd;

	coord_first_unit (& crd, node);

	/* Assert the pre-condition for the following do-loop, essentially
	 * stating that the node is not empty. */
	assert ("jmacd-2000", ! coord_after_last (& crd));

	/* Do ... while not the last unit. */
	do {
		if (item_plugin_by_coord (&crd)->item_plugin_id == EXTENT_POINTER_ID) {
			if ((ret = allocate_extent_item_in_place (&crd, preceder))) {
				return ret;
			}
		} else if (item_plugin_by_coord (&crd)->down_link) {
			/* Get the child of this node pointer, check for
			 * error, skip if it is not dirty. */
			znode *child = child_znode (& crd, 1);

			if (IS_ERR (child)) { return PTR_ERR (child); }

			if (! znode_is_dirty (child)) { continue; }

			/* Recursive call. */
			if ((ret = squalloc_parent_first (ZJNODE (child), preceder))) {
				return ret;
			}
		} else {
			warning ("jmacd-2001", "Unexpected item type above leaf level");
			print_znode ("node", node);
			return -EIO;
		}

	} while (! coord_next_unit (& crd));

	return 0;
}


/* Squeeze and allocate the right neighbor.  This is called after @left and
 * its current children have been squeezed and allocated already.  This
 * proceedure's job is to squeeze and items from @right to @left.
 *
 * If at the leaf level, use the squeeze_everything_left memcpy-optimized
 * version of shifting (squeeze_leaves).
 *
 * If at the twig level, extents are allocated as they are shifted from @right
 * to @left (squalloc_twig).
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
		/* This is possible.  FIXME: VS: Explain? */
		return SQUEEZE_SOURCE_EMPTY;
	}

	switch (znode_get_level (left)) {
	case LEAF_LEVEL:
		/* Use the memcpy-optimzied shift. */
		ret = squeeze_leaves (left, right);
		break;

	case TWIG_LEVEL:
		/* Shift with extent allocating until either an internal item
		 * is encountered or everything is shifted or no free space
		 * left in @left */
		ret = squalloc_twig (left, right, preceder);
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
static int squeeze_leaves (znode * right, znode * left)
{
	int ret;
	carry_pool pool;
	carry_level todo;

	init_carry_pool (& pool);
	init_carry_level (& todo, & pool);
	
	ret = shift_everything_left (right, left, & todo);

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
 * unallocated extents as they are copied.  Returns SQUEEZE_TARGET_FILL or
 * SQUEEZE_SOURCE_EMPTY when no more can be shifted.  If the next item is an
 * internal item it calls shift_one_internal_unit and may then return
 * SUBTREE_MOVED. */
static int squalloc_twig (znode    *left,
			  znode    *right,
			  reiser4_blocknr_hint *preceder)
{
	int ret;
	tree_coord coord;
	reiser4_key stop_key;

	assert ("jmacd-2008", ! node_is_empty (right));

	coord_first_unit (&coord, right);

	/* Initialize stop_key to detect if any extents are copied.  After
	 * this loop loop if stop_key is still equal to *min_key then nothing
	 * was copied (and there is nothing to cut). */
	stop_key = *min_key ();

	while (item_plugin_by_coord (&coord)->item_plugin_id == EXTENT_POINTER_ID) {

		if ((ret = allocate_and_copy_extent (left, &coord, preceder, &stop_key)) < 0) {
			return ret;
		}

		if (ret == SQUEEZE_TARGET_FULL) {
			/* Could not complete with current extent item. */
			break;
		}

		assert ("jmacd-2009", ret == SQUEEZE_CONTINUE);

		if (! coord_next_item (&coord)) {
			ret = SQUEEZE_SOURCE_EMPTY;
			break;
		}
	}

	if (keycmp (&stop_key, min_key ()) != EQUAL_TO) {
		int cut_ret;

		/* @coord is set to the first unit that does not have to be
		 * cut or after last item in the node.  If we are positioned
		 * at the coord of a unit, it means the extent processing
		 * stoped in the middle of an extent item, the last unit of
		 * which was not copied.  Cut everything before that point. */
		if (coord_of_unit (& coord)) {
			coord_prev_unit (& coord);
		}

		/* Helper function to do the cutting. */
		if ((cut_ret = squalloc_twig_cut_copied (&coord, &stop_key))) {
			return cut_ret;
		}
	}

	if (node_is_empty (right)) {
		/* The whole right node was copied into @left. */
		assert ("vs-464", ret == SQUEEZE_SOURCE_EMPTY);
		return ret;
	}

	coord_first_unit (&coord, right);

	if (! item_plugin_by_coord (&coord)->down_link) {
		/* There is no space in @left anymore. */
		assert ("vs-433",
			item_plugin_by_coord (&coord)->item_plugin_id ==
			EXTENT_POINTER_ID);
		assert ("vs-465", ret == SQUEEZE_TARGET_FULL);
		return ret;
	}

	return shift_one_internal_unit (left, right);
}

/* Squalloc_twig helper function, cut a range of extent items from
 * cut node to->node from the beginning up to coord @to. */
static int squalloc_twig_cut_copied (tree_coord * to, reiser4_key * to_key)
{
	tree_coord from;
	reiser4_key from_key;

	coord_first_unit (&from, to->node);
	item_key_by_coord (&from, &from_key);

	return cut_node (&from, to, &from_key, to_key, NULL /* smallest_removed */, DELETE_DONT_COMPACT);
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
	tree_coord coord;
	int size, moved;


	coord_first_unit (&coord, right);

	assert ("jmacd-2007", item_plugin_by_coord (&coord)->down_link);

	init_carry_pool (&pool);
	init_carry_level (&todo, &pool);

	size = item_length_by_coord (&coord);
	ret  = node_plugin_by_node (left)->shift (&coord, left, SHIFT_LEFT,
						  1/* delete @right if it becomes empty*/,
						  0/* move coord */,
						  &todo);

	/* If shift returns positive, then we shifted the item. */
	assert ("vs-423", ret <= 0 || size == ret);
	moved = (ret > 0);

	if (ret > 0) {
		/* Carry is called to update delimiting key or to remove empty
		 * node. */
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
 * JNODE INTERFACE
 ********************************************************************************/

/* Gets the sibling of an unformatted jnode using its index, only if it is in
 * memory, and reference it. */
static jnode* jnode_get_neighbor_in_memory (jnode *node, unsigned long node_index)
{
	struct page *pg;

	pg = find_lock_page (node->pg->mapping, node_index);

	if (pg == NULL) {
		return NULL;
	}

	assert ("jmacd-1700", ! IS_ERR (pg));
	
	return jnode_of_page (pg);
}

/* Return true if @node has already been processed by the squeeze and allocate
 * process.  This implies the block address has been finalized for the
 * duration of this atom (or it is clean and will remain in place).  If this
 * returns true you may use the block number as a hint. */
static int jnode_is_allocated (jnode *node)
{
	return /*JF_ISSET (node, ZNODE_RELOC | ZNODE_WANDER) ||*/
		! blocknr_is_fake (jnode_get_block (node)));
}

/* Allocate a block number and flush. */
static int jnode_allocate_flush (jnode *node, reiser4_blocknr_hint *preceder)
{
	int ret;
	int len;
	reiser4_block_nr blk;

	/* FIXME: Need to set RELOC/WANDER bits. */
	   
	/* FIXME_ZAM: Interface issues... */
	if (0 && (ret = reiser4_alloc_blocks (preceder, & blk, & len))) {
		return ret;
	}

	/* Now the node has been allocated.  FIXME: WRITE IT.  Set it
	 * clean. */
	jnode_set_clean (node);

	node->blocknr = blk;

	return 0;
}

/* Lock a node (if formatted) and then get its parent locked, set the child's
 * coordinate in the parent.  If the child is the root node, the above_root
 * znode is returned but the coord is not set.  This function may cause atom
 * fusion, but it is only used for read locks (at this point) and therefore
 * fusion only occurs when the parent is already dirty. */
static int
jnode_lock_parent_coord (jnode *node,
			 tree_coord *coord,
			 lock_handle *parent_lh,
			 znode_lock_mode parent_mode)
{
	int ret;

	assert ("jmacd-2060", jnode_is_unformatted (node) || znode_is_any_locked (JZNODE (node)));

	if (jnode_is_unformatted (node)) {

		/* Unformatted node case: Generate a key for the extent entry,
		 * search in the tree using coord_by_key, which handles
		 * locking for us. */
		struct inode *ino = node->pg->mapping->host;
		reiser4_key   key;
		file_plugin  *fplug = get_file_plugin (ino);
		loff_t        loff = node->pg->index << PAGE_CACHE_SHIFT;

		if ((ret = fplug->key_by_inode (ino, & loff, & key))) {
			return ret;
		}

		if ((ret = coord_by_key (current_tree, & key, coord, parent_lh, parent_mode, FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL, 0)) != CBK_COORD_FOUND) {
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
		if (! znode_above_root (parent_lh->node) &&
		    (ret = find_child_ptr (parent_lh->node, JZNODE (node), coord))) {
			return ret;
		}
	}

	return 0;
}

/********************************************************************************
 * FLUSH SCAN LEFT
 ********************************************************************************/

/* Initialize the flush_scan data structure. */
static void flush_scan_init (flush_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
}

/* Release any resources held by the flush scan, e.g., release locks, free memory, etc. */
static void flush_scan_cleanup (flush_scan *scan)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}
}

/* Returns true if leftward flush scanning is finished. */
static int flush_scan_left_finished (flush_scan *scan)
{
	return scan->stop || scan->size >= REISER4_FLUSH_SCAN_MAXNODES;
}

/* Return true if the scan should continue to the left.  Go left if the node
 * is not allocated, dirty, and in the same atom as the current scan position.
 * If not, deref the "left" node and stop the scan. */
static int flush_scan_goleft (flush_scan *scan, jnode *left)
{
	int goleft;

	goleft = ! jnode_is_allocated (left) && txn_same_atom_dirty (scan->node, left);

	if (! goleft) {
		jput (left);
		scan->stop = 1;
	}

	return goleft;
}

/* Set the current scan->node, refcount it, increment size, and deref previous current. */
static void flush_scan_set_current (flush_scan *scan, jnode *node)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}

	scan->size += 1;
	scan->node  = node;
}

/* Perform a single leftward step using the parent, finding the next-left item, and
 * descending.  Only used at the left-boundary of an extent or range of znodes. */
static int flush_scan_left_using_parent (flush_scan *scan)
{
	int ret;
	lock_handle node_lh, parent_lh, left_parent_lh;
	tree_coord coord;
	common_item_plugin *iplug;
	jnode *child_left;

	assert ("jmacd-1403", ! flush_scan_left_finished (scan));

	init_coord (& coord);
	init_lh    (& node_lh);
	init_lh    (& parent_lh);
	init_lh    (& left_parent_lh);

	/* Lock the node itself, which is necessary for getting its parent. */
	if (jnode_is_formatted (scan->node) &&
	    (ret = longterm_lock_znode (& node_lh, JZNODE (scan->node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
		goto done;
	}

	/* Since we have reached the end of a formatted or unformatted region
	 * we are going to read-lock the parent.  This may cause atom fusion
	 * (if the parent is already dirty). */
	if ((ret = jnode_lock_parent_coord (scan->node, & coord, & parent_lh, ZNODE_READ_LOCK))) {
		goto done;
	}

	/* Check root case. */
	if (znode_above_root (parent_lh.node)) {
		scan->stop = 1;
		ret = 0;
		goto done;
	}

	/* Finished with the node lock. */
	done_lh (& node_lh);

	/* Shift the coord to the left. */
	if ((ret = coord_prev_unit (& coord)) != 0) {
		/* If coord_prev returns 1, coord is already leftmost of its node. */

		/* Lock the left-of-parent node, but don't read into memory.
		 * ENOENT means not-in-memory.  This may also cause atom
		 * fusion, but in such case the regions were adjacent and so
		 * this makes sense. */
		if ((ret = reiser4_get_left_neighbor (& left_parent_lh, parent_lh.node, ZNODE_READ_LOCK, 0)) && (ret != -ENOENT)) {
			goto done;
		}

		if (ret == 0) {
			/* Release parent lock -- don't need it any more. */
			done_lh (& parent_lh);

			/* Set coord to the rightmost position of the left-of-parent node. */
			coord_last_unit (& coord, left_parent_lh.node);

			ret = 0;
		}
	}

	/* Get the item plugin. */
	iplug = item_plugin_by_coord (& coord);

	assert ("jmacd-2040", iplug->utmost_child != NULL);

	/* Get the rightmost child of this coord, which is the child to the left of the scan position. */
	if ((ret = iplug->utmost_child (& coord, RIGHT_SIDE, & child_left))) {
		goto done;
	}

	/* If the child is not in memory, stop. */
	if (child_left == NULL) {
		scan->stop = 1;
		ret = 0;
		goto done;
	}

	/* We've found a child to the left, now see if we should continue the scan.  If not then scan->stop is set. */
	if (flush_scan_goleft (scan, child_left)) {
		flush_scan_set_current (scan, child_left);
	}

	/* Release locks. */
 done:
	done_lh (& node_lh);
	done_lh (& parent_lh);
	done_lh (& left_parent_lh);

	return ret;
}

/* Performs leftward scanning starting from a formatted node */
static int flush_scan_left_formatted (flush_scan *scan, znode *node)
{
	/* Follow left pointers under tree lock as long as:
	 *
	 * - node->left is non-NULL
	 * - node->left is connected, dirty
	 * - node->left belongs to the same atom
	 * - scan has not reached maximum size
	 */
	znode *left;

	assert ("jmacd-1401", ! flush_scan_left_finished (scan));

	/*info_znode ("scan_left: ", node);*/

	do {
		/* Node should be connected. */
		assert ("jmacd-1402", znode_is_connected (node));

		/* Lock the tree, check & reference left sibling. */
		spin_lock_tree (current_tree);

		/* It may be that a node is inserted or removed between a node
		 * and its left sibling while the tree lock is released, but
		 * the left boundary does not need to be precise. */
		if ((left = node->left) != NULL) {
			zref (left);
		}

		spin_unlock_tree (current_tree);

		/* If left is NULL, need to continue using parent. */
		if (left == NULL) {
			break;
		}

		/* Check the condition for going left, break if it is not met,
		 * release left reference. */
		if (! flush_scan_goleft (scan, ZJNODE (left))) {
			break;
		}

		/* Advance the flush_scan state to the left. */
		flush_scan_set_current (scan, ZJNODE (left));

	} while (! flush_scan_left_finished (scan));

	/* If left is NULL then we reached the end of a formatted region, or else the
	 * sibling is out of memory, now check for an extent to the left (as long as
	 * LEAF_LEVEL). */
	if (left == NULL && znode_get_level (node) == LEAF_LEVEL && ! flush_scan_left_finished (scan)) {
		return flush_scan_left_using_parent (scan);
	}

	scan->stop = 1;
	return 0;
}

/* Performs leftward scanning starting from an unformatted node */
static int flush_scan_left_extent (flush_scan *scan, jnode *node)
{
	jnode *left;
	unsigned long scan_index;

	assert ("jmacd-1404", ! flush_scan_left_finished (scan));
	assert ("jmacd-1405", jnode_get_level (node) == LEAF_LEVEL);

	/* Starting at the index (i.e., block offset) of the jnode in its extent... */
	scan_index = jnode_get_index (node);

	while (scan_index > 0 && ! flush_scan_left_finished (scan)) {

		/* For each loop iteration, get the previous index. */
		left = jnode_get_neighbor_in_memory (node, --scan_index);

		/* If the left neighbor is not in memory... */
		if (left == NULL) {
			scan->stop = 1;
			break;
		}

		/* Check the condition for going left. */
		if (! flush_scan_goleft (scan, left)) {
			break;
		}

		/* Advance the flush_scan state to the left. */
		flush_scan_set_current (scan, left);
	}

	/* If we made it all the way to the beginning of the extent, check for another
	 * extent or znode to the left. */
	if (scan_index == 0 && ! flush_scan_left_finished (scan)) {
		return flush_scan_left_using_parent (scan);
	}

	scan->stop = 1;
	return 0;
}

/* Performs leftward scanning starting either kind of node */
static int flush_scan_left (flush_scan *scan, jnode *node)
{
	int ret;

	/* Reference and set the initial leftmost boundary. */
	flush_scan_set_current (scan, jref (node));

	/* Continue until we've scanned far enough. */
	do {
		/* Choose the appropriate scan method and go. */
		if (jnode_is_unformatted (node)) {
			ret = flush_scan_left_extent (scan, node);
		} else {
			ret = flush_scan_left_formatted (scan, JZNODE (node));
		}

		if (ret != 0) {
			return ret;
		}

	} while (ret == 0 && ! flush_scan_left_finished (scan));

	return ret;
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
