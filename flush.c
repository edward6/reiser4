/* Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* The design document for this file is at www.namesys.com/flush-alg.html.
 * Note on abbreviation: "squeeze and allocate" == "squalloc" */

#include "reiser4.h"

/* FIXME: Comments are out of date and missing in this file. */

/* FIXME: Nikita has written similar functions to these, should replace them with his. */
typedef struct load_handle load_handle;
struct load_handle { znode *node; };
static inline void init_zh (load_handle *zh) { zh->node = NULL; }
static inline void done_zh (load_handle *zh) { if (zh->node != NULL) { zrelse (zh->node); zh->node = NULL; } }
static inline int  load_zh_common (load_handle *zh, znode *node) { int ret; if ((ret = zload (node))) { return ret; } zh->node = node; return 0; }
static inline int  load_zh (load_handle *zh, znode *node) { done_zh (zh); return load_zh_common (zh, node); }
static inline int  load_jh (load_handle *zh, jnode *node) { done_zh (zh); return jnode_is_formatted (node) ? load_zh_common (zh, JZNODE (node)) : 0; }
static inline void move_zh (load_handle *new, load_handle *old) { done_zh (new); new->node = old->node; old->node = NULL; }
static inline void copy_zh (load_handle *new, load_handle *old) { done_zh (new); new->node = old->node; atomic_inc (& ZJNODE(old->node)->d_count); }

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

	/* A handle for zload/zrelse of current scan position node. */
	load_handle node_load;

	/* When the position is unformatted, its parent, coordinate, and parent
	 * zload/zrelse handle. */
	lock_handle parent_lock;
	coord_t     parent_coord;
	load_handle parent_load;

	/* The block allocation hint. */
	reiser4_block_nr preceder_blk;
};

/* An encapsulation of the current flush point and all the parameters that are passed
 * through the entire flush routine. */
struct flush_position {
	jnode                *point;
	lock_handle           point_lock;
	lock_handle           parent_lock;
	coord_t               parent_coord;
	load_handle           point_load;
	load_handle           parent_load;
	reiser4_blocknr_hint  preceder;
	int                   leaf_relocate;
	int                  *nr_to_flush;
	int                   alloc_cnt;
	int                   enqueue_cnt;
	jnode               **queue;
 	int                   queue_num;

	/* FIXME: Add a per-level bitmask, I think, or something to detect the first set
	 * of nodes that needs to be checked for having still-unallocated children?  How
	 * about the last set? */
};

typedef enum {
	SCAN_EVERY_LEVEL,
	SCAN_FIRST_LEVEL,
	SCAN_NEVER
} flush_scan_config;

static void          flush_scan_init              (flush_scan *scan);
static void          flush_scan_done              (flush_scan *scan);
static int           flush_scan_set_current       (flush_scan *scan, jnode *node, unsigned add_size, const coord_t *parent);
static int           flush_scan_common            (flush_scan *scan, flush_scan *other);
static int           flush_scan_finished          (flush_scan *scan);
static int           flush_scan_extent            (flush_scan *scan, int skip_first);
static int           flush_scan_formatted         (flush_scan *scan);
static int           flush_scan_left              (flush_scan *scan, flush_scan *right, jnode *node, __u32 limit);
static int           flush_scan_right             (flush_scan *scan, jnode *node, __u32 limit);

static int           flush_left_relocate_dirty    (jnode *node, const coord_t *parent_coord, flush_position *pos);

static int           flush_allocate_znode         (znode *node, coord_t *parent_coord, flush_position *pos);
static int           flush_release_znode          (znode *node);
static int           flush_rewrite_jnode          (jnode *node);
static int           flush_release_ancestors      (znode *node);
static int           flush_queue_jnode            (jnode *node, flush_position *pos);

static int           flush_finish                 (flush_position *pos, int nobusy);
/*static*/ int       squalloc_right_neighbor      (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig          (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig_cut      (coord_t * to, reiser4_key * to_key, znode *left);
static int           squeeze_right_non_twig       (znode *left, znode *right);
static int           shift_one_internal_unit      (znode *left, znode *right);

static int           flush_squeeze_left_edge      (flush_position *pos);
static int           flush_squalloc_right         (flush_position *pos);

static int           jnode_lock_parent_coord      (jnode *node,
						   coord_t *coord,
						   lock_handle *parent_lh,
						   load_handle *parent_zh,
						   znode_lock_mode mode);
static int           znode_get_utmost_if_dirty    (znode *node, lock_handle *right_lock, sideof side, znode_lock_mode mode);
static int           znode_same_parents           (znode *a, znode *b);

static int           flush_pos_init               (flush_position *pos, int *nr_to_flush);
static int           flush_pos_valid              (flush_position *pos);
static void          flush_pos_done               (flush_position *pos);
static int           flush_pos_stop               (flush_position *pos);
static int           flush_pos_unformatted        (flush_position *pos);
static int           flush_pos_to_child_and_alloc (flush_position *pos);
static int           flush_pos_to_parent          (flush_position *pos);
static int           flush_pos_set_point          (flush_position *pos, jnode *node);
static void          flush_pos_release_point      (flush_position *pos);
static int           flush_pos_lock_parent        (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, load_handle *parent_load, znode_lock_mode mode);

static const char*   flush_pos_tostring           (flush_position *pos);
static const char*   flush_jnode_tostring         (jnode *node);
static const char*   flush_znode_tostring         (znode *node);

static int           jnode_check_dirty_and_connected (jnode *node);


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
int jnode_flush (jnode *node, int *nr_to_flush, int flags UNUSED_ARG)
{
	int ret;
	flush_position flush_pos;
	flush_scan right_scan;
	flush_scan left_scan;

	if (0 /* To disable flush and keep the txnmgr happy, just set node clean. */) {
		jnode_set_clean (node);
		return 0;
	}

	trace_on (TRACE_FLUSH, "flush_jnode %s\n", flush_jnode_tostring (node));

	/* A race is possible where node is not dirty at this point. */
	if (! jnode_check_dirty_and_connected (node)) {
		if (nr_to_flush != NULL) {
			(*nr_to_flush) = 0;
		}
		trace_on (TRACE_FLUSH_VERB, "flush_jnode not dirty/connected\n");
		return 0;
	}

	if (jnode_check_allocated (node)) {
		/* Already has been assigned a block number, just write it again?  FIXME: Hans? */
		if ((ret = flush_rewrite_jnode (node))) {
			return ret;
		}

		if (nr_to_flush != NULL) {
			(*nr_to_flush) = 1;
		}

		trace_on (TRACE_FLUSH_VERB, "flush_jnode rewrite\n");
		return 0;
	}

	if ((ret = flush_pos_init (& flush_pos, nr_to_flush))) {
		return ret;
	}

	flush_scan_init (& right_scan);
	flush_scan_init (& left_scan);

	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_BRIEF));*/
	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_CHECK));*/

	/* First scan left and remember the leftmost position (and, if
	 * unformatted, its parent_coord). */
	if ((ret = flush_scan_left (& left_scan, & right_scan, node, REISER4_FLUSH_SCAN_MAXNODES))) {
		goto failed;
	}

	/* Then possibly go right to decide if we will relocate everything possible. */
	if ((left_scan.size < FLUSH_RELOCATE_THRESHOLD) &&
	    (ret = flush_scan_right (& right_scan, node, FLUSH_RELOCATE_THRESHOLD - left_scan.size))) {
		goto failed;
	}

	/* Only the count is needed, release right away. */
	flush_scan_done (& right_scan);

	/* ... and the answer is: */
	flush_pos.leaf_relocate = (left_scan.size + right_scan.size >= FLUSH_RELOCATE_THRESHOLD);

	assert ("jmacd-6218", jnode_check_dirty (left_scan.node));

	/* FIXME: Funny business here.  We set an unformatted point at the
	 * left-end of the scan, but after that an unformatted flush position sets
	 * pos->point to NULL.  This is awkward and may cause problems later.
	 * Think about it. */
	if ((ret = flush_pos_set_point (& flush_pos, left_scan.node))) {
		goto failed;
	}

	/* Now setup flush_pos using scan_left's endpoint. */
	if (jnode_is_unformatted (left_scan.node)) {
		coord_dup (& flush_pos.parent_coord, & left_scan.parent_coord);
		move_lh (& flush_pos.parent_lock, & left_scan.parent_lock);
		move_zh (& flush_pos.parent_load, & left_scan.parent_load);
	} else {
		if ((ret = longterm_lock_znode (& flush_pos.point_lock, JZNODE (left_scan.node), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
			/* EINVAL means the node was deleted, DEADLK should be impossible here. */
			assert ("jmacd-34113", ret != -EDEADLK);
			if (ret == -EINVAL) {
				ret = 0;
			}
			goto failed;
		}
	}

	/* In some cases, we discover the parent-first preceder during the
	 * leftward scan.  Copy it. */
	flush_pos.preceder.blk = left_scan.preceder_blk;
	flush_scan_done (& left_scan);

	/* At this point, try squeezing at the "left edge", meaning to possibly
	 * change the parent of the left end of the scan.  NOT IMPLEMENTED FUTURE
	 * OPTIMIZATION. */
	if ((ret = flush_squeeze_left_edge (& flush_pos))) {
		goto failed;
	}

	/* Do the rightward-bottom-up pass. */
	if ((ret = flush_squalloc_right (& flush_pos))) {
		goto failed;
	}

	/* Write anything left in the queue. */
	ret = flush_finish (& flush_pos, 1);

	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_CHECK));*/
   failed:

	//print_tree_rec ("parent_first", current_tree, REISER4_TREE_BRIEF);
	if (nr_to_flush != NULL) {
		if (ret == 0) {
			trace_on (TRACE_FLUSH, "flush_jnode wrote %u blocks\n", flush_pos.enqueue_cnt);
			(*nr_to_flush) = flush_pos.enqueue_cnt;
		} else {
			(*nr_to_flush) = 0;
		}
	}

	if (ret == -EINVAL || ret == -EDEADLK /* FIXME: What about -ENAVAIL, -ENOENT? */ || ret == -ENAVAIL || ret == -ENOENT) {
		/* FIXME: Something bad happened, but difficult to avoid...  Try again! */
		ret = 0;
	}

	if (ret != 0) {
		warning ("jmacd-16739", "flush failed: %d\n", ret);
	}

	flush_pos_done (& flush_pos);
	flush_scan_done (& left_scan);
	flush_scan_done (& right_scan);
	return ret;
}

static int jnode_check_dirty_and_connected( jnode *node )
{
	int is_dirty;
	assert( "jmacd-7798", node != NULL );
	assert( "jmacd-7799", spin_jnode_is_not_locked (node) );
	spin_lock_jnode (node);
	is_dirty = jnode_is_dirty (node) && (jnode_is_unformatted (node) || znode_is_connected (JZNODE (node)));
	spin_unlock_jnode (node);
	return is_dirty;
}

/********************************************************************************
 * (RE-) LOCATION POLICIES
 ********************************************************************************/

/* This implements the is-it-close-enough-to-its-preceder? test for relocation. */
static int flush_relocate_unless_close_enough (const reiser4_block_nr *pblk,
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
static int flush_left_relocate_check (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	reiser4_block_nr pblk = 0;
	reiser4_block_nr nblk = 0;

	assert ("jmacd-8989", ! jnode_is_root (node));

	/* New nodes are treated as if they are being relocated. */
	if (jnode_created (node) || (pos->leaf_relocate && jnode_get_level (node) == LEAF_LEVEL)) {
		return 1;
	}

	/* Find the preceder. */
	if (coord_is_leftmost_unit (parent_coord)) {
		pblk = *znode_get_block (parent_coord->node);
	} else {
		pblk = pos->preceder.blk;
	}

	/* If (pblk == 0) then the preceder isn't allocated or isn't known: relocate. */
	if (pblk == 0) {
		return 1;
	}

	nblk = *jnode_get_block (node);

	return flush_relocate_unless_close_enough (& pblk, & nblk);
}

/* FIXME: comment */
static int flush_left_relocate_dirty (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	int ret;

	if (! znode_check_dirty (parent_coord->node)) {

		if ((ret = flush_left_relocate_check (node, parent_coord, pos)) < 0) {
			return ret;
		}

		if (ret == 1) {
			znode_set_dirty (parent_coord->node);
		}
	}

	return 0;
}

/* FIXME: comment */
static int flush_right_relocate_end_of_twig (flush_position *pos)
{
	int ret;
	jnode *child = NULL;
	lock_handle right_lock;
	load_handle right_load;
	coord_t right_coord;

	init_lh (& right_lock);
	init_zh (& right_load);

	/* Not using get_utmost_if_dirty because then we would not discover
	 * a dirty unformatted leftmost child of a clean twig. */
	if ((ret = reiser4_get_right_neighbor (& right_lock, pos->parent_lock.node, ZNODE_WRITE_LOCK, 0))) {
		/* If -ENAVAIL,-ENOENT we are finished (or do we go upward anyway?). */
		/* FIXME: check EDEADLK, EINVAL */
		if (ret == -ENAVAIL || ret == -ENOENT) {

			/* Now finished with twig node. */
			trace_on (TRACE_FLUSH_VERB, "end_of_twig: STOP (end of twig, ENAVAIL right): %s\n", flush_pos_tostring (pos));
			ret = flush_release_ancestors (pos->parent_lock.node);
			flush_pos_stop (pos);
		}
		goto exit;
	}

	trace_on (TRACE_FLUSH_VERB, "end_of_twig: right node %s\n", flush_znode_tostring (right_lock.node));

	/* If the right twig is dirty then we don't have to check the child. */
	if (znode_check_dirty (right_lock.node)) {
		ret = 0;
		goto exit;
	}

	if ((ret = load_zh (& right_load, right_lock.node))) {
		goto exit;
	}

	/* Then if the child is not dirty, we have nothing to do. */
	coord_init_first_unit (& right_coord, right_lock.node);

	if ((ret = item_utmost_child (& right_coord, LEFT_SIDE, & child))) {
		goto exit;
	}

	if (child == NULL || ! jnode_check_dirty (child)) {
		/* Finished at this twig. */
		trace_on (TRACE_FLUSH_VERB, "end_of_twig: STOP right node & leftmost child clean\n");
		ret = flush_release_ancestors (pos->parent_lock.node);
		flush_pos_stop (pos);
		goto exit;
	}

	/* Now see if the child should be relocated. */
	if ((ret = flush_left_relocate_dirty (child, & right_coord, pos))) {
		goto exit;
	}

	/* If the child is relocated it will be handled by squalloc_changed_ancestor,
	 * which also handles pos_to_child. */

 exit:
	if (child != NULL) { jput (child); }
	done_lh (& right_lock);
	done_zh (& right_load);
	return ret;
}

/********************************************************************************
 * FIXME:
 ********************************************************************************/

/* Here the rule is: squeeze left uncle with parent if uncle is dirty.  Repeat until the
 * child's parent is stable.  If the child is a leftmost child, repeat this left-edge
 * squeezing operation at the next level up.  AS YET UNIMPLEMENTED in the interest of
 * reducing time-to-benchmark.  Also note that we cannot allocate during the left-squeeze
 * and we have to maintain coordinates throughout or else repeat a tree search.
 * Difficult. */
static int flush_squeeze_left_edge (flush_position *pos UNUSED_ARG)
{
	return 0;
}

/* FIXME: comment */
static int flush_set_preceder (const coord_t *coord_in, flush_position *pos)
{
	int ret;
	coord_t coord;
	lock_handle left_lock;

	coord_dup (& coord, coord_in);

	init_lh (& left_lock);

	if (! coord_is_leftmost_unit (& coord)) {
		coord_prev_unit (& coord);
	} else {
		if ((ret = reiser4_get_left_neighbor (& left_lock, coord.node, ZNODE_READ_LOCK, 0))) {
			/* FIXME: check EINVAL, EDEADLK */
			if (ret == -ENAVAIL || ret == -ENOENT) { ret = 0; }
			goto exit;
		}
	}

	if ((ret = item_utmost_child_real_block (& coord, RIGHT_SIDE, & pos->preceder.blk))) {
		goto exit;
	}

 exit:
	done_lh (& left_lock);
	return ret;
}

/* Recurse up the tree as long as ancestors are same_atom_dirty and not allocated,
 * allocate on the way back down. */
static int flush_alloc_one_ancestor (coord_t *coord, flush_position *pos)
{
	int ret = 0;
	lock_handle alock;
	load_handle aload;
	coord_t acoord;

	/* As we ascend at the left-edge of the region to flush, take this opportunity at
	 * the twig level to find our parent-first preceder unless we have already set
	 * it. */
	if (pos->preceder.blk == 0 && znode_get_level (coord->node) == TWIG_LEVEL) {
		if ((ret = flush_set_preceder (coord, pos))) {
			return ret;
		}
	}

	/* If the ancestor is clean or already allocated, or if the child is not a
	 * leftmost child, stop going up. */
	if (! znode_check_dirty (coord->node) || znode_check_allocated (coord->node) || ! coord_is_leftmost_unit (coord)) {
		return 0;
	}

	init_lh (& alock);
	init_zh (& aload);
	coord_init_invalid (& acoord, NULL);

	/* Only ascend to the next level if it is a leftmost child, but write-lock the
	 * parent in case we will relocate the child. */
	if (! znode_is_root (coord->node)) {

		if ((ret = jnode_lock_parent_coord (ZJNODE (coord->node), & acoord, & alock, & aload, ZNODE_WRITE_LOCK))) {
			/* FIXME: check EINVAL, EDEADLK */
			goto exit;
		}

		if ((ret = flush_left_relocate_dirty (ZJNODE (coord->node), & acoord, pos))) {
			goto exit;
		}

		/* Recursive call. */
		if (znode_check_dirty (acoord.node) &&
		    ! znode_check_allocated (acoord.node) &&
		    (ret = flush_alloc_one_ancestor (& acoord, pos))) {
			goto exit;
		}
	}

	/* Note: we call allocate with the parent write-locked (except at the root) in
	 * case we relocate the child, in which case it will modify the parent during this
	 * call. */
	ret = flush_allocate_znode (coord->node, & acoord, pos);

 exit:
	done_zh (& aload);
	done_lh (& alock);
	return ret;
}

/* Handle the first step in allocating ancestors, setup the call to
 * flush_alloc_one_ancestor.  This is a special case due to the twig-level and unformatted
 * nodes. */
static int flush_alloc_ancestors (flush_position *pos)
{
	int ret = 0;
	lock_handle plock;
	load_handle pload;
	coord_t pcoord;

	coord_init_invalid (& pcoord, NULL);
	init_lh (& plock);
	init_zh (& pload);

	if (flush_pos_unformatted (pos) || ! znode_is_root (JZNODE (pos->point))) {
		/* Lock the parent (it may already be locked, thus the special case). */
		if ((ret = flush_pos_lock_parent (pos, & pcoord, & plock, & pload, ZNODE_WRITE_LOCK))) {
			goto exit;
		}

		/* It may not be dirty, in which case we should decide whether to relocate the
		 * child now. */
		if ((ret = flush_left_relocate_dirty (pos->point, & pcoord, pos))) {
			goto exit;
		}

		ret = flush_alloc_one_ancestor (& pcoord, pos);
	}

	/* If we are at a formatted node, allocate it now. */
	if (ret == 0 && ! flush_pos_unformatted (pos)) {
		ret = flush_allocate_znode (JZNODE (pos->point), & pcoord, pos);
	}

 exit:
	done_zh (& pload);
	done_lh (& plock);
	return ret;
}

/* FIXME: comment */
static int flush_squalloc_one_changed_ancestor (znode *node, int call_depth, flush_position *pos)
{
	int ret;
	int same_parents;
	int unallocated_below;
	lock_handle right_lock;
	lock_handle parent_lock;
	load_handle right_load;
	load_handle parent_load;
	load_handle node_load;
	coord_t at_right, right_parent_coord;

	init_lh (& right_lock);
	init_lh (& parent_lock);
	init_zh (& right_load);
	init_zh (& parent_load);
	init_zh (& node_load);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] %s\n", call_depth, flush_znode_tostring (node));

	/* Originally the idea was to assert that a node is always allocated before the
	 * upward recursion here, but its not always true.  We are allocating in the
	 * rightward direction and there is no reason the ancestor must be allocated. */
	/*assert ("jmacd-9925", znode_check_allocated (node));*/

	if ((ret = load_zh (& node_load, node))) {
		warning ("jmacd-61424", "zload failed: %d", ret);
		goto exit;
	}

 RIGHT_AGAIN:
	/* Get the right neighbor. */
	if ((ret = znode_get_utmost_if_dirty (node, & right_lock, RIGHT_SIDE, ZNODE_WRITE_LOCK))) {
		/* If the node is unavailable... */
		if (ret == -ENAVAIL) {
			/* We are finished at this level, except at the leaf level we may
			 * have an unformatted node to the right.  That is tested again in
			 * the calling function.  Could be done here without a second
			 * test, except that complicates the recursion here. */
			ret = 0;
			trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] ENAVAIL: %s\n", call_depth, flush_pos_tostring (pos));
		} else {
			warning ("jmacd-61425", "znode_get_if_dirty failed: %d", ret);
		}
		/* Otherwise error. */
		goto exit;
	}

	if ((ret = load_zh (& right_load, right_lock.node))) {
		warning ("jmacd-61426", "zload failed: %d", ret);
		goto exit;
	}

	coord_init_after_last_item (& at_right, node);

	assert ("jmacd-7866", ! node_is_empty (right_lock.node));

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before right neighbor %s\n", call_depth, flush_znode_tostring (right_lock.node));

	/* We found the right znode (and locked it), now squeeze from right into
	 * current node position. */
	if ((ret = squalloc_right_neighbor (node, right_lock.node, pos)) < 0) {
		warning ("jmacd-61427", "squalloc_right_neighbor failed: %d", ret);
		goto exit;
	}

	unallocated_below = ! coord_is_after_rightmost (& at_right);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after right neighbor %s: unallocated_below = %u\n",
		  call_depth, flush_znode_tostring (right_lock.node), unallocated_below);

	/* unallocated_below may be true but we still may have allocated to the end of a twig
	 * (via extent_copy_and_allocate), in which case we should unset it. */
	if (unallocated_below && node == pos->parent_coord.node) {

		assert ("jmacd-1732", ! coord_is_after_rightmost (& pos->parent_coord));

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before (shifted & unformatted): %s\n", call_depth, flush_pos_tostring (pos));
		/*trace_if (TRACE_FLUSH_VERB, print_coord ("present coord", & pos->parent_coord, 0));*/

		/* We reached this point because we were at the end of a twig, and now we
		 * have shifted new contents into that twig.  Skip past any allocated
		 * extents.  If we are still at the end of the node, unset unallocated_below. */
		coord_next_unit (& pos->parent_coord);

		/*trace_if (TRACE_FLUSH_VERB, print_coord ("after next_unit", & pos->parent_coord, 0));*/
		assert ("jmacd-1731", coord_is_existing_unit (& pos->parent_coord));

		while (coord_is_existing_unit (& pos->parent_coord) &&
		       item_is_extent (& pos->parent_coord)) {
			assert ("jmacd-8612", ! extent_is_unallocated (& pos->parent_coord));
			coord_next_unit (& pos->parent_coord);
		}

		if (! coord_is_existing_unit (& pos->parent_coord)) {
			unallocated_below = 0;
		}

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after (shifted & unformatted): unallocated_below = %u: %s\n", call_depth, unallocated_below, flush_pos_tostring (pos));
	}

	/* The next two if-stmts depend on call_depth, which is initially set to
	 * is_unformatted because when allocating for unformatted nodes the first call is
	 * effectively at level 1: */

	/* If we emptied the right node and we are unconcerned with allocation at the
	 * level below. */
	if ((unallocated_below == 0 || call_depth == 0) && node_is_empty (right_lock.node)) {
		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] right again: %s\n", call_depth, flush_pos_tostring (pos));
		done_zh (& right_load);
		done_lh (& right_lock);
		goto RIGHT_AGAIN;
	}

	/* If anything is shifted at an upper level, we should not allocate any further
	 * because the child is no longer rightmost. */
	if (unallocated_below && call_depth > 0) {
		ret = 0;
		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] shifted & not leaf: %s\n", call_depth, flush_pos_tostring (pos));
		goto exit;
	}

	assert ("jmacd-18231", ! node_is_empty (right_lock.node));

	/* Here we still have the right node locked, current node is full, ready to shift
	 * positions, but first we have to check for ancestor changes and squeeze going
	 * upward. */
	if (! (same_parents = znode_same_parents (node, right_lock.node))) {

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before (not same parents): %s\n", call_depth, flush_pos_tostring (pos));

		/* Recurse upwards on parent of node. */
		if ((ret = reiser4_get_parent (& parent_lock, node, ZNODE_WRITE_LOCK, 1 /*only_connected*/))) {
			/* FIXME: check ENAVAIL, EINVAL, EDEADLK */
			warning ("jmacd-61428", "reiser4_get_parent failed: %d", ret);
			goto exit;
		}

		if ((ret = flush_squalloc_one_changed_ancestor (parent_lock.node, call_depth + 1, pos))) {
			warning ("jmacd-61429", "sq1_ca recursion failed: %d", ret);
			goto exit;
		}

		done_lh (& parent_lock);

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after (not same parents): %s\n", call_depth, flush_pos_tostring (pos));
	}

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] ready to enqueue node %s\n", call_depth, flush_znode_tostring (node));

	/* Now finished with node. */
	if (/*znode_check_dirty (node) && znode_check_allocated (node) &&*/
	    (ret = flush_release_znode (node))) {
		warning ("jmacd-61440", "flush_release_znode failed: %d", ret);
		goto exit;
	}

	/* No reason to hold onto the node data now, can release it early.  Okay to call
	 * done_zh twice. */
	done_zh (& node_load);

	/* NOTE: A possible optimization is to avoid locking the right_parent here.  It
	 * requires handling three cases, however, which makes it more complex than I want
	 * to implement right now.  (1) Same parents (no recursion) case, lift the above
	 * get_parent call outside the preceding (! same_parents) condition and allocate
	 * the right node here. (2) Pass the right child into the recursive call and
	 * allocate when the right neighbor (its parent) is locked in the call above, but
	 * (3) handle the case where the right child is shifted so that they have same
	 * parents after shifting. */

	/* Allocate the right node. */
	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] ready to allocate right %s\n", call_depth, flush_znode_tostring (right_lock.node));

	if (! znode_check_allocated (right_lock.node)) {
		if ((ret = jnode_lock_parent_coord (ZJNODE (right_lock.node), & right_parent_coord, & parent_lock, & parent_load, ZNODE_WRITE_LOCK))) {
			/* FIXME: check EINVAL, EDEADLK */
			warning ("jmacd-61430", "jnode_lock_parent_coord failed: %d", ret);
			goto exit;
		}

		if ((ret = flush_allocate_znode (right_lock.node, & right_parent_coord, pos))) {
			warning ("jmacd-61431", "flush_allocate_znode failed: %d", ret);
			goto exit;
		}
	}

	ret = 0;
 exit:
	done_zh (& node_load);
	done_zh (& right_load);
	done_zh (& parent_load);
	done_lh (& right_lock);
	done_lh (& parent_lock);
	return ret;
}

/* FIXME: comment */
static int flush_squalloc_changed_ancestors (flush_position *pos)
{
	int ret;
	int is_unformatted, is_dirty;
	lock_handle right_lock;
	znode *node;

 repeat:
	if ((is_unformatted = flush_pos_unformatted (pos))) {
		assert ("jmacd-9812", coord_is_after_rightmost (& pos->parent_coord));

		node = pos->parent_lock.node;
	} else {
		node = JZNODE (pos->point);
	}

	trace_on (TRACE_FLUSH_VERB, "sq_r changed ancestors before: %s\n", flush_pos_tostring (pos));

	assert ("jmacd-9814", znode_is_write_locked (node));

	init_lh (& right_lock);

	if ((ret = flush_squalloc_one_changed_ancestor (node, /*call_depth*/is_unformatted, pos))) {
		warning ("jmacd-61432", "sq1_ca failed: %d", ret);
		goto exit;
	}

	if (! flush_pos_valid (pos)) {
		goto exit;
	}

	trace_on (TRACE_FLUSH_VERB, "sq_rca after sq_ca recursion: %s\n", flush_pos_tostring (pos));

	/* In the unformatted case, we may have shifted new contents into the current
	 * twig. */
	if (is_unformatted && ! coord_is_after_rightmost (& pos->parent_coord)) {

		trace_on (TRACE_FLUSH_VERB, "sq_rca unformatted after: %s\n", flush_pos_tostring (pos));

		/* Then, if we are positioned at a formatted item, allocate & descend. */
		if (item_is_internal (& pos->parent_coord)) {
			ret = flush_pos_to_child_and_alloc (pos);
		}

		/* That's all. */
		goto exit;
	}

	/* Get the right neighbor. */
	assert ("jmacd-1092", znode_is_write_locked (node));
	if ((ret = znode_get_utmost_if_dirty (node, & right_lock, RIGHT_SIDE, ZNODE_WRITE_LOCK))) {

		/* Unless we get ENAVAIL at the leaf level, it means to stop. */
		if (ret != -ENAVAIL || znode_get_level (node) != LEAF_LEVEL) {
			if (ret == -ENAVAIL) {
				ret = flush_release_ancestors (node);
				trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (ENAVAIL, ancestors allocated): %s\n", flush_pos_tostring (pos));
				flush_pos_stop (pos);
			} else {
				warning ("jmacd-61433", "znode_get_if_dirty failed: %d", ret);
			}
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "sq_rca no right at leaf, to parent: %s\n", flush_pos_tostring (pos));

		/* We are leaving node now, enqueue it. */
		if ((ret = flush_release_znode (node))) {
			warning ("jmacd-61434", "flush_release_znode failed: %d", ret);
			goto exit;
		}

		/* We may have a unformatted node to the right. */
		if ((ret = flush_pos_to_parent (pos))) {
			warning ("jmacd-61435", "flush_pos_to_parent failed: %d", ret);
			goto exit;
		}

		/* Procede with unformatted case. */
		assert ("jmacd-9259", flush_pos_unformatted (pos));
		assert ("jmacd-9260", ! coord_is_after_rightmost (& pos->parent_coord));
		is_unformatted = 1;
		node = pos->parent_lock.node;

		coord_next_item (& pos->parent_coord);

		/* Now maybe try the twig to the right... */
		if (coord_is_after_rightmost (& pos->parent_coord)) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca to right twig: %s\n", flush_pos_tostring (pos));

			if (znode_check_dirty (node)) {
				goto repeat;
			} else {
				trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (right twig clean): %s\n", flush_pos_tostring (pos));
				ret = flush_pos_stop (pos);
				goto exit;
			}
		}

		/* If positioned over a formatted node, then the preceding
		 * get_utmost_if_dirty would have succeeded if it were in memory. */
		if (item_is_internal (& pos->parent_coord)) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca stop at twig, next is internal: %s\n", flush_pos_tostring (pos));
		stop_at_twig:
			/* We are leaving twig now, enqueue it if allocated. */
			ret = flush_release_ancestors (node);
			trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (at twig): %s\n", flush_pos_tostring (pos));
			flush_pos_stop (pos);
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "sq_rca check right twig child: %s\n", flush_pos_tostring (pos));

		/* Finally, we must now be positioned over an extent, but is it dirty? */
		if ((ret = item_utmost_child_dirty (& pos->parent_coord, LEFT_SIDE, & is_dirty))) {
			warning ("jmacd-61437", "item_utmost_child_dirty failed: %d", ret);
			goto exit;
		}

		if (! is_dirty) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca stop at twig, child not dirty: %s\n", flush_pos_tostring (pos));
			goto stop_at_twig;
		}

		ret = 0;
		goto exit;
	}

	/* We have a new right and it should have been allocated by the call to
	 * flush_squalloc_one_changed_ancestor.  FIXME: its coneivable under a
	 * multi-threaded workload that this can be violated.  Nikita seems to have proven
	 * it.  Eventually handle this nicely.  Until then, this asserts that sq1 does the
	 * right thing. */
	assert ("jmacd-90123", jnode_check_allocated (ZJNODE (right_lock.node)));

	if (is_unformatted) {
		done_lh (& pos->parent_lock);
		move_lh (& pos->parent_lock, & right_lock);
		if ((ret = load_zh (& pos->parent_load, pos->parent_lock.node))) {
			warning ("jmacd-61438", "zload failed: %d", ret);
			goto exit;
		}
		coord_init_first_unit (& pos->parent_coord, pos->parent_lock.node);

		if (! item_is_extent (& pos->parent_coord)) {
			ret = flush_pos_to_child_and_alloc (pos);
		}
	} else {
		done_lh (& pos->point_lock);
		if ((ret = flush_pos_set_point (pos, ZJNODE (right_lock.node)))) {
			warning ("jmacd-61439", "flush_pos_set_point failed: %d", ret);
			goto exit;
		}
		move_lh (& pos->point_lock, & right_lock);
	}

 exit:
	done_lh (& right_lock);
	return ret;
}

/* FIXME: comment */
static int flush_squalloc_right (flush_position *pos)
{
	int ret;

	/* Step 1: Re-allocate all the ancestors as long as the position is a leftmost
	 * child. */
	trace_on (TRACE_FLUSH_VERB, "sq_r alloc ancestors: %s\n", flush_pos_tostring (pos));

	assert ("jmacd-9921", jnode_check_dirty (pos->point));
	assert ("jmacd-9925", ! jnode_check_allocated (pos->point));

	if ((ret = flush_alloc_ancestors (pos))) {
		goto exit;
	}

 STEP_2:/* Step 2: Handle extents. */
	if (flush_pos_valid (pos) && flush_pos_unformatted (pos)) {

		int is_dirty;

		assert ("jmacd-8712", item_is_extent (& pos->parent_coord));

		trace_on (TRACE_FLUSH_VERB, "allocate_extent_in_place: %s\n", flush_pos_tostring (pos));

		/* This allocates extents up to the end of the current twig and returns
		 * pos->parent_coord set to the next item. */
		if ((ret = allocate_extent_item_in_place (& pos->parent_coord, pos))) {
			goto exit;
		}

		coord_next_unit (& pos->parent_coord);

		/* If we have not allocated this node completely... */
		if (! coord_is_after_rightmost (& pos->parent_coord)) {

			if ((ret = item_utmost_child_dirty (& pos->parent_coord, LEFT_SIDE, & is_dirty))) {
				goto exit;
			}

			/* If dirty then repeat, otherwise stop here. */
			if (is_dirty) {
				/* If the parent_coord is not positioned over an extent
				 * (at this twig level), we should descend to the
				 * formatted child. */
				trace_on (TRACE_FLUSH_VERB, "sq_r unformatted_right_is_dirty: %s type %s\n",
					  flush_pos_tostring (pos), item_is_extent (& pos->parent_coord) ? "extent" : "internal");

				if (! item_is_extent (& pos->parent_coord) && (ret = flush_pos_to_child_and_alloc (pos))) {
					goto exit;
				}

				trace_on (TRACE_FLUSH_VERB, "sq_r unformatted_goto_step2: %s\n", flush_pos_tostring (pos));
				goto STEP_2;
			} else {
				/* We are finished at this level. */
				ret = flush_release_ancestors (pos->parent_coord.node);
				goto exit;
			}
		}

		/* We are about to try to allocate the right twig by calling
		 * flush_squalloc_changed_ancestors in the flush_pos_unformatted state.
		 * However, the twig may need to be dirtied first if its left-child will
		 * be relocated. */
		if ((ret = flush_right_relocate_end_of_twig (pos))) {
			goto exit;
		}

		/* Now squeeze upward, allocate downward, for any ancestors that are not
		 * in common between parent_node and right_twig and not allocated
		 * (including right_twig itself). */
	}

	if (flush_pos_valid (pos)) {
		/* Step 3: Formatted and unformatted cases. */
		if ((ret = flush_squalloc_changed_ancestors (pos))) {
			goto exit;
		}
	}

	if (flush_pos_valid (pos)) {
		/* Repeat. */
		trace_on (TRACE_FLUSH_VERB, "sq_r repeat: %s\n", flush_pos_tostring (pos));
		goto STEP_2;
	}

 exit:
	return ret;
}

/********************************************************************************
 * SQUEEZE AND ALLOCATE
 ********************************************************************************/

/* Squeeze and allocate the right neighbor.  This is called after @left and
 * its current children have been squeezed and allocated already.  This
 * procedure's job is to squeeze and items from @right to @left.
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
					flush_position *pos)
{
	int ret;

	assert ("jmacd-9321", ! node_is_empty (left));
	assert ("jmacd-9322", ! node_is_empty (right));
	assert ("jmacd-9323", znode_get_level (left) == znode_get_level (right));

	trace_on (TRACE_FLUSH_VERB, "sq_rn[%u] left  %s\n", znode_get_level (left), flush_znode_tostring (left));
	trace_on (TRACE_FLUSH_VERB, "sq_rn[%u] right %s\n", znode_get_level (left), flush_znode_tostring (right));

	switch (znode_get_level (left)) {
	case TWIG_LEVEL:
		/* Shift with extent allocating until either an internal item
		 * is encountered or everything is shifted or no free space
		 * left in @left */
		ret = squalloc_right_twig (left, right, pos);
		break;

	default:
		/* All other levels can use shift_everything until we implement per-item
		 * flush plugins. */
		ret = squeeze_right_non_twig (left, right);
		break;
	}

	assert ("jmacd-2011", (ret < 0 ||
			       ret == SQUEEZE_SOURCE_EMPTY ||
			       ret == SQUEEZE_TARGET_FULL ||
			       ret == SUBTREE_MOVED));

	if (ret == SQUEEZE_SOURCE_EMPTY) {
		reiser4_stat_flush_add (squeezed_completely);
	}

	trace_on (TRACE_FLUSH_VERB, "sq_rn[%u] returns %s: left %s\n",
		  znode_get_level (left),
		  (ret == SQUEEZE_SOURCE_EMPTY) ? "src empty" :
		  ((ret == SQUEEZE_TARGET_FULL) ? "tgt full" :
		   ((ret == SUBTREE_MOVED) ? "tree moved" : "error")),
		  flush_znode_tostring (left));
	return ret;
}

/* Shift as much as possible from @right to @left using the memcpy-optimized
 * shift_everything_left.  @left and @right are formatted neighboring nodes on
 * leaf level. */
static int squeeze_right_non_twig (znode *left, znode *right)
{
	int ret;
	carry_pool pool;
	carry_level todo;

	assert ("nikita-2246", znode_get_level (left) == znode_get_level (right));
	init_carry_pool (& pool);
	init_carry_level (& todo, & pool);

	ret = shift_everything_left (right, left, & todo);

	spin_lock_dk (current_tree);
	update_znode_dkeys (left, right);
	spin_unlock_dk (current_tree);

	if (ret > 0) {
		/* Carry is called to update delimiting key or to remove empty
		 * node. */
		//info ("shifted %u bytes %p <- %p\n", ret, left, right);
		ON_STATS (todo.level_no = znode_get_level (left) + 1);
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
				flush_position *pos)
{
	int ret = 0;
	coord_t coord, /* used to iterate over items */
		stop_coord; /* used to call twig_cut properly */
	reiser4_key stop_key;

	assert ("jmacd-2008", ! node_is_empty (right));
	coord_init_first_unit (&coord, right);

	/* Initialize stop_key to detect if any extents are copied.  After
	 * this loop loop if stop_key is still equal to *min_key then nothing
	 * was copied (and there is nothing to cut). */
	stop_key = *min_key ();

	trace_on (TRACE_FLUSH_VERB, "sq_twig before copy extents: left %s\n", flush_znode_tostring (left));
	trace_on (TRACE_FLUSH_VERB, "sq_twig before copy extents: right %s\n", flush_znode_tostring (right));
	/*trace_if (TRACE_FLUSH_VERB, print_node_content ("left", left, ~0u));*/
	/*trace_if (TRACE_FLUSH_VERB, print_node_content ("right", right, ~0u));*/

	while (item_is_extent (&coord)) {
		reiser4_key last_stop_key;


		trace_if (TRACE_FLUSH_VERB, print_coord ("sq_twig:item_is_extent:", & coord, 0));

		last_stop_key = stop_key;
		if ((ret = allocate_and_copy_extent (left, &coord, pos, &stop_key)) < 0) {
			return ret;
		}

		/* we will cut from the beginning of node upto @stop_coord (and
		 * @stop_key) */
		if (!keyeq (&stop_key, &last_stop_key)) {
			/* something were copied, update cut boundary */
			coord_dup (&stop_coord, &coord);
		}

		if (ret == SQUEEZE_TARGET_FULL) {
			/* Could not complete with current extent item. */
			trace_if (TRACE_FLUSH_VERB, print_coord ("sq_twig:target_full:", & coord, 0));
			break;
		}

		assert ("jmacd-2009", ret == SQUEEZE_CONTINUE);

		/* coord_next_item returns 0 if there are more items. */
		if (coord_next_item (&coord) != 0) {
			ret = SQUEEZE_SOURCE_EMPTY;
			trace_if (TRACE_FLUSH_VERB, print_coord ("sq_twig:source_empty:", & coord, 0));
			break;
		}

		trace_if (TRACE_FLUSH_VERB, print_coord ("sq_twig:continue:", & coord, 0));
	}

	trace_on (TRACE_FLUSH_VERB, "sq_twig:after copy extents: left %s\n", flush_znode_tostring (left));
	trace_on (TRACE_FLUSH_VERB, "sq_twig:after copy extents: right %s\n", flush_znode_tostring (right));
	/*trace_if (TRACE_FLUSH_VERB, print_node_content ("left", left, ~0u));*/
	/*trace_if (TRACE_FLUSH_VERB, print_node_content ("right", right, ~0u));*/

	if (!keyeq (&stop_key, min_key ())) {
		int cut_ret;

		trace_if (TRACE_FLUSH_VERB, print_coord ("sq_twig:cut_coord", & coord, 0));

		/* Helper function to do the cutting. */
		if ((cut_ret = squalloc_right_twig_cut (&stop_coord, &stop_key, left))) {
			warning ("jmacd-87113", "cut_node failed: %d", cut_ret);
			assert ("jmacd-6443", cut_ret < 0);
			return cut_ret;
		}

		/*trace_if (TRACE_FLUSH_VERB, print_node_content ("right_after_cut", right, ~0u));*/
	}

	if (ret == SQUEEZE_TARGET_FULL) { goto out; }

	if (node_is_empty (right)) {
		/* The whole right node was copied into @left. */
		trace_on (TRACE_FLUSH_VERB, "sq_twig right node empty: %s\n", flush_znode_tostring (right));
		assert ("vs-464", ret == SQUEEZE_SOURCE_EMPTY);
		goto out;
	}

	coord_init_first_unit (&coord, right);

	assert ("jmacd-433", item_is_internal (&coord));

	/* Shift an internal unit.  The child must be allocated before shifting any more
	 * extents, so we stop here. */
	ret = shift_one_internal_unit (left, right);

out:
	assert ("jmacd-8612", ret < 0 || ret == SQUEEZE_TARGET_FULL || ret == SUBTREE_MOVED || ret == SQUEEZE_SOURCE_EMPTY);
	return ret;
}

/* squalloc_right_twig helper function, cut a range of extent items from
 * cut node to->node from the beginning up to coord @to. */
static int squalloc_right_twig_cut (coord_t * to, reiser4_key * to_key, znode * left)
{
	coord_t from;
	reiser4_key from_key;

	coord_init_first_unit (&from, to->node);
	item_key_by_coord (&from, &from_key);

	return cut_node (&from, to, & from_key, to_key,
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
	coord_t coord;
	int size, moved;
	carry_plugin_info info;

	assert ("nikita-2247", znode_get_level (left) == znode_get_level (right));
	coord_init_first_unit (&coord, right);

	assert ("jmacd-2007", item_is_internal (&coord));

	init_carry_pool (&pool);
	init_carry_level (&todo, &pool);

	size = item_length_by_coord (&coord);
	info.todo  = &todo;
	info.doing = NULL;
	ret  = node_plugin_by_node (left)->shift (&coord, left, SHIFT_LEFT,
						  1/* delete @right if it becomes empty*/,
						  0/* move coord */,
						  &info);

	/* If shift returns positive, then we shifted the item. */
	assert ("vs-423", ret <= 0 || size == ret);
	moved = (ret > 0);

	if (moved) {
		znode_set_dirty (left);
		znode_set_dirty (right);
		spin_lock_dk (current_tree);
		update_znode_dkeys (left, right);
		spin_unlock_dk (current_tree);

		ON_STATS (todo.level_no = znode_get_level (left) + 1);
		ret = carry (&todo, NULL /* previous level */);
	}

	trace_on (TRACE_FLUSH_VERB, "shift_one %s an item: left has %u items, right has %u items\n",
		  moved > 0 ? "moved" : "did not move", node_num_items (left), node_num_items (right));

	done_carry_pool (&pool);

	if (ret != 0) {
		/* Shift or carry operation failed. */
		assert ("jmacd-7325", ret < 0);
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
int jnode_check_allocated (jnode *node)
{
	/* It must be clean or relocated or wandered.  New allocations are set to relocate. */
	int ret;
	assert ("jmacd-71275", spin_jnode_is_not_locked (node));
	spin_lock_jnode (node);
	ret = jnode_is_allocated (node);
	spin_unlock_jnode (node);
	return ret;
}

int znode_check_allocated (znode *node)
{
	return jnode_check_allocated (ZJNODE (node));
}

/* Audited by: umka (2002.06.11) */
void jnode_set_block( jnode *node /* jnode to update */,
		      const reiser4_block_nr *blocknr /* new block nr */ )
{
	assert( "nikita-2020", node  != NULL );
	assert( "umka-055", blocknr != NULL );
	node -> blocknr = *blocknr;
}

/* return true if jnode has real blocknr */ /* FIXME: JMACD->?? Who wrote this, who uses it?  Looks funny to me. */
int jnode_has_block (jnode * node)
{
	assert ("vs-673", node);
	assert ("vs-674", jnode_is_unformatted (node));
	return node->blocknr;
}

/* FIXME: comment */
static int flush_allocate_znode_update (znode *node, coord_t *parent_coord, flush_position *pos)
{
	int ret;
	reiser4_block_nr blk;
	reiser4_block_nr len = 1;
	lock_handle fake_lock;

	if ((ret = reiser4_alloc_blocks (& pos->preceder, & blk, & len))) {
		return ret;
	}

	if (! ZF_ISSET (node, ZNODE_CREATED) &&
	    (ret = reiser4_dealloc_block (znode_get_block (node), /* defer */1, 0 /* FIXME: JMACD->ZAM: Why 0 (BLOCK_NOT_COUNTED)? */))) {
		return ret;
	}

	init_lh (& fake_lock);

	if (! znode_is_root (node)) {

		internal_update (parent_coord, blk);

	} else {
		znode *fake = zget (current_tree, &FAKE_TREE_ADDR, NULL, 0 , GFP_KERNEL);

		if (IS_ERR (fake)) { ret = PTR_ERR(fake); goto exit; }

		if ((ret = longterm_lock_znode (& fake_lock, fake, ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI))) {
			/* The fake node cannot be deleted, and we must have priority
			 * here, and may not be confused with ENOSPC. */
			assert ("jmacd-74412", ret != -EINVAL && ret != -EDEADLK && ret != -ENOSPC);
			zput (fake);
			goto exit;
		}

		zput (fake);

		spin_lock_tree (current_tree);
		current_tree->root_block = blk;
		spin_unlock_tree (current_tree);
	}

	ret = znode_rehash (node, & blk);
 exit:
	done_lh (& fake_lock);
	return ret;
}

/* FIXME: comment */
static int flush_allocate_znode (znode *node, coord_t *parent_coord, flush_position *pos)
{
	int ret;

	assert ("jmacd-7987", ! jnode_check_allocated (ZJNODE (node)));
	assert ("jmacd-7988", znode_is_write_locked (node));
	assert ("jmacd-7989", coord_is_invalid (parent_coord) || znode_is_write_locked (parent_coord->node));

	if (jnode_created (node) || znode_is_root (node)) {
		/* No need to decide with new nodes, they are treated the same as
		 * relocate. If the root node is dirty, relocate. */
		goto best_reloc;

	} else if (pos->leaf_relocate != 0 && znode_get_level (node) == LEAF_LEVEL) {

		/* We have enough nodes to relocate no matter what. */
		goto best_reloc;
	} else if (pos->preceder.blk == 0) {

		/* If we don't know the preceder, leave it where it is. */
		ZF_SET (node, ZNODE_WANDER);
	} else {
		/* Make a decision based on block distance. */
		reiser4_block_nr dist;
		reiser4_block_nr nblk = *znode_get_block (node);

		assert ("jmacd-6172", ! blocknr_is_fake (& nblk));
		assert ("jmacd-6173", ! blocknr_is_fake (& pos->preceder.blk));
		assert ("jmacd-6174", pos->preceder.blk != 0);

		if (pos->preceder.blk == nblk - 1) {
			/* Ideal. */
			ZF_SET (node, ZNODE_WANDER);
		} else {

			dist = (nblk < pos->preceder.blk) ? (pos->preceder.blk - nblk) : (nblk - pos->preceder.blk);

			/* See if we can find a closer block (forward direction only). */
			pos->preceder.max_dist = min ((reiser4_block_nr) FLUSH_RELOCATE_DISTANCE, dist);
			pos->preceder.level    = znode_get_level (node);

			if ((ret = flush_allocate_znode_update (node, parent_coord, pos)) && (ret != -ENOSPC)) {
				return ret;
			}

			if (ret == 0) {
				/* Got a better allocation. */
				ZF_SET (node, ZNODE_RELOC);
			} else if (dist < FLUSH_RELOCATE_DISTANCE) {
				/* The present allocation is good enough. */
				ZF_SET (node, ZNODE_WANDER);
			} else {
				/* Otherwise, try to relocate to the best position. */
			best_reloc:
				ZF_SET (node, ZNODE_RELOC);
				pos->preceder.max_dist = 0;
				if ((ret = flush_allocate_znode_update (node, parent_coord, pos))) {
					return ret;
				}
			}
		}
	}

	/* This is the new preceder. */
	pos->preceder.blk = *znode_get_block (node);
	pos->alloc_cnt += 1;

	assert ("jmacd-4277", ! blocknr_is_fake (& pos->preceder.blk));
	assert ("jmacd-4278", ! JF_ISSET (node, ZNODE_FLUSH_BUSY));

	JF_SET (node, ZNODE_FLUSH_BUSY);
	trace_on (TRACE_FLUSH, "alloc: %s\n", flush_znode_tostring (node));

	/* Queue it now, node is busy until release_znode is called. */
	return flush_queue_jnode (ZJNODE (node), pos);
}

/* FIXME: comment */
static int flush_queue_jnode (jnode *node, flush_position *pos)
{
	/* FIXME: The main source of problems for this approach is likely to be that the
	 * queue retains a reference to each node.  While a node is referenced it can
	 * still be reached through the sibling list, which can confuse flush code.
	 * However, this should only be able to confuse a concurrent flush process.  We
	 * shall see. */

	/* FIXME: See comment in flush_rewrite_jnode. */
	if (! jnode_check_dirty (node) || JF_ISSET (node, ZNODE_HEARD_BANSHEE)) {
		return 0;
	}

	assert ("jmacd-4279", pos->queue_num < FLUSH_QUEUE_SIZE);
	assert ("jmacd-1771", jnode_check_allocated (node));

	pos->queue[pos->queue_num++] = jref (node);

	trace_if (TRACE_FLUSH, if (jnode_is_unformatted (node)) { info ("queue: %s\n", flush_jnode_tostring (node)); });

	if (pos->queue_num == FLUSH_QUEUE_SIZE) {
		return flush_finish (pos, 0);
	}

	return 0;
}

/* This enqueues the node into the developing "struct bio" queue. */
static int flush_release_znode (znode *node)
{
	trace_on (TRACE_FLUSH_VERB, "relse: %s\n", flush_znode_tostring (node));
	ZF_CLR (node, ZNODE_FLUSH_BUSY);
	return 0;
}

/* FIXME: comment */
int flush_enqueue_unformatted (jnode *node, flush_position *pos)
{
	return flush_queue_jnode (node, pos);
}

/* FIXME: comment */
/* This is called from withing interrupt context, so we need to  
   make a reiser4 context in order for all other stuff (and assertions)
   to work correctly */
static void flush_bio_write (struct bio *bio)
{
	int i;

	if (bio->bi_vcnt == 0) {
		warning ("nikita-2243", "Empty write bio completed.");
		return;
	}
	/* Note, we may put assertion here that this is in fact our sb and so
	   on */
	if (0 && REISER4_TRACE) {
		info ("flush_bio_write completion for %u blocks: BIO %p\n", 
		      bio->bi_vcnt, bio);
	}

	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (0 && REISER4_TRACE) {
			print_page ("flush_bio_write", pg);
		}

		if (! test_bit (BIO_UPTODATE, & bio->bi_flags)) {
			SetPageError (pg);
		}

		/*page_cache_release (pg);*/
		end_page_writeback (pg);
	}
	
	bio_put (bio);
}

/* FIXME: comment */
static int flush_finish (flush_position *pos, int none_busy)
{
	int i;
	int refill = 0;
	int ret = 0;

	if (REISER4_DEBUG && none_busy) {
		int nbusy = 0;

		for (i = 0; i < pos->queue_num; i += 1) {
			nbusy += JF_ISSET (pos->queue[i], ZNODE_FLUSH_BUSY);
		}

		assert ("jmacd-71239", nbusy == 0);
	}

	for (i = 0; ret == 0 && i < pos->queue_num; ) {

		jnode *check;

		check = pos->queue[i];

		assert ("jmacd-71235", check != NULL);

		/* FIXME: See the comment in flush_rewrite_jnode. */
		if (! jnode_check_dirty (check) || JF_ISSET (check, ZNODE_HEARD_BANSHEE)) {
			jput (check);
			pos->queue[i++] = NULL;
			continue;
		}

		assert ("jmacd-71236", jnode_check_allocated (check));

		/* Skip if the node is still busy (i.e., its children are being squalloced). */
		if (JF_ISSET (check, ZNODE_FLUSH_BUSY)) {

			assert ("jmacd-71238", ! none_busy);

			if (i != refill) {
				assert ("jmacd-71237", pos->queue[refill] == NULL);
				pos->queue[refill] = check;
				pos->queue[i] = NULL;
			}

			refill += 1;
			i += 1;
			continue;
		}

		/*
		 * FIXME:NIKITA->* temporary workaround until log record are
		 * implemented and debugged.
		 */
		if (WRITE_LOG && JF_ISSET (check, ZNODE_WANDER)) {
			/* It will be written later. */

			/* Log-writer expects these to be on the clean list.  They cannot
			 * leave memory and will remain captured. */
			jnode_set_clean (check);
			jput (check);
			pos->queue[i++] = NULL;
			continue;

		} else {

			/* Find consecutive nodes. */
			struct bio *bio;
			jnode *prev = check;
			int j, c, nr;
			struct super_block *super;
			int blksz;
			int max_j;

			super = check->pg->mapping->host->i_sb;
			assert( "jmacd-2029", super != NULL );

			/* FIXME: Need to work on this: */
#if REISER4_USER_LEVEL_SIMULATION
			max_j = pos->queue_num;
#else
 			max_j = min (pos->queue_num, i+ (bdev_get_queue (super->s_bdev)->max_sectors >> (super->s_blocksize_bits - 9)));
#endif

			/* Set j to the first non-consecutive, non-wandered block (or end-of-queue) */
			for (j = i + 1; j < max_j; j += 1) {
				if ((WRITE_LOG && JF_ISSET (pos->queue[j], ZNODE_WANDER)) ||
				    JF_ISSET (pos->queue[j], ZNODE_FLUSH_BUSY) ||
				    (*jnode_get_block (prev) + 1 != *jnode_get_block (pos->queue[j]))) {
					break;
				}
				prev = pos->queue[j];
			}

			nr = j - i;

			/* FIXME: What GFP flag? */
			if ((bio = bio_alloc (GFP_NOIO, nr)) == NULL) {
				ret = -ENOMEM;
				break;
			}

			/* Note: very much copied from page_cache.c:page_bio. */
			blksz = super->s_blocksize;
			assert( "jmacd-2028", blksz == ( int ) PAGE_CACHE_SIZE );

			bio->bi_sector = *jnode_get_block (check) * (blksz >> 9);
			bio->bi_bdev   = super->s_bdev;
			bio->bi_vcnt   = nr;
			bio->bi_size   = blksz * nr;
			bio->bi_end_io = flush_bio_write;

			for (c = 0, j = i; c < nr; c += 1, j += 1) {

				jnode       *node = pos->queue[j];
				struct page *pg   = node->pg;

				pos->queue[j] = NULL;

				trace_on (TRACE_FLUSH_VERB, "flush_finish writes %s\n", flush_jnode_tostring (node));

				assert ("jmacd-71442", super == pg->mapping->host->i_sb);

				/* FIXME: JMACD->NIKITA: can you review this? */
				jnode_set_clean (node);

				/*page_cache_get (pg);*/
				SetPageWriteback (pg);

				bio->bi_io_vec[c].bv_page   = pg;
				bio->bi_io_vec[c].bv_len    = blksz;
				bio->bi_io_vec[c].bv_offset = 0;

				/* The page reference is enough to maintain the jnode... */
				jput (node);
			}

			i = j;
			pos->enqueue_cnt += nr;

			trace_on (TRACE_FLUSH, "flush_finish writes %u consecutive blocks: BIO %p\n", nr, bio);

			submit_bio (WRITE, bio);

			/* FIXME: !!! temporary solution !!! */
			blk_run_queues ();
		}
	}

	trace_if (TRACE_FLUSH, if (ret == 0) { info ("flush_finish wrote %u leaving %u queued\n", pos->queue_num - refill, refill); });
	pos->queue_num = refill;

	return ret;
}

/* FIXME: comment */
static int flush_release_ancestors (znode *node)
{
	int ret;
	lock_handle parent_lock;

	assert ("jmacd-7444", znode_is_any_locked (node));

	if (! znode_check_dirty (node) || ! znode_check_allocated (node)) {
		return 0;
	}

	assert ("jmacd-7443", znode_check_allocated (node));

	if ((ret = flush_release_znode (node))) {
		return ret;
	}

	if (znode_is_root (node)) {
		return 0;
	}

	init_lh (& parent_lock);

	/* FIXME: Don't really need a longterm lock here, just climbing the tree, right? */
	if ((ret = reiser4_get_parent (& parent_lock, node, ZNODE_READ_LOCK, 1))) {
		/* FIXME: check ENAVAIL, EINVAL, EDEADLK */
		goto exit;
	}

	if ((ret = flush_release_ancestors (parent_lock.node))) {
		goto exit;
	}

 exit:
	done_lh (& parent_lock);
	return ret;
}

/* This writes a single page when it is flushed after an earlier allocation within the
 * same txn. */
static int flush_rewrite_jnode (jnode *node)
{
	struct page *pg;
	int ret;

	/* FIXME: Have to be absolutely sure that HEARD_BANSHEE isn't set when we write,
	 * otherwise if the page was a fresh allocation the dealloc of that block might
	 * have been non-deferred, and then we could trash otherwise-allocated data? */
	if (! jnode_check_dirty (node) || JF_ISSET (node, ZNODE_HEARD_BANSHEE)) {
		return 0;
	}

	if ((pg = jnode_page (node)) == NULL) {
		return -ENOMEM;
	}

	jnode_set_clean (node);

	lock_page (pg);
	/*SetPageWriteback (pg);*/

	ret = write_one_page (pg, 0 /* no wait */);

	trace_on (TRACE_FLUSH, "rewrite: %s\n", flush_jnode_tostring (node));
	return ret;
}

/********************************************************************************
 * JNODE INTERFACE
 ********************************************************************************/

/* Lock a node (if formatted) and then get its parent locked, set the child's
 * coordinate in the parent.  If the child is the root node, the above_root
 * znode is returned but the coord is not set.  This function may cause atom
 * fusion, but it is only used for read locks (at this point) and therefore
 * fusion only occurs when the parent is already dirty. */
/* Hans adds this note: remember to ask how expensive this operation is vs. storing parent
 * pointer in jnodes. */
static int jnode_lock_parent_coord (jnode *node,
				    coord_t *coord,
				    lock_handle *parent_lh,
				    load_handle *parent_zh,
				    znode_lock_mode parent_mode)
{
	int ret;

	assert ("jmacd-2060", jnode_is_unformatted (node) || znode_is_any_locked (JZNODE (node)));

	if (jnode_is_unformatted (node)) {

		/* Unformatted node case: Generate a key for the extent entry,
		 * search in the tree using coord_by_key, which handles
		 * locking for us. */
		struct inode *ino = jnode_page (node)->mapping->host;
		reiser4_key   key;
		file_plugin  *fplug = inode_file_plugin (ino);
		loff_t        loff = jnode_get_index (node) << PAGE_CACHE_SHIFT;

		assert ("jmacd-1812", coord != NULL);

		if ((ret = fplug->key_by_inode (ino, loff, & key))) {
			return ret;
		}

		if ((ret = coord_by_key (current_tree, & key, coord, parent_lh, parent_mode, FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL, CBK_UNIQUE)) != CBK_COORD_FOUND) {
			return ret;
		}

		if ((ret = load_zh (parent_zh, parent_lh->node))) {
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
		if (coord != NULL) {

			if ((ret = load_zh (parent_zh, parent_lh->node))) {
				warning ("jmacd-976812", "load_zh failed: %d", ret);
				return ret;
			}

			if ((ret = find_child_ptr (parent_lh->node, JZNODE (node), coord))) {
				warning ("jmacd-976812", "find_child_ptr failed: %d", ret);
				return ret;
			}
		}
	}

	return 0;
}

/* Get the right neighbor of a znode locked provided a condition is met.  The neighbor
 * must be dirty and a member of the same atom.  If there is no right neighbor or the
 * neighbor is not in memory or if there is a neighbor but it is not dirty or not in the
 * same atom, -ENAVAIL is returned. */
static int znode_get_utmost_if_dirty (znode *node, lock_handle *lock, sideof side, znode_lock_mode mode)
{
	znode *neighbor;
	int go;
	int ret;
	ON_DEBUG (int xcnt = -1);

	assert ("jmacd-6334", znode_is_connected (node));

	spin_lock_tree (current_tree);
	neighbor = side == RIGHT_SIDE ? node->right : node->left;
	if (neighbor != NULL) {
		ON_DEBUG (xcnt = atomic_read (& neighbor->x_count));
		zref (neighbor);
	}
	spin_unlock_tree (current_tree);

	if (neighbor == NULL) {
		return -ENAVAIL;
	}

	if (! (go = txn_same_atom_dirty (ZJNODE (node), ZJNODE (neighbor), 0, 0))) {
		ret = -ENAVAIL;
		goto fail;
	}

	if ((ret = reiser4_get_neighbor (lock, node, mode, side == LEFT_SIDE ? GN_GO_LEFT : 0))) {
		/* May return -ENOENT or -ENAVAIL. */
		/* FIXME: check EINVAL, EDEADLK */
		if (ret == -ENOENT) { ret = -ENAVAIL; }
		goto fail;
	}

	/* Can't assert is_dirty here, even though we checked it above,
	 * because there is a race when the tree_lock is released. */
        if (! znode_check_dirty (lock->node)) {
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
	int x;

	assert ("jmacd-7011", znode_is_write_locked (a));
	assert ("jmacd-7012", znode_is_write_locked (b));

	spin_lock_tree (current_tree);
	x = (a->ptr_in_parent_hint.node == b->ptr_in_parent_hint.node);
	spin_unlock_tree (current_tree);
	return x;
}

/********************************************************************************
 * FLUSH SCAN LEFT
 ********************************************************************************/

/* Initialize the flush_scan data structure. */
static void flush_scan_init (flush_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
	init_lh (& scan->parent_lock);
	init_zh (& scan->parent_load);
	init_zh (& scan->node_load);
	coord_init_invalid (& scan->parent_coord, NULL);
}

/* Release any resources held by the flush scan, e.g., release locks, free memory, etc. */
static void flush_scan_done (flush_scan *scan)
{
	done_zh (& scan->node_load);
	if (scan->node != NULL) {
		jput (scan->node);
		scan->node = NULL;
	}
	done_zh (& scan->parent_load);
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
	int go = txn_same_atom_dirty (scan->node, tonode, 1, 0);

	if (! go) {
		scan->stop = 1;
		trace_on (TRACE_FLUSH_VERB, "flush scan stop: stop at node %s\n", flush_jnode_tostring (scan->node));
		trace_on (TRACE_FLUSH_VERB, "flush scan stop: do not cont at %s\n", flush_jnode_tostring (tonode));
		jput (tonode);
	}

	return go;
}

/* Set the current scan->node, refcount it, increment size, and deref previous current. */
static int flush_scan_set_current (flush_scan *scan, jnode *node, unsigned add_size, const coord_t *parent)
{
	int ret;

	if ((ret = load_jh (& scan->node_load, node))) {
		return ret;
	}

	if (scan->node != NULL) {
		jput (scan->node);
	}

	scan->node  = node;
	scan->size += add_size;

	/* FIXME: This is a little inefficient. */
	if (parent != NULL) {
		coord_dup (& scan->parent_coord, parent);
	}

	return 0;
}

/* Return true if going left. */
static int flush_scanning_left (flush_scan *scan)
{
	return scan->direction == LEFT_SIDE;
}

/* Performs leftward scanning starting from an unformatted node and its parent coordinate */
static int flush_scan_extent_coord (flush_scan *scan, const coord_t *in_coord)
{
	coord_t coord;
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist; /* FIXME: is u64 right? */
	reiser4_block_nr unit_start;
	struct inode *ino = NULL;
	struct page *pg;
	int ret = 0, allocated, incr;

	coord_dup (& coord, in_coord);

	assert ("jmacd-1404", ! flush_scan_finished (scan));
	assert ("jmacd-1405", jnode_get_level (scan->node) == LEAF_LEVEL);
	assert ("jmacd-1406", jnode_is_unformatted (scan->node));

	scan_index = jnode_get_index (scan->node);

	assert ("jmacd-7889", item_is_extent (& coord));

	trace_on (TRACE_FLUSH_VERB, "%s scan starts %lu: %s\n",
		  (flush_scanning_left (scan) ? "left" : "right"),
		  scan_index,
		  flush_jnode_tostring (scan->node));

 repeat:
	/* If the get_inode call is expensive we can be a bit more clever... */
	extent_get_inode (& coord, & ino);

	if (ino == NULL) {
		scan->stop = 1;
		return 0;
	}

	trace_on (TRACE_FLUSH_VERB, "%s scan index %lu: parent %p inode %lu\n",
		  (flush_scanning_left (scan) ? "left" : "right"),
		  scan_index, coord.node, ino->i_ino);

	/* If not allocated, the entire extent must be dirty and in the same atom.
	 * (Actually, I'm not sure this is properly enforced, but it should be the
	 * case since one atom must write the parent and the others must read
	 * it). */
	allocated  = ! extent_is_unallocated (& coord);
	unit_index = extent_unit_index (& coord);
	unit_width = extent_unit_width (& coord);
	unit_start = extent_unit_start (& coord);

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

	/* If the extent is allocated we have to check each of its blocks.  If the extent
	 * is unallocated we can skip to the scan_max. */
	if (allocated) {

		do {
			/* Note: On the very first pass through this block we test
			 * the current position. */
			pg = find_get_page (ino->i_mapping, scan_index);

			if (pg == NULL) {
				goto stop_same_parent;
			}

			neighbor = jnode_of_page (pg);

			page_cache_release (pg);

			if (neighbor == NULL) {
				ret = -ENOMEM;
				goto exit;
			}

			trace_on (TRACE_FLUSH_VERB, "alloc scan index %lu: %s\n", scan_index, flush_jnode_tostring (neighbor));

			if (scan->node != neighbor && ! flush_scan_goto (scan, neighbor)) {
				goto stop_same_parent;
			}

			if ((ret = flush_scan_set_current (scan, neighbor, 1, & coord))) {
				goto exit;
			}

			scan_index += incr;

		} while (incr + scan_max != scan_index);

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

		if (neighbor == NULL) {
			ret = -ENOMEM;
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "unalloc scan index %lu: %s\n", scan_index, flush_jnode_tostring (neighbor));

		assert ("jmacd-3551", ! jnode_check_allocated (neighbor) && txn_same_atom_dirty (neighbor, scan->node, 0, 0));

		if ((ret = flush_scan_set_current (scan, neighbor, scan_dist, & coord))) {
			goto exit;
		}
	}

	if (coord_sideof_unit (& coord, scan->direction) == 0 && item_is_extent (& coord)) {
		/* Continue as long as there are more extent units. */

		scan_index = extent_unit_index (& coord) + (flush_scanning_left (scan) ? extent_unit_width (& coord) - 1 : 0);
		goto repeat;
	}

	if (0) {
	stop_same_parent:

		/* If we are scanning left and we stop in the middle of an allocated
		 * extent, we know the preceder immediately. */
		if (flush_scanning_left (scan)) {
			scan->preceder_blk = unit_start + scan_index;
		}

		/* In this case, we leave coord set to the parent of scan->node. */
		scan->stop = 1;

	} else {
		/* In this case, we are still scanning, coord is set to the next item which is
		 * either off-the-end of the node or not an extent. */
		assert ("jmacd-8912", scan->stop == 0);
		assert ("jmacd-7812", (coord_is_after_sideof_unit (& coord, scan->direction) || ! item_is_extent (& coord)));
	}

	/* This asserts the invariant. */
	/*jnode_check_parent_coord (scan->node, & scan->parent_coord);*/

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
	int ret = 0;
	lock_handle next_lock;
	load_handle next_load;
	coord_t next_coord;
	jnode *child;

	init_lh (& next_lock);
	init_zh (& next_load);

	for (; ! flush_scan_finished (scan); skip_first = 0) {
		/* Either skip the first item (formatted) or scan the first extent. */
		if (skip_first == 0) {
			assert ("jmacd-1230", item_is_extent (& scan->parent_coord));

			if ((ret = flush_scan_extent_coord (scan, & scan->parent_coord))) {
				goto exit;
			}

			if (flush_scan_finished (scan)) {
				break;
			}
		} else {
			assert ("jmacd-1231", item_is_internal (& scan->parent_coord));
		}

		/* Either way, the invariant is that scan->parent_coord is set to the
		 * parent of scan->node.  Now get the next item. */
		coord_dup (& next_coord, & scan->parent_coord);
		coord_sideof_unit (& next_coord, scan->direction);

		/* If off-the-end, try the next twig. */
		if (coord_is_after_sideof_unit (& next_coord, scan->direction)) {

			ret = znode_get_utmost_if_dirty (next_coord.node, & next_lock, scan->direction, ZNODE_WRITE_LOCK /*ZNODE_READ_LOCK*/);

			if (ret == -ENAVAIL) { scan->stop = 1; ret = 0; break; }

			if (ret != 0) { goto exit; }

			if ((ret = load_zh (& next_load, next_lock.node))) {
				goto exit;
			}

			coord_init_sideof_unit (& next_coord, next_lock.node, sideof_reverse (scan->direction));
		}

		/* If skip_first was set, then we are only interested in continuing if the
		 * next item is an extent.  If this is not the case, stop now.
		 * (Otherwise, if the next item is an extent we will return and the next
		 * call will be to scan_formatted()). */
		if (! item_is_extent (& next_coord) && skip_first) {
			scan->stop = 1;
			break;
		}

		/* Get the next child. */
		if ((ret = item_utmost_child (& next_coord, sideof_reverse (scan->direction), & child))) {
			goto exit;
		}

		if (child == NULL) {
			scan->stop = 1;
			break;
		}

		/* See if it is dirty, part of the same atom. */
		if (! flush_scan_goto (scan, child)) {
			break;
		}

		/* If so, make it current. */
		if ((ret = flush_scan_set_current (scan, child, 1, & next_coord))) {
			goto exit;
		}

		/* Now continue.  If formatted we release the parent lock and return, then
		 * proceed. */
		if (jnode_is_formatted (child)) {
			break;
		}

		/* Otherwise, repeat the above loop with next_coord. */
		if (next_load.node != NULL) {
			done_lh (& scan->parent_lock);
			move_lh (& scan->parent_lock, & next_lock);
			move_zh (& scan->parent_load, & next_load);
		}

		coord_dup (& scan->parent_coord, & next_coord);

		assert ("jmacd-1239", item_is_extent (& scan->parent_coord));
	}

	assert ("jmacd-6233", flush_scan_finished (scan) || jnode_is_formatted (scan->node));
 exit:
	if (jnode_is_formatted (scan->node)) {
		done_lh (& scan->parent_lock);
		done_zh (& scan->parent_load);
	}

	done_zh (& next_load);
	done_lh (& next_lock);
	return ret;
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
	int ret;
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

		trace_on (TRACE_FLUSH_VERB, "format scan %s %s\n",
			  flush_scanning_left (scan) ? "left" : "right",
			  flush_znode_tostring (neighbor));

		/* Check the condition for going left, break if it is not met,
		 * release left reference. */
		if (! flush_scan_goto (scan, ZJNODE (neighbor))) {
			break;
		}

		/* Advance the flush_scan state to the left. */
		if ((ret = flush_scan_set_current (scan, ZJNODE (neighbor), 1, NULL))) {
			return ret;
		}

	} while (! flush_scan_finished (scan));

	/* If neighbor is NULL then we reached the end of a formatted region, or else the
	 * sibling is out of memory, now check for an extent to the left (as long as
	 * LEAF_LEVEL). */
	if (neighbor != NULL || jnode_get_level (scan->node) != LEAF_LEVEL || flush_scan_finished (scan)) {
		scan->stop = 1;
		return 0;
	}

	/* Otherwise, continue at the parent level. */
	{
		int ret;
		lock_handle end_lock;

		init_lh (& end_lock);

		/* Need the node locked to get the parent lock. */
		if ((ret = longterm_lock_znode (& end_lock, JZNODE (scan->node), ZNODE_WRITE_LOCK /*ZNODE_READ_LOCK*/, ZNODE_LOCK_LOPRI))) {
			/* EINVAL or EDEADLK here mean... try again!  At this point we've
			 * scanned too far.  Seems right to start over. */
			return ret;
		}

		ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, & scan->parent_load, ZNODE_WRITE_LOCK /*ZNODE_READ_LOCK*/);
		/* FIXME: check EINVAL, EDEADLK */

		done_lh (& end_lock);

		if (ret != 0) { return ret; }

		return flush_scan_extent (scan, 1/* skip_first*/);
	}
}

/* Performs leftward scanning starting from either kind of node.  Counts the starting node. */
static int flush_scan_left (flush_scan *scan, flush_scan *right, jnode *node, __u32 limit)
{
	int ret;

	scan->max_size  = limit;
	scan->direction = LEFT_SIDE;

	if ((ret = flush_scan_set_current (scan, jref (node), 1, NULL))) {
		return ret;
	}

	return flush_scan_common (scan, right);
}

/* Performs rightward scanning... Does not count the starting node. */
static int flush_scan_right (flush_scan *scan, jnode *node, __u32 limit)
{
	int ret;

	scan->max_size  = limit;
	scan->direction = RIGHT_SIDE;

	if ((ret = flush_scan_set_current (scan, jref (node), 0, NULL))) {
		return ret;
	}

	ret = flush_scan_common (scan, NULL);

	/* All we need is a count, so release it now. */
	flush_scan_done (scan);

	return ret;
}

/* Performs left or right scanning. */
static int flush_scan_common (flush_scan *scan, flush_scan *other)
{
	int ret;

	/* Special case for starting at an unformatted node.  Optimization: we only want
	 * to search for the parent (which requires a tree traversal) once.  Obviously, we
	 * shouldn't have to call it once for the left scan and once for the right
	 * scan. */
	if (jnode_is_unformatted (scan->node)) {

		if (coord_is_invalid (& scan->parent_coord)) {

			if ((ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, & scan->parent_load, ZNODE_WRITE_LOCK /*ZNODE_READ_LOCK*/))) {
				/* FIXME: check EINVAL, EDEADLK */
				return ret;
			}

			assert ("jmacd-8661", other != NULL);

			/* Duplicate the reference into the other flush_scan. */
			coord_dup (& other->parent_coord, & scan->parent_coord);
			copy_lh (& other->parent_lock, & scan->parent_lock);
			copy_zh (& other->parent_load, & scan->parent_load);
		}

		if ((ret = flush_scan_extent (scan, 0/*skip_first*/))) {
			return ret;
		}
	}

	/* In all other cases, scan_formatted does evertyhing.  If it reaches a
	 * formatted-leaf with NULL sibling it checks for an extent and calls scan_extent
	 * directly. */
	while (! flush_scan_finished (scan)) {

		if ((ret = flush_scan_formatted (scan))) {
			return ret;
		}
	}

	return 0;
}

/********************************************************************************
 * FLUSH POS HELPERS
 ********************************************************************************/

/* Initialize the fields of a flush_position. */
static int flush_pos_init (flush_position *pos, int *nr_to_flush)
{
	if ((pos->queue = reiser4_kmalloc (FLUSH_QUEUE_SIZE * sizeof (jnode*), GFP_NOFS)) == NULL) {
		return -ENOMEM;
	}
	memset (pos->queue, 0, FLUSH_QUEUE_SIZE * sizeof (jnode*));

	pos->queue_num = 0;
	pos->point = NULL;
	pos->leaf_relocate = 0;
	pos->alloc_cnt = 0;
	pos->enqueue_cnt = 0;
	pos->nr_to_flush = nr_to_flush;

	coord_init_invalid (& pos->parent_coord, NULL);

	blocknr_hint_init (& pos->preceder);
	init_lh (& pos->point_lock);
	init_lh (& pos->parent_lock);
	init_zh (& pos->parent_load);
	init_zh (& pos->point_load);

	return 0;
}

/* FIXME: comment */
static int flush_pos_valid (flush_position *pos)
{
	if (pos->nr_to_flush != NULL && pos->enqueue_cnt >= *pos->nr_to_flush) {
		return 0;
	}
	return pos->point != NULL || lock_mode (& pos->parent_lock) != ZNODE_NO_LOCK;
}

/* Release any resources of a flush_position. */
static void flush_pos_done (flush_position *pos)
{
	flush_pos_stop (pos);
	blocknr_hint_done (& pos->preceder);
	if (pos->queue != NULL) {
		int i;
		for (i = 0; i < pos->queue_num; i += 1) {
			if (pos->queue[i] != NULL) {
				jput (pos->queue[i]);
			}
		}
		kfree (pos->queue);
		pos->queue = NULL;
	}
}

/* Reset the point and parent. */
static int flush_pos_stop (flush_position *pos)
{
	done_zh (& pos->parent_load);
	done_zh (& pos->point_load);
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_lh (& pos->point_lock);
	done_lh (& pos->parent_lock);
	coord_init_invalid (& pos->parent_coord, NULL);
	return 0;
}

/* FIXME: comments. */
static int flush_pos_to_child_and_alloc (flush_position *pos)
{
	int ret;
	jnode *child;

	assert ("jmacd-6078", flush_pos_unformatted (pos));
	assert ("jmacd-6079", lock_mode (& pos->point_lock) == ZNODE_NO_LOCK);
	assert ("jmacd-6080", pos->point_load.node == NULL);

	trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: %s\n", flush_pos_tostring (pos));

	/* Get the child if it is memory, lock it, unlock the parent. */
	if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child))) {
		return ret;
	}

	if (child == NULL) {
		trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: STOP (no child): %s\n", flush_pos_tostring (pos));
		goto stop;
	}

	if (! jnode_check_dirty (child)) {
		trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: STOP (not dirty): %s\n", flush_pos_tostring (pos));
		jput (child);
		goto stop;
	}

	assert ("jmacd-8861", jnode_is_formatted (child));

	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}

	pos->point = child;

	if ((ret = longterm_lock_znode (& pos->point_lock, JZNODE (child), ZNODE_WRITE_LOCK /*ZNODE_READ_LOCK*/, ZNODE_LOCK_LOPRI))) {
		return ret;
	}

	if (! jnode_check_allocated (child) && (ret = flush_allocate_znode (JZNODE (child), & pos->parent_coord, pos))) {
		return ret;
	}

	if (0) {
	stop:
		if ((ret = flush_release_ancestors (pos->parent_lock.node))) {
			return ret;
		}
		return flush_pos_stop (pos);
	}

	/* And keep going... */
	done_zh (& pos->parent_load);
	done_lh (& pos->parent_lock);
	coord_init_invalid (& pos->parent_coord, NULL);
	return 0;
}

/* FIXME: comments. */
static int flush_pos_to_parent (flush_position *pos)
{
	int ret;

	assert ("jmacd-6078", ! flush_pos_unformatted (pos));

	/* Lock the parent, find the coordinate. */
	if ((ret = jnode_lock_parent_coord (pos->point, & pos->parent_coord, & pos->parent_lock, & pos->parent_load, ZNODE_WRITE_LOCK))) {
		/* FIXME: check EINVAL, EDEADLK */
		return ret;
	}

	/* When this is called, we have already tried the sibling link of the znode in
	 * question, therefore we are not interested in saving ->point. */
	done_zh (& pos->point_load);
	done_lh (& pos->point_lock);

	/* Note: we leave the point set, but unlocked/unloaded. */
	/* FIXME: This is a bad idea if the child can be deleted.... but it helps the call to left_relocate.  Needs a better solution. */
	return 0;
}

static int flush_pos_unformatted (flush_position *pos)
{
	/* FIXME: more asserts. */
	return pos->parent_lock.node != NULL;
}

static void flush_pos_release_point (flush_position *pos)
{
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_zh (& pos->point_load);
	done_lh (& pos->point_lock);
}

static int flush_pos_set_point (flush_position *pos, jnode *node)
{
	flush_pos_release_point (pos);
	pos->point = jref (node);
	return load_jh (& pos->point_load, node);
}

static int flush_pos_lock_parent (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, load_handle *parent_load, znode_lock_mode mode)
{
	int ret;

	if (flush_pos_unformatted (pos)) {
		/* In this case we already have the parent locked. */
		znode_lock_mode have_mode = lock_mode (& pos->parent_lock);

		/* For now we only deal with the case where the previously requested
		 * parent lock has the proper mode.  Otherwise we have to release the lock
		 * here and get a new one. */
		assert ("jmacd-9923", have_mode == mode);
		copy_lh (parent_lock, & pos->parent_lock);
		if ((ret = load_zh (parent_load, parent_lock->node))) {
			return ret;
		}
		coord_dup (parent_coord, & pos->parent_coord);
		return 0;

	} else {
		assert ("jmacd-9922", ! znode_is_root (JZNODE (pos->point)));
		assert ("jmacd-9924", lock_mode (& pos->parent_lock) == ZNODE_NO_LOCK);
		/* FIXME: check EINVAL, EDEADLK */
		return jnode_lock_parent_coord (pos->point, parent_coord, parent_lock, parent_load, mode);
	}
}

reiser4_blocknr_hint* flush_pos_hint (flush_position *pos)
{
	return & pos->preceder;
}

int flush_pos_leaf_relocate (flush_position *pos)
{
	return pos->leaf_relocate;
}

#if REISER4_DEBUG
static void flush_jnode_tostring_internal (jnode *node, char *buf)
{
	const char* state;
	char atom[32];
	char block[32];
	char items[32];
	int fmttd;
	int dirty;

	spin_lock_jnode (node);

	fmttd = !JF_ISSET (node, ZNODE_UNFORMATTED);
	dirty = JF_ISSET (node, ZNODE_DIRTY);

	sprintf (block, " block=%llu", *jnode_get_block (node));

	if (JF_ISSET (node, ZNODE_WANDER)) {
		state = dirty ? "wandr,dirty" : "wandr";
	} else if (JF_ISSET (node, ZNODE_RELOC) && JF_ISSET (node, ZNODE_CREATED)) {
		state = dirty ? "creat,dirty" : "creat";
	} else if (JF_ISSET (node, ZNODE_RELOC)) {
		state = dirty ? "reloc,dirty" : "reloc";
	} else if (JF_ISSET (node, ZNODE_CREATED)) {
		assert ("jmacd-61554", dirty);
		state = "fresh";
		block[0] = 0;
	} else {
		state = dirty ? "dirty" : "clean";
	}

	if (node->atom == NULL) {
		atom[0] = 0;
	} else {
		sprintf (atom, " atom=%u", node->atom->atom_id);
	}

	items[0] = 0;
	if (! fmttd) {
		sprintf (items, " index=%lu", jnode_get_index (node));
	}

	sprintf (buf+strlen(buf),
		 "%s=%p [%s%s%s level=%u%s%s]",
		 fmttd ? "z" : "j",
		 node,
		 state,
		 atom,
		 block,
		 jnode_get_level (node),
		 items,
		 JF_ISSET (node, ZNODE_FLUSH_BUSY) ? " fbusy" : "");

	spin_unlock_jnode (node);
}

static const char* flush_znode_tostring (znode *node)
{
	return flush_jnode_tostring (ZJNODE (node));
}

static const char* flush_jnode_tostring (jnode *node)
{
	static char fmtbuf[256];
	fmtbuf[0] = 0;
	flush_jnode_tostring_internal (node, fmtbuf);
	return fmtbuf;
}

static const char* flush_pos_tostring (flush_position *pos)
{
	static char fmtbuf[256];
	load_handle load;
	fmtbuf[0] = 0;

	init_zh (& load);

	if (pos->parent_lock.node != NULL) {

		assert ("jmacd-79123", pos->parent_lock.node == pos->parent_load.node);

		strcat (fmtbuf, "par:");
		flush_jnode_tostring_internal (ZJNODE (pos->parent_lock.node), fmtbuf);

		if (load_zh (& load, pos->parent_lock.node)) {
			return "*error*";
		}

		if (coord_is_before_leftmost (& pos->parent_coord)) {
			sprintf (fmtbuf+strlen(fmtbuf), "[left]");
		} else if (coord_is_after_rightmost (& pos->parent_coord)) {
			sprintf (fmtbuf+strlen(fmtbuf), "[right]");
		} else {
			sprintf (fmtbuf+strlen(fmtbuf), "[%s i=%u/%u",
				 coord_tween_tostring (pos->parent_coord.between),
				 pos->parent_coord.item_pos,
				 node_num_items (pos->parent_coord.node));

			if (! coord_is_existing_item (& pos->parent_coord)) {
				sprintf (fmtbuf+strlen(fmtbuf), "]");
			} else {

				sprintf (fmtbuf+strlen(fmtbuf), ",u=%u/%u %s]",
					 pos->parent_coord.unit_pos,
					 coord_num_units (& pos->parent_coord),
					 coord_is_existing_unit (& pos->parent_coord) ?
					 (item_is_extent (& pos->parent_coord) ?
					  "ext" :
					  (item_is_internal (& pos->parent_coord) ? "int" : "other")) :
					 "tween");
			}
		}
	} else if (pos->point != NULL) {
		strcat (fmtbuf, "pt:");
		flush_jnode_tostring_internal (pos->point, fmtbuf);
	}

	done_zh (& load);
	return fmtbuf;
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
