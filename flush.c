
/*

The use of atomic commits dramatically impacts the use of LRU as the
basis for page cleaning (though using it for clean page discarding is
still effective.)

The use of write clustering dramatically impacts the use of LRU as the
basis for page cleaning.

ReiserFS v4 uses both.

We will not use LRU in v4.0 of reiserfs, and then in later versions we
may gradually partially reintroduce it.

Optimizations to make on flush:

* block (re)allocation

* tail conversion

* extent formation

* node repacking

* wandering log definition


Memory Pressure:

There are kinds of memory pressure:

* general lack of memory for processes requesting it

* too much dirty memory

* dirty memory is too old and should be more permanently preserved on disk

* particular page needs freeing for DMA setup

[All programmers should understand that I expect strict observance of the
following taboo: you will not add an unnecessary copying of data, while coding
this.  I cannot understand why there is resistance to this, but I keep seeing
code which ignores this.]


Unlike clean pages, dirty memory must be written to disk before being
freed for other use.  It also may require processing that will require
more memory before it can be cleaned by writing it to disk.  This
processing makes it vulnerable to deadlocks in extreme cases.
Precisely reserving enough memory to allow that extra processing
without deadlock is often difficult.

reiser4 limits its usage of dirty pages to 75%, which is enough to
ensure that the extra processing will not cause the system to run out
of memory.  More safeguards are possible, including letting commit
choose to swap, but we will wait until this rather simple mechanism
has a problem in practice before elaborating it.

reiser4 supports the reiserfs_flush(page) command for cleaning pages

If the Linux VM system was properly designed, it would be based upon
memory sub-managers each reflecting some common principles of
responding to pressure that is in proportion to their size.  Linux
used to have multiple caches, these caches had no understand of how
large they were, they made no attempt to proportionalize the pressure
upon each cache, and their management was generally badly designed
without any effective mechanism for ensuring that the caches did not
get out of balance with each other.  From this it was concluded that
there should be only one unified cache, rather than designing an
effective mechanism for expressing to each subcache a sense of
pressure in proportion to the size of the subcache, and requiring that
the subcache embody some effective mechanism for responding to that
sense of pressure.

The unified cache is indeed better than badly designed multiple
caches.  It does however perform very poorly at handling caches of
objects that are not page sized.

Linus says it already has a subcache manager design, we just need to
use writepage.  Ok, fine, we will be the first subcache.

So, understand that in reiserfs, writepage does not write pages, it
pressures the reiserfs memory manager, and understand that the place a
page has on the various mm lists does not determine when it gets
written out, it merely determines when it triggers the next pressure
on the reiserfs memory manager.

What reiser4 does is interpret pressure on a page as pressure on a
subcache within reiserfs.

Write clustering, transaction commits, objects whose value to cache is
out of proportion to the number of bytes consumed by them, caches
whose working set size and pattern of access is known to the
application, and those occasions when other factors knowable to the
filesystem or application but not the OS generally are important to
deciding what to eject, and objects much smaller than a page with no
correlation of references for objects on the same page, or larger than
a page with a complete correlation between their pages, are good example of when cache
submanagers should be employed.

 */

/* You should read crypt.c and then return. */
/* You should read block_alloc.c and then return. */

#include "reiser4.h"

static void          slum_scan_init               (slum_scan *scan);
static void          slum_scan_cleanup            (slum_scan *scan);
static int           slum_scan_left_finished      (slum_scan *scan);
static int           slum_scan_left_extent        (slum_scan *scan, jnode *node);
static int           slum_scan_left_formatted     (slum_scan *scan, znode *node);
static void          slum_scan_set_current        (slum_scan *scan, jnode *node);
static int           slum_scan_left               (slum_scan *scan, jnode *node);
static int           slum_lock_left_ancestor      (jnode *node, jnode **left_ancestor, reiser4_lock_handle *left_ancestor_lock);
static int            slum_allocate_and_squeeze_parent_first (jnode *gda);

static int           jnode_lock_parent_coord      (jnode *node, reiser4_lock_handle *node_lh, reiser4_lock_handle *parent_lh,
						   tree_coord *coord, znode_lock_mode mode);
static int           jnode_is_allocated           (jnode *node);
static jnode*        jnode_get_neighbor_in_memory (jnode *node UNUSED_ARG, unsigned long node_index UNUSED_ARG);
static unsigned long jnode_get_index              (jnode *node);
static int           jnode_parent_equals          (jnode *node, znode *parent);
static int           jnode_allocate               (jnode *node);

#define FLUSH_WORKS 0

/* First phase:
 *
 * If leaf-level: scan to left
 *
 * If not leaf-level: scan to left in parent-first-order, meaning, if the left
 * neighbor's rightmost descendent is in memory, scan from there, otherwise
 * stop.  I don't think this makes sense because we don't know if that
 * descendant will continue to be if we squeeze, which we should do first,
 * which means we should scan at this level.
 *
 * If stopped at leftmost leaf, mark for relocate (dirty parent)
 *
 * Go to parent, try to squeeze left, scan left at parent level first.
 *
 */

/* Perform encryption, allocate-on-flush, and squeezing-left of slums. */
int flush_jnode_slum (jnode *node)
{
	int ret;
	jnode *gda = NULL; /* Greatest dirty ancestor */
	reiser4_lock_handle gda_lock;

	reiser4_init_lh (& gda_lock);

	/* FIXME: comments out of date */
	if (FLUSH_WORKS && (ret = slum_lock_left_ancestor (node, & gda, & gda_lock))) {
		goto failed;
	}

	if (FLUSH_WORKS && (ret = slum_allocate_and_squeeze_parent_first (gda))) {
		goto failed;
	}

	/* FIXME: The txnmgr expects this to clean the node.  (i.e., move it
	 * to the clean list).  That's all this does for now. */
	jnode_set_clean (node);
	ret = 0;

   failed:

	if (gda != NULL) {
		jput (gda);
	}
	reiser4_done_lh (& gda_lock);
	return ret;
}

/********************************************************************************
 * SLUM_LOCK_LEFT_ANCESTOR
 ********************************************************************************/

/* This function is called on @level_node, which which the search for the
 * greatest dirty ancestor begins.  This procedure is recursive (up the tree).
 *
 * A slum_scan_left is performed starting from @level_node.  At the end of the
 * slum, the parent is locked.  If we are going to relocate the left end of
 * the slum on this level, then set its parent dirty before going further.
 *
 * If the parent (at the left end of the slum) is dirty, repeat process going
 * upward until the leftmost, higest, dirty node is found, which we call the
 * greatest dirty ancestor.  Returns with only the greatest dirty ancestor
 * locked (and referenced), unless the node is unformatted, in which case only
 * a reference is returned.
 */
static int slum_lock_left_ancestor (jnode *level_node, jnode **left_ancestor, reiser4_lock_handle *left_ancestor_lock)
{
	int ret;
	int relocate_child;
	reiser4_lock_handle parent_lock;
	slum_scan           level_scan;

	slum_scan_init  (& level_scan);
	reiser4_init_lh (& parent_lock);

	/* Scan parent level for the leftmost dirty */
	if ((ret = slum_scan_left (& level_scan, level_node))) {
		goto failure;
	}

	/* Actual relocation policy will be:
	 *
	 *   (leftmost_of_parent && (is_leaf || leftmost_child_is_relocated))
	 *
	 * or so I think, at least. */
#define flush_should_relocate(x) 1

	relocate_child = flush_should_relocate (node);

	/* We're at the end of a level slum, read lock the parent.  Don't need
	 * a write-lock yet because we do not start squeezing until the
	 * greatest dirty ancestor is found.
	 *
	 * FIXME_JMACD: if node is root, reiser4_get_parent() returns error.
	 */
	if ((ret = reiser4_get_parent (& parent_lock, JZNODE (level_scan.node), ZNODE_READ_LOCK, 1))) {
		goto failure;
	}

	/* If relocating, artificially dirty the parent right now. */
	if (relocate_child) {
		znode_set_dirty (parent_lock.node);
	}

	/* If the parent is dirty, it needs to be squeezed also, recurse upwards */
	if (znode_is_dirty (parent_lock.node)) {

		/* Recurse upwards. */
		if ((ret = slum_lock_left_ancestor (ZJNODE (parent_lock.node), left_ancestor, left_ancestor_lock))) {
			goto failure;
		}

	} else {
		/* End the recursion, get a write lock at the highest level. */
		(*left_ancestor) = jref (level_scan.node);

		if (jnode_is_formatted (level_scan.node) &&
		    (ret = reiser4_lock_znode (left_ancestor_lock, JZNODE (level_scan.node), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
			goto failure;
		}
	}

 failure:
	slum_scan_cleanup (& level_scan);
	reiser4_done_lh   (& parent_lock);

	return 0;
}

/********************************************************************************
 * SLUM ALLOCATE AND SQUEEZE
 ********************************************************************************/

static int slum_allocate_and_squeeze_children (znode *node)
{
	int ret;
	tree_coord crd;

	reiser4_init_coord (& crd);
	crd.node = node;

	coord_first_unit (& crd);

	assert ("jmacd-2000", ! coord_after_last (& crd));

	do {
		item_plugin *item = item_plugin_by_coord (& crd);

		switch (item->item_type) {
		case EXTENT_ITEM_TYPE:

			/* FIXME: */
			/*allocate_extent_item (item);*/
			break;

		case INTERNAL_ITEM_TYPE: {

			znode *child = child_znode (& crd, 1);

			if (IS_ERR (child)) { return PTR_ERR (child); }

			if (! znode_is_dirty (child)) { continue; }

			if ((ret = slum_allocate_and_squeeze_parent_first (ZJNODE (child)))) {
				return ret;
			}
			break;
		}
		default:
			warning ("jmacd-2001", "Unexpected item type");
			print_znode ("node", node);
			return -EIO;
		}

	} while (! coord_next_unit (& crd));

	return 0;
}

static int slum_allocate_and_squeeze_parent_first (jnode *node)
{
	int ret;

        /* Stop recursion if its not dirty, meaning don't allocate children
         * either.  Children might be dirty but there is an overwrite below
         * this level or else this node would be dirty. */
        if (! jnode_is_dirty (node)) {
                return 0;
        }

	/* Allocate (parent) first. It might be allocated already. */
        if (! jnode_is_allocated (node)) {

                if (jnode_is_unformatted (node)) {
                        /* We got here because the unformatted node is not
			 * being relocated.  Otherwise the parent would be
			 * dirty (and this recursive function does not descend
			 * * to unformatted nodes).  Since the node is in the
			 * overwrite set, there's no allocation to do. */
			return 0;
                }

		/* Allocate it. */
                jnode_allocate (node);

                /* Recursive case: */
                if ((jnode_get_level (node) > LEAF_LEVEL) &&
		    (ret = slum_allocate_and_squeeze_children (JZNODE (node)))) {
			return ret;
                }

        } else {
                /* We went through this node already. We are back here because
                 * its right neighbor now has the same parent. */

		/* FIXME_JMACD: VS: I don't understand. */
	}

#if 0
        /*
         * @node and everything below it are squeezed and allocated
         */
        if (jnode_is_formatted (node->right) && is_dirty (node->right)) {
                /*
                 * Now we try to move into @node content of its right
                 * neighbor. Moving stops whenever one unit of internal item
                 * and, therefore, whole subtree is moved
                 */
                while (squeeze_to_left (node, node->right) == subtree_moved) {
                        /*
                         * last item in @node is internal item. Its last unit
                         * was just moved from node->right, therefore, it
                         * points to subtree which still has to be allocated &
                         * squeezed.
                         */
                        assert (is_internal_item (last_item (node)));

                        /*
                         * FIXME-VS: the below complication can be avoided if
                         * we can disregard the possibility of merging new last
                         * child of @node with its left neighbor
                         */
                        if (is_internal_item (last_but_one (node)) &&
                            jnode_is_dirty (internal_item_child (last_but_one (node))) {
                                /*
                                 * we may have to squeeze moved child with old
                                 * last child
                                 */
                                allocate_and_squeeze_parent_first (internal_item_child (last_but_one (node)));
			} else {
                                /*
                                 * there is nothing to the left of moved child
                                 * we can squeeze it with
                                 */
                                allocate_and_squeeze_parent_first (internal_item_child (last_item (node)));
			}
                }
        }
#endif 

	return 0;
}

/********************************************************************************
 * SLUM JNODE INTERFACE
 ********************************************************************************/

/* Gets the sibling of an unformatted jnode using its index, only if it is in memory, and
 * reference it. */
static jnode* jnode_get_neighbor_in_memory (jnode *node UNUSED_ARG, unsigned long node_index UNUSED_ARG)
{
	/* FIXME_JMACD: jref (), consult with vs. */
	not_yet ("jmacd-1700", "");
	return NULL;
}

/* Get the index of a block. */
static unsigned long jnode_get_index (jnode *node)
{
	not_yet ("jmacd-1700", "");
	return node->pg->index;
}

/* return true if "node" is allocated */
static int jnode_is_allocated (jnode *node UNUSED_ARG)
{
	/* FIXME_JMACD: */
	return 1;
}

static int jnode_allocate (jnode *node UNUSED_ARG)
{
	/* FIXME_JMACD: */
	return 0;
}

static int jnode_parent_equals (jnode *node, znode *parent)
{
	if (jnode_is_formatted (node)) {
		return (JZNODE (node)->ptr_in_parent_hint.node == parent);
	} else {
		/* FIXME: */
		return 0;
	}
}

/* Lock a node (if formatted) and then get its parent of a jnode locked, get
 * the coordinate. */
static int
jnode_lock_parent_coord (jnode *node,
			 reiser4_lock_handle *node_lh,
			 reiser4_lock_handle *parent_lh,
			 tree_coord *coord,
			 znode_lock_mode parent_mode)
{
	int ret;

	if (jnode_is_unformatted (node)) {

		/* FIXME_JMACD: how do we do this? */
		not_yet ("jmacd-1700", "");

	} else {
		/* Formatted node case: */

		/* Lock the node itself, which is necessary for getting its
		 * parent. FIXME_JMACD: Not sure LOPRI is correct. */

		if ((ret = reiser4_lock_znode (node_lh, JZNODE (node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
			return ret;
		}

		/* Get the parent read locked.
		 *
		 * FIXME_JMACD: if node is root, reiser4_get_parent() returns
		 * error.
		 */
		if ((ret = reiser4_get_parent (parent_lh, JZNODE (node), parent_mode, 1))) {
			return ret;
		}

		/* Make the child's position "hint" up-to-date. */
		if ((ret = find_child_ptr (parent_lh->node, JZNODE (node), coord))) {
			return ret;
		}
	}

	return 0;
}

/********************************************************************************
 * SLUM SCAN LEFT
 ********************************************************************************/

/* Initialize the slum_scan data structure. */
static void slum_scan_init (slum_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
	reiser4_init_lh (& scan->node_lock);
}

/* Release any resources held by the slum scan, e.g., release locks, free memory, etc. */
static void slum_scan_cleanup (slum_scan *scan)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}

	reiser4_done_lh (& scan->node_lock);
}

/* Returns true if leftward slum scanning is finished. */
static int slum_scan_left_finished (slum_scan *scan)
{
	return scan->stop || scan->size >= SLUM_SCAN_MAXNODES;
}

/* Return true if the scan should continue to the left.  If not, deref the "left" node and stop the scan. */
static int slum_scan_goleft (slum_scan *scan, jnode *left)
{
	int goleft;

	/* Spin lock the left node to check its state. */
	spin_lock_jnode (left);

	goleft = ((jnode_is_unformatted (left) ? 1 : znode_is_connected (JZNODE (left))) &&
		  jnode_is_dirty (left) &&
		  (scan->atom == left->atom));

	spin_unlock_jnode (left);

	if (! goleft) {
		jput (left);
		scan->stop = 1;
	}

	return goleft;
}

/* Set the current scan->node, refcount it, increment size, and deref previous current. */
static void slum_scan_set_current (slum_scan *scan, jnode *node)
{
	if (scan->node != NULL) {
		jput (scan->node);
	}

	scan->size += 1;
	scan->node  = node;
}

/* Perform a single leftward step using the parent, finding the next-left item, and
 * descending.  Only used at the left-boundary of an extent or range of znodes. */
static int slum_scan_left_using_parent (slum_scan *scan)
{
	int ret;
	reiser4_lock_handle node_lh, parent_lh, left_parent_lh;
	tree_coord coord;
	item_plugin *iplug;
	jnode *child_left;

	assert ("jmacd-1403", ! slum_scan_left_finished (scan));

	reiser4_init_coord (& coord);
	reiser4_init_lh    (& node_lh);
	reiser4_init_lh    (& parent_lh);
	reiser4_init_lh    (& left_parent_lh);

	if ((ret = jnode_lock_parent_coord (scan->node, & node_lh, & parent_lh, & coord, ZNODE_READ_LOCK))) {
		goto done;
	}

	/* Finished with the node lock. */
	reiser4_done_lh (& node_lh);

	/* Shift the coord to the left. */
	if ((ret = coord_prev (& coord)) != 0) {
		/* If coord_prev returns 1, coord is already leftmost of its node. */

		/* Lock the left-of-parent node, but don't read into memory.  ENOENT means not-in-memory. */
		if ((ret = reiser4_get_left_neighbor (& left_parent_lh, parent_lh.node, ZNODE_READ_LOCK, 0)) && (ret != -ENOENT)) {
			goto done;
		}

		if (ret == 0) {
			/* Release parent lock -- don't need it any more. */
			reiser4_done_lh (& parent_lh);

			/* Set coord to the rightmost position of the left-of-parent node. */
			coord.node = left_parent_lh.node;
			coord_last_unit (& coord);

			ret = 0;
			coord.node = left_parent_lh.node;
			coord_last_unit (& coord);

			ret = 0;
		}
	}

	/* Get the item plugin. */
	iplug = item_plugin_by_coord (& coord);

	/* If the item has no utmost_child routine (odd), or if the child is not in memory, stop. */
	if (iplug->utmost_child == NULL || (child_left = iplug->utmost_child (& coord, RIGHT_SIDE)) == NULL) {
		scan->stop = 1;
		ret = 0;
		goto done;
	}

	if (IS_ERR (child_left)) {
		ret = PTR_ERR (child_left);
		goto done;
	}

	/* We've found a child to the left, now see if we should continue the scan.  If not then scan->stop is set. */
	if (slum_scan_goleft (scan, child_left)) {
		slum_scan_set_current (scan, child_left);
	}

	/* Release locks. */
 done:
	reiser4_done_lh (& node_lh);
	reiser4_done_lh (& parent_lh);
	reiser4_done_lh (& left_parent_lh);

	return ret;
}

/* Performs leftward scanning starting from a formatted node */
static int slum_scan_left_formatted (slum_scan *scan, znode *node)
{
	/* Follow left pointers under tree lock as long as:
	 *
	 * - node->left is non-NULL
	 * - node->left is connected, dirty
	 * - node->left belongs to the same atom
	 * - slum has not reached maximum size
	 */
	znode *left;

	assert ("jmacd-1401", ! slum_scan_left_finished (scan));

	/*info_znode ("scan_left: ", node);*/

	do {
		/* Node should be connected, or else why is it part of the slum? */
		assert ("jmacd-1402", znode_is_connected (node));

		/* Lock the tree, check & reference left sibling. */
		spin_lock_tree (current_tree);

		/* FIXME_JMACD: It may be that a node is inserted or removed between a
		 * node and its left sibling while the tree lock is released, but that is
		 * probably okay, it just means that the slum size is not precise. */

		if ((left = node->left) != NULL) {
			zref (left);
		}

		spin_unlock_tree (current_tree);

		/* If left is NULL, need to continue using parent. */
		if (left == NULL) {
			break;
		}

		/* Check the condition for going left, break if left is not part of slum, release left reference. */
		if (! slum_scan_goleft (scan, ZJNODE (left))) {
			break;
		}

		/* Advance the slum_scan state to the left. */
		slum_scan_set_current (scan, ZJNODE (left));

	} while (! slum_scan_left_finished (scan));

	/* If left is NULL then we reached the end of a formatted region, or else the
	 * sibling is out of memory, now check for an extent to the left (as long as
	 * LEAF_LEVEL). */
	if (left == NULL && znode_get_level (node) == LEAF_LEVEL && ! slum_scan_left_finished (scan)) {
		return slum_scan_left_using_parent (scan);
	}

	scan->stop = 1;
	return 0;
}

/* Performs leftward scanning starting from an unformatted node */
static int slum_scan_left_extent (slum_scan *scan, jnode *node)
{
	jnode *left;
	unsigned long scan_index;

	assert ("jmacd-1404", ! slum_scan_left_finished (scan));
	assert ("jmacd-1405", jnode_get_level (node) == LEAF_LEVEL);

	/* Starting at the index (i.e., block offset) of the jnode in its extent... */
	scan_index = jnode_get_index (node);

	while (scan_index > 0 && ! slum_scan_left_finished (scan)) {

		/* For each loop iteration, get the previous index. */
		left = jnode_get_neighbor_in_memory (node, --scan_index);

		/* If the left neighbor is not in memory... */
		if (left == NULL) {
			scan->stop = 1;
			break;
		}

		/* Check the condition for going left. */
		if (! slum_scan_goleft (scan, left)) {
			break;
		}

		/* Advance the slum_scan state to the left. */
		slum_scan_set_current (scan, left);
	}

	/* If we made it all the way to the beginning of the extent, check for another
	 * extent or znode to the left. */
	if (scan_index == 0 && ! slum_scan_left_finished (scan)) {
		return slum_scan_left_using_parent (scan);
	}

	scan->stop = 1;
	return 0;
}

/* Performs leftward scanning starting either kind of node */
static int slum_scan_left (slum_scan *scan, jnode *node)
{
	int ret;

	/* Reference and set the initial leftmost boundary. */
	slum_scan_set_current (scan, jref (node));

	/* Continue until we've scanned far enough. */
	do {
		/* Choose the appropriate scan method and go. */
		if (jnode_is_unformatted (node)) {
			ret = slum_scan_left_extent (scan, node);
		} else {
			ret = slum_scan_left_formatted (scan, JZNODE (node));
		}

		if (ret != 0) {
			/* FIXME_JMACD: what to do about deadlock? */
			return ret;
		}

	} while (! slum_scan_left_finished (scan));

	/* At the end of a scan, get a lock (if applicable). */
	if (ret == 0 &&
	    jnode_is_formatted (scan->node) &&
	    (ret = reiser4_lock_znode (& scan->node_lock, JZNODE (scan->node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
		return ret;
	}

	return 0;
}

/********************************************************************************
 * PSEUDO CODE
 ********************************************************************************/

#if 0




			/* FIXME: Now if the parent is allocated, we have to
			 * ensure that all of its children are also allocated
			 * before it is flushed, but allocate does not
			 * necessarily imply flushing, although for efficiency
			 * we want to flush the parent at the same time as the
			 * child we are currently allocating.  Should it be
			 * done prior to flush, or should it be done after
			 * this call to slum_allocate_left_edge finishes its
			 * work at the bottom level?  If this parent is the
			 * parent of the bottom level, then the rightward
			 * phase that is about to commense will do some of the
			 * allocation that is required here.  Ask Hans for his
			 * thoughts.
			 */

			/* ANSWER: To address the allocate-during-squeeze
			 * issue, we will not squeeze in this function.
			 * During this recursive, upward pass, we find the
			 * leftmost dirty edge on each level that are
			 * connected in the tree.  Then we perform a "sweep"
			 * across all levels on which we encountered dirty
			 * nodes, processing dirty nodes in a pre-order
			 * traversal, squeezing and allocating as we go along.
			 * This algorithm has the nice property of not
			 * squeezing nodes and then inflating unallocated
			 * extents immediately afterward.  There is one
			 * problem with this algorithm, however.
			 *
			 * In the pre-order traversal through dirty nodes of
			 * the tree, we may miss the opportunity to relocate
			 * the clean parent of a dirty child, even though a
			 * later call to flush that dirty child would mark its
			 * parent dirty, thus adding it to a consecutive group
			 * of pre-ordered nodes.
			 *
			 * According to Hans: its okay to search clean nodes,
			 * if it means better packing.  Solves that issue.
			 */

/* Beginning at scan->node, lock parents until clean or allocated, and
 * allocate on the way back down. */
static int slum_allocate_ancestors (slum_scan *scan, reiser4_lock_handle *locks)
{
	int ret;
	unsigned int start_level = jnode_get_level (scan->node);
	unsigned int ancestor_level;
	tree_coord coord;
	znode *ancestor;

	/* Lock the first level above the node, and possibly also the node (if
	 * it's formatted) */
	if ((ret = jnode_lock_parent_coord (scan->node, & locks[start_level], & locks[start_level+1], & coord))) {
		return ret;
	}

	ancestor_level = start_level+1;
	ancestor       = locks[ancestor_level].node;

	/* Lock upwards until we find a either an allocated node or a clean node */
	while (znode_is_dirty (ancestor) && ! jnode_is_allocated (ZJNODE (ancestor))) {

		ancestor_level += 1;

		if ((ret = reiser4_get_parent (& locks[ancestor_level], ancestor, ZNODE_READ_LOCK, 1))) {
			return ret;
		}

		ancestor = locks[ancestor_level].node;
	}

	/* FIXME: What happens if the ancestor is dirty but allocated?  It
	 * means we have already processed this ancestor once, but what do we
	 * do about it here? */

	/* At this point, we have at least one znode locked, but it might not
	 * even be dirty (an unformatted block is modified and its parent
	 * extent is clean).  If we are going to relocate that block, then the
	 * parent will be modified, otherwise we have no znodes.  Possibly we
	 * should ask the flush plugin whether it wants to relocate this
	 * block, in which case we should mark the parent dirty and continue
	 * with our allocation.  Once the parent is marked dirty, however, we
	 * may have a dirty grandparent, which really means we should start
	 * the "lock upwards" process over again.
	 */
	if (ancestor_level == TWIG_LEVEL) {

		/* FIXME: Its a dirty unformatted block: ancestor is either
		 * allocated and dirty or clean. */
 	}

	/* The last-aquired ancestor lock is not needed, since it is either
	 * clean or allocated (unless we are going to force relocation of the
	 * ancestor for min-overwrite-set purposes). */
	reiser4_done_lh (& locks[ancestor_level--]);

	/* We've now got the left-edge of the slum locked on all levels,
	 * except for the leaf if it is unformatted (unformatted nodes don't
	 * have locks).  However, the leaf node may not be the leftmost dirty
	 * child of its parent, the slum-scan-left operation doesn't find that
	 * node. */

	/* This almost suggests (brain storming): instead of slum_scan_left as
	 * it is now, slum_scan_left should find the greatest dirty ancestor
	 * and do tree recursion from there.  Problem: it has low-priority.
	 */

	/* The last lock is not needed.  Root special case? */
	reiser4_done_lh (& locks[ancestor_level--]);

	for (; ancestor_level > start_level; ancestor_level -= 1) {
		/* allocate it, unlock it */
	}

	/* left at start_level: now scan right, allocate */

	/* what about squeeze? */

	return 0;
}
#endif

#if 0
/* What Hans wrote:
 *
 * Hans recommends: try to release parent before taking child lock, to avoid
 * contention with hipri traversals.
 *
 * allocate_tree(node,
 *               reiser4_blocknr * blocknrs_passed_by_parent, -- 0 on first entry
 *               nr_blocknrs -- 0 on first entry)
 *
 * {
 * blocks_needed = count(node) plus count(all of its children);
 * get blocknrs for them;
 * allocate_node(node, blocknrs_passed_by_parent, nr_blocknrs);
 * if (update_pointer_in_parent_to_me() == PARENT_FULL) {
 *         put_remaining_as_yet_unallocated_extent_in_right_neighbor_of_parent;
 *         return;
 * }
 * if (child is formatted node)
 *         squeeze_left (child);
 * while (child can fit into parent)
 *         allocate_tree (child, blocknrs, nr_blocknrs);
 * free_unused_blocknrs();
 * }
 *
 */

/*
current_leaf = find_leftmost_leaf_in_slum();

while (current_leaf)
{
  while (ancestors remain to be allocated by this flush)
    {
      ancestor_node = find its highest dirty parent not yet allocated by this flush();
      allocate_node(ancestor_node);
      release lock on node(ancestor_node);
    }
  current_parent = parent(current_leaf);
   do
     {
       squeeze_leaf_left(current_leaf);
       if (current_leaf ceases to exist)
         continue;
       if (parent(current_leaf) != current_parent)
         squeeze_parent_pointer_left(current_leaf);
       if (parent(current_leaf) != current_parent)
         break;
       allocate_node(current_leaf);
       if (parent(current_leaf) != current_parent)
         break;
     }
   while (current_leaf = get_right_neighbor_in_slum(current_leaf))
}
*/

/* What Josh wrote: */

/* Begin allocation at a node by finding its greatest dirty ancestor (not its
 * dirtiest ancestor).  Count the number of dirty nodes to allocate, then
 * proceed in a tree-recursive manner.
 */
int allocate_tree (jnode *node)
{
	get_node_lock (LOPRI);
	unformatted = node_is_unformatted (node);

	/* upward search, avoiding recursion */
 repeat:
	get_parent_locked (HIPRI);

	/* if the parent is dirty, or if it is an unformatted node without a dirty parent. */
	if (parent_is_dirty () || unformatted) {

		release_node_lock ();
		node = parent;
		unformatted = 0;

		/* unless the parent is not dirty, repeat upward search. */
		if (! parent_is_dirty ()) {
			goto repeat;
		}
	} else {
		/* found greatest dirty ancestor. */
		release_parent_lock ();
	}

	/* note: node is guaranteed to be formatted (i.e., its a znode) */

	blocks_needed = allocate_count (node);

	allocate_info = find (blocks_needed) blocks, prepare to allocate them;

	return allocate_tree_recursive (node, allocate_info);
}

/* Return the number of blocks to allocate that are children of this node. */
int allocate_count (jnode *node)
{
	int count = 1;

	/* recursive count */
	for (each child of node) {
		count += allocate_count (child);

                if (count > MAX_ALLOCATE_COUNT) {
			return MAX_ALLOCATE_COUNT;
                }
	}

	return count;
}

/* Perform tree-recursive allocation, squeezing formatted nodes and filling in
 * unallocated extent items. */
int allocate_tree_recursive (znode *node, allocation_info)
{
	really_allocate_this_node (node, allocation_info);

	for (each_child (node)) {
		/* stop when we've allocated the entire allocation */
		if (! more_blocks_available_in (allocation_info)) {
			return 0;
		}

		/* if formatted, squeeze and recurse */
		if (is_formatted (child)) {
			squeeze_slum (child); /* FIXME: need a way to prevent re-squeezing nodes */
			allocate_tree_recursive (child, allocation_info);
		} else {
			/* if unformatted, fill as much of the extent as possible */
			add_as_many_extent_blocks_as_will_fit_in_this_node (node, child, allocation_info);

			skip_as_many_children_of_this_node ();
		}
	}

	/* release unallocated blocks */
	free_unused_blocks (allocation_info);
	return 0;
}


	/* PLAN "SCAN SLUM"
	 *
	 * Order of steps to flush a slum (on leaf level -- most interesting case):
	 *
	 * 1. Scan left -
	 *
	 *   a. If formatted node, follow left pointers until NULL or not-dirty/not-in-atom
	 *   b. If unformatted node, search page cache for dirty pages from high indexes (right) to
	 *      low indexes (left) until ZERO or not-dirty/not-in-atom
	 *
	 * 2. Lock leftmost node -
	 *
	 * 3. Scan right - create an "iterator" - object for scanning right one node at a time.
	 *
	 * 4. Call allocate plugin, which iterates right using the iterator
	 *
	 *   a. If the iterator is positioned at an unformatted node, and to
	 *      the right is a formatted node, squeeze formatted slum to-the-right before
	 *      returning to the iterator.
	 *   b. Allocate nodes while iterating to the right, squeezing each time you begin
	 *      a new formatted slum.
	 *
	 *
	 */

#endif


/********************************************************************************
 * OLDER PSEUDO CODE
 ********************************************************************************/

#if YOU_CAN_COMPILE_PSEUDO_CODE

/* This function accepts a znode argument for now, since we don't really have hooks to the
 * real VM yet.
 */
int reiser4_flush_node (znode *node)
{
	int ret;
	int do_flush = 0;

	spin_lock_znode (node);

	if (atomic_read (& node->d_count) > 0 || znode_is_any_locked (node)) {

		/* Page is currently in use.  What to do? */

	} else if (! znode_is_dirty (node)) {

		/* Node is clean, either captured by an atom (meaning either it was
		 * read-captured but not modified or it was modified and subsequently
		 * early-flushed) or not. */

	} else {

		/* Node is part of a slum, perform pre-flushing ritual after releasing
		 * znode lock. */
		do_flush = 1;

		/* If meta-data journaling, the atom may be NULL, otherwise has_slum
		 * implies has_atom. */
	}

	spin_unlock_znode (node);

	if (do_flush) {
		/* Flushing is called under the tree lock, tree lock is released. */
		spin_lock_tree (current_tree);

		if ((ret = flush_znode_slum (node)) != 0) {
			return ret;
		}

		assert ("jmacd-1086", spin_tree_is_not_locked (current_tree));

		/* Now balanced, slum can be freed: the nodes can be flushed, setting
		 * ZNODE_WRITEOUT, eventually unsetting ZNODE_DIRTY bits.  At when d_count
		 * reaches zero, nodes in slum can be unloaded, removed from slum.  The
		 * slum is deallocated.
		 *
		 * But since we released the znode spinlock, the node could be referenced
		 * again, etc...
		 */
	}

	return 0;
}



/* this version probably won't get implemented, see the one below */
reiser4_flush_page(page page)
{
	static int pressures;		/* number of times reiserfs has been
					   pressured by VM to free dirty
					   pages */

	reshuffle_page(page);		/*  mark page as recently used, I forget the correct function name for this */

	if (page is member of transaction) {
		pressure_transaction();
		/* note incorrect assumption of there being only one
		   transaction, fix me after some thought */
		if (pressures > number of pages in transaction) {
			flush_transaction();
			return;
		}
	}

	if (has_children_in_memory(page)) {
		/* we get here for internal nodes with children in memory */
		reiserfs_toss_a_page(page);
	} else if (page is dirty) {
		write page to disk;
	}
}

reiser4_toss_a_page(page page)
{
	in v4.1 make internal nodes more resistant to ejection than other pages and benchmark;
}

/* needs to be updated to use the  */
reiser4_flush_page(page page)
{
	slum = gather_slum(page page);
	flush_slum(slum);
	if (page is pinned member of transaction) {
		flush_transaction(page);
	}
}


flush_transaction()
{
	/* see txn_atom_try_to_commit, txn_force_commit */
	flush_all_slums_in_transaction();
}

flush_slum()
{
	encrypt_all_crypto_objects_with_items_in_slum();
	allocate_blocks_in_slum(i);
	squeeze_slum();
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
