
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
/* Some old dead/pseudo-code removed Thu Apr 25 17:11:30 MSD 2002 */

#include "reiser4.h"

static void          slum_scan_init               (slum_scan *scan);
static void          slum_scan_cleanup            (slum_scan *scan);
static int           slum_scan_left_finished      (slum_scan *scan);
static int           slum_scan_left_extent        (slum_scan *scan, jnode *node);
static int           slum_scan_left_formatted     (slum_scan *scan, znode *node);
static void          slum_scan_set_current        (slum_scan *scan, jnode *node);
static int           slum_scan_left               (slum_scan *scan, jnode *node);

static int           flush_preceder_hint          (jnode *gda, znode *parent_node, reiser4_blocknr_hint *preceder);
static int           flush_should_relocate        (jnode *node, znode *parent);

static int           slum_lock_greatest_dirty_ancestor      (jnode                *start,
							     reiser4_lock_handle  *start_lock,
							     jnode               **gda,
							     reiser4_lock_handle  *gda_lock,
							     reiser4_blocknr_hint *preceder);

static int           squalloc_parent_first                  (jnode *gda, reiser4_blocknr_hint *preceder);

static int           jnode_lock_parent_coord      (jnode *node, reiser4_lock_handle *node_lh, reiser4_lock_handle *parent_lh,
						   tree_coord *coord, znode_lock_mode mode);
static int           jnode_is_allocated           (jnode *node);
static jnode*        jnode_get_neighbor_in_memory (jnode *node, unsigned long node_index);
static unsigned long jnode_get_index              (jnode *node);
static int           jnode_allocate               (jnode *node);

/* Abbreviation: "squeeze and allocate" == "squalloc" */

#define FLUSH_WORKS 0

/* Perform encryption, allocate-on-flush, and squeezing-left of slums. */
int flush_jnode_slum (jnode *node)
{
	int ret;
	jnode *gda = NULL; /* Greatest dirty ancestor */
	reiser4_lock_handle gda_lock;
	reiser4_blocknr_hint preceder;

	reiser4_init_lh (& gda_lock);

	/* First, locate the greatest dirty ancestor of the node to flush,
	 * which found by recursing upward as long as the parent is dirty and
	 * leftward along each level as long as the left neighbor is dirty.
	 * Also initializes preceder hint.
	 */
	if (FLUSH_WORKS && (ret = slum_lock_greatest_dirty_ancestor (node, NULL, & gda, & gda_lock, & preceder))) {
		goto failed;
	}

	/* Second, the parent-first squeeze and allocate traversal. */
	if (FLUSH_WORKS && (ret = squalloc_parent_first (gda, & preceder))) {
		goto failed;
	}

	/* FIXME: Is that it? */
	   
	if (! FLUSH_WORKS) {
		/* FIXME: Old behavior: The txnmgr expects this to clean the
		 * node.  (i.e., move it to the clean list).  That's all this
		 * does for now. */
		jnode_set_clean (node);
		ret = 0;
	}

   failed:

	if (gda != NULL) {
		jput (gda);
	}
	reiser4_done_lh (& gda_lock);
	return ret;
}

/********************************************************************************
 * SLUM_LOCK_GREATEST_DIRTY_ANCESTOR
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
static int slum_lock_greatest_dirty_ancestor (jnode *start_node,
					      reiser4_lock_handle *start_lock,
					      jnode **gda,
					      reiser4_lock_handle *gda_lock,
					      reiser4_blocknr_hint *preceder)
{
	int ret;
	jnode *end_node;
	znode *parent_node;
	reiser4_lock_handle end_lock;
	reiser4_lock_handle parent_lock;
	slum_scan           level_scan;

	slum_scan_init  (& level_scan);
	reiser4_init_lh (& parent_lock);
	reiser4_init_lh (& end_lock);

	/* Scan parent level for the leftmost dirty */
	if ((ret = slum_scan_left (& level_scan, start_node))) {
		goto failure;
	}

	end_node = level_scan.node;

	/* If end_node is not the same as start node, we can release
	 * start_lock and try to get end_lock.  Need to lock the child before
	 * we can lock the parent.  (FIXME:) This is seen as
	 * not-quite-necessary, but we do it anyway since there are assertions
	 * to that effect in reiser4_get_parent. */
	if (end_node != start_node) {

		reiser4_done_lh (start_lock);
		
		if (jnode_is_formatted (end_node) &&
		    (ret = longterm_lock_znode (& end_lock, JZNODE (end_node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
			goto failure;
		}
	}

	/* We're at the end of a level slum, read lock the parent.  Don't need
	 * a write-lock yet because we do not start squeezing until the
	 * greatest dirty ancestor is found.
	 *
	 * FIXME: if node is root, reiser4_get_parent() returns error.  Use
	 * znode_is_root()?  Use znode_is_true_root()?  Don't you need to lock
	 * the above_root node to determine this?  Confused.
	 *
	 * FIXME: This is wrong!!!!! get_parent doesn't work on unformatted nodes.
	 */
	if ((ret = reiser4_get_parent (& parent_lock, JZNODE (end_node), ZNODE_READ_LOCK, 1))) {
		goto failure;
	}

	parent_node = parent_lock.node;

	/* Some or all of this should be a plugin.  Since I don't know exactly
	 * what yet, none of it is a plugin.  I am partly concerned that
	 * breaking these things into plugins will result in duplicated
	 * effort.  Returns 0 for no-relocate, 1 for relocate, < 0 for
	 * error. */
	if ((ret = flush_should_relocate (end_node, parent_node)) < 0) {
		goto failure;
	}

	/* If relocating the child, artificially dirty the parent right now. */
	if (ret == 1) {
		znode_set_dirty (parent_node);
	}

	/* If the parent is dirty, it needs to be squeezed also, recurse upwards */
	if (znode_is_dirty (parent_node)) {

		/* Release lock at this level before going upward. Only one of
		 * these two is actually locked, since if the two nodes are
		 * different start_lock was released before end_lock was
		 * aquired. */
		reiser4_done_lh (start_lock);
		reiser4_done_lh (& end_lock);
 
		/* Recurse upwards. */
		if ((ret = slum_lock_greatest_dirty_ancestor (ZJNODE (parent_node), & parent_lock, gda, gda_lock, preceder))) {
			goto failure;
		}

	} else {
		/* End the recursion, get a write lock at the highest level. */

		/* But as long as we have the parent locked, might as well
		 * initialize the preceder hint now.  It could release the
		 * lock for us after determining we don't need to look at the
		 * parent to initialize the hint. */
		if ((ret = flush_preceder_hint (end_node, parent_node, preceder))) {
			goto failure;
		}
		
		(*gda) = jref (end_node);

		/* Release any locks we might hold first, they are all read
		 * locks on this level, and parent lock is not needed.  */
		reiser4_done_lh (& parent_lock);
		reiser4_done_lh (start_lock);
		reiser4_done_lh (& end_lock);

		if (jnode_is_formatted (end_node) &&
		    (ret = longterm_lock_znode (gda_lock, JZNODE (end_node), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
			jput (end_node);
			goto failure;
		}
	}

	assert ("jmacd-2030", ret == 0);
 failure:
	slum_scan_cleanup (& level_scan);
	reiser4_done_lh   (& parent_lock);
	reiser4_done_lh   (& end_lock);

	return ret;
}

/********************************************************************************
 * SLUM ALLOCATE AND SQUEEZE
 ********************************************************************************/

/* This relocation policy is:
 *
 *   (leftmost_of_parent && (is_leaf || leftmost_child_is_relocated))
 */
static int flush_should_relocate (jnode *node, znode *parent)
{
	int ret;

	if (jnode_is_formatted (node)) {

		int is_leftmost;
		tree_coord coord;
		item_plugin *iplug;
		jnode *left_child;

		if ((ret = find_child_ptr (JZNODE (node), parent, & coord))) {
			assert ("jmacd-2050", ret < 0);
			return ret;
		}

		if (! (is_leftmost = coord_prev_item (& coord))) {
			/* Not leftmost of parent, don't relocate */
			return 0;
		}

		if (jnode_get_level (node) == LEAF_LEVEL) {
			/* Leftmost leaf, relocate */
			return 1;
		}

		coord_first_unit (& coord, JZNODE (node));

		iplug = item_plugin_by_coord (& coord);

		if ((ret = iplug->utmost_child (& coord, LEFT_SIDE, 0, & left_child, NULL))) {
			assert ("jmacd-2051", ret < 0);
			return ret;
		}

		if (left_child == NULL) {
			/* Leftmost of parent, left child not dirty, don't relocate. */
			return 0;
		}

		if (jnode_is_dirty (left_child)) {
			/* Leftmost of parent, leftmost child dirty, relocate. */
			return 1;
		}

		/* Leftmost of parent, leftmost child clean, don't relocate. */
		return 0;
	} else {
		/* FIXME: how to get parent of unforamtted? */
	}
}

/* Called while the parent is still locked, since we may need it.
 */
static int flush_preceder_hint (jnode *gda,
				znode *parent_node,
				reiser4_blocknr_hint *preceder)
{
	

	return 0;
}

/* Called on a non-leaf-level znode to process its children in the
 * squalloc traversal.  For each extent or internal item in this
 * node, either allocate the extent or make a squalloc_parent_first
 * call on the child.
 */
static int squalloc_children (znode *node, reiser4_blocknr_hint *preceder)
{
	int ret;
	tree_coord crd;

	coord_first_unit (& crd, node);

	/* Assert the pre-condition for the following do-loop, essentially
	 * stating that the node is not empty. */
	assert ("jmacd-2000", ! coord_after_last (& crd));

	do {
		item_plugin *item = item_plugin_by_coord (& crd);

		switch (item->item_type) {
		case EXTENT_ITEM_TYPE:

			if ((ret = allocate_extent_item_in_place (&crd, preceder))) {
				return ret;
			}
			break;

		case INTERNAL_ITEM_TYPE: {

			znode *child = child_znode (& crd, 1);

			if (IS_ERR (child)) { return PTR_ERR (child); }

			if (! znode_is_dirty (child)) { continue; }

			if ((ret = squalloc_parent_first (ZJNODE (child), preceder))) {
				return ret;
			}
			break;
		}
		default:
			warning ("jmacd-2001", "Unexpected item type above leaf level");
			print_znode ("node", node);
			return -EIO;
		}

	} while (! coord_next_unit (& crd));

	return 0;
}

/*
 * @left and @right are formatted neighboring nodes on leaf level. Shift as
 * much as possible from @right to @left using the memcpy-optimized
 * shift_everything_left.
 */
static int squeeze_leaves (znode * right, znode * left)
{
	int ret;
	carry_pool pool;
	carry_level todo;

	init_carry_pool (& pool);
	init_carry_level (& todo, & pool);
	
	ret = shift_everything_left (right, left, & todo);

	if (ret >= 0) {
		/*
		 * carry is called to update delimiting key or to remove empty node
		 */
		ret = carry (& todo, NULL /* previous level */);
	}
	
	done_carry_pool (& pool);

	if (ret < 0) {
		return ret;
	}

	return node_is_empty (right) ? SQUEEZE_SOURCE_EMPTY : SQUEEZE_TARGET_FULL;
}

/*
 * Shift first unit of first item if it is an internal one.  Return
 * SQUEEZE_DONE if it fails to shift an item, otherwise return SUBTREE_MOVED.
 */
static int shift_one_internal_unit (znode * left, znode * right)
{
	int ret;
	carry_pool pool;
	carry_level todo;
	tree_coord coord;
	int size, moved;


	coord_first_unit (&coord, right);

	assert ("jmacd-2007", item_is_internal (& coord));

	init_carry_pool (&pool);
	init_carry_level (&todo, &pool);

	size = item_length_by_coord (&coord);
	ret  = node_plugin_by_node (left)->shift (&coord, left, SHIFT_LEFT,
						  1/* delete @right if it becomes empty*/,
						  0/* move coord */,
						  &todo);

	/*
	 * If shift returns positive, then we shifted the item.
	 */
	assert ("vs-423", ret <= 0 || size == ret);
	moved = (ret > 0);

	if (ret > 0) {
		/*
		 * carry is called to update delimiting key or to remove empty node,
		 * unless shift failed.
		 */
		ret = carry (&todo, NULL /* previous level */);
	}

	done_carry_pool (&pool);

	if (ret != 0) {
		return ret;
	}
	
	return moved ? SUBTREE_MOVED : SQUEEZE_TARGET_FULL;
}

/*
 * cut node to->node from the beginning up to coord @to
 */
static int cut_copied (tree_coord * to, reiser4_key * to_key)
{
	tree_coord from;
	reiser4_key from_key;

	coord_first_unit (&from, to->node);
	item_key_by_coord (&from, &from_key);

	/*
	 * FIXME-VS: not sure yet what to do here [if cut_node fails]
	 * impossible ("vs-434", "carry should have done its job");
	 *
	 * FIXME_JMACD: What's the problem?  (Same with shift_one_internal_unit.)
	 */
	return cut_node (&from, to, &from_key, to_key, NULL /* smallest_removed */, DELETE_DONT_COMPACT);
}

/*
 * copy as much of the leading extents from the right to left, returning
 * SQUEEZE_DONE when no more can be shifted.  If the next item is an internal
 * item it calls shift_one_internal_unit and may then return SUBTREE_MOVED.
 */
static int squalloc_twig (znode    *left,
			  znode    *right,
			  reiser4_blocknr_hint *preceder)
{
	int ret;
	tree_coord coord;
	reiser4_key stop_key;


	assert ("jmacd-2008", ! node_is_empty (right));

	coord_first_unit (&coord, right);

	/*
	 * if after while loop stop_key is still equal to *min_key - nothing
	 * were copied, therefore there will be nothing to cut
	 */
	stop_key = *min_key ();
	while (item_is_extent (&coord)) {

		if ((ret = allocate_and_copy_extent (left, &coord, preceder, &stop_key)) < 0) {
			return ret;
		}

		if (ret == SQUEEZE_TARGET_FULL) {
			/*
			 * could not complete with current extent item
			 */
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
		/*
		 * something was copied
		 */
		/*
		 * @coord is set to first unit which does not have to be cut or
		 * after last item in the node.  If we are positioned at the
		 * coord of a * unit, it means the allocate_and_copy_extent
		 * processing stops in the * middle of an extent item, the last
		 * unit of which was not copied.  * Cut everything before that
		 * point.
		 */
		if (coord_of_unit (& coord)) {
			coord_prev_unit (& coord);
		}
		
		if ((cut_ret = cut_copied (&coord, &stop_key))) {
			return cut_ret;
		}
	}

	if (node_is_empty (right)) {
		/*
		 * whole right fitted into left
		 */
		assert ("vs-464", ret == SQUEEZE_SOURCE_EMPTY);
		return ret;
	}

	coord_first_unit (&coord, right);

	if (! item_is_internal (&coord)) {
		/*
		 * there is no space in left anymore
		 */
		assert ("vs-433", item_is_extent (&coord));
		assert ("vs-465", ret == SQUEEZE_TARGET_FULL);
		return ret;
	}

	return shift_one_internal_unit (left, right);
}


/*
 * shift items from @right to @left. Unallocated extents of extent items are
 * allocated first and then moved. When unit of internal item is moved -
 * squeezing stops and SUBTREE_MOVED is returned. When all content of @rigth is
 * squeezed - SQUEEZE_SOURCE_EMPTY is returned. If nothing can be moved into
 * @left anymore - SQUEEZE_TARGET_FULL is returned
 */
/*static*/ int squalloc_right_neighbor (znode    *left,
					znode    *right,
					reiser4_blocknr_hint *preceder)
{
	int ret;

	
	assert ("vs-425", !node_is_empty (left));

	if (node_is_empty (right))
		/*
		 * this is possible
		 */
		return SQUEEZE_SOURCE_EMPTY;

	switch (znode_get_level (left)) {
	case LEAF_LEVEL:
		/*
		 * shift as much as possible
		 */
		ret = squeeze_leaves (left, right);
		assert ("jmacd-2010", ret < 0 || ret == SQUEEZE_SOURCE_EMPTY || ret == SQUEEZE_TARGET_FULL);
		break;

	case TWIG_LEVEL:
		/*
		 * shift with extent allocating until either internal item
		 * encountered or everything is shifted or no free space left
		 * in @left
		 */
		ret = squalloc_twig (left, right, preceder);
		assert ("jmacd-2011", ret < 0 || ret == SQUEEZE_SOURCE_EMPTY || ret == SQUEEZE_TARGET_FULL);
		break;

	default:
		/*
		 * all other levels contain items of internal type only
		 */
		ret = shift_one_internal_unit (left, right);
		assert ("jmacd-2012", ret < 0 || ret == SQUEEZE_SOURCE_EMPTY || ret == SQUEEZE_TARGET_FULL);
		break;

	}

	if (ret == SQUEEZE_SOURCE_EMPTY)
		reiser4_stat_slum_add (squeezed_completely);

	return ret;
}

/* Starting from the greatest dirty ancestor of a subtree to flush, allocate
 * self then recursively squeeze and allocate the children. */
static int squalloc_parent_first (jnode *node, reiser4_blocknr_hint *preceder)
{
	int ret, goright;
	znode *right;
	reiser4_lock_handle right_lock;
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
	
	/* Allocate (parent) first. It might be allocated already. */
        if (! jnode_is_allocated (node)) {

		/* Allocate it. */
                jnode_allocate (node);

                /* Recursive case: */
                if ((jnode_get_level (node) > LEAF_LEVEL) &&
		    (ret = squalloc_children (JZNODE (node), preceder))) {
			return ret;
                }
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
	reiser4_init_lh (& right_lock);

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

	/* FIXME: Check ZNODE_HEARD_BANSHEE? */

	while ((squeeze = squalloc_right_neighbor (JZNODE (node), right_lock.node, preceder)) == SUBTREE_MOVED) {
		/*
		 * unit of internal item is shifted - allocated and squeeze that
		 * subtree which roots from last unit in node
		 */
		tree_coord crd;
		znode *child;
		
		reiser4_init_coord (& crd);
		coord_last_unit (& crd, JZNODE (node));

		assert ("vs-442", item_is_internal (& crd));

		child = child_znode (& crd, 1/*set delim key*/);
		if (IS_ERR (child)) { return PTR_ERR (child); }

		if (! znode_is_dirty (child)) { continue; }

		if ((ret = squalloc_parent_first (ZJNODE (child), preceder))) {
			return ret;
		}
	}

	if (squeeze == SQUEEZE_SOURCE_EMPTY) {
		/*
		 * right neighbor was squeezes completely into @node, try to
		 * squeeze with new right neighbor
		 */
		goto again;
	}
	/*
	 * error or nothing else of right_lock.node can be shifted to @node
	 */
	assert ("vs-444", node_is_empty (right_lock.node));
	assert ("vs-443", squeeze == SQUEEZE_TARGET_FULL || squeeze < 0);
	ret = ((squeeze < 0) ? squeeze : 0);

 cleanup:
	reiser4_done_lh (& right_lock);
	return ret;
}

/********************************************************************************
 * SLUM JNODE INTERFACE
 ********************************************************************************/

/* Gets the sibling of an unformatted jnode using its index, only if it is in memory, and
 * reference it. */
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

/* Get the index of a block. */
static unsigned long jnode_get_index (jnode *node)
{
	return node->pg->index;
}

/* return true if "node" is allocated */
static int jnode_is_allocated (jnode *node UNUSED_ARG)
{
	/* FIXME_JMACD: */
	return 0;
}

static int jnode_allocate (jnode *node UNUSED_ARG)
{
	/* FIXME_JMACD: */
	return -EINVAL;
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

		if ((ret = longterm_lock_znode (node_lh, JZNODE (node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
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

	goleft = txn_same_atom_dirty (scan->node, left);

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
	if ((ret = coord_prev_unit (& coord)) != 0) {
		/* If coord_prev returns 1, coord is already leftmost of its node. */

		/* Lock the left-of-parent node, but don't read into memory.  ENOENT means not-in-memory. */
		if ((ret = reiser4_get_left_neighbor (& left_parent_lh, parent_lh.node, ZNODE_READ_LOCK, 0)) && (ret != -ENOENT)) {
			goto done;
		}

		if (ret == 0) {
			/* Release parent lock -- don't need it any more. */
			reiser4_done_lh (& parent_lh);

			/* Set coord to the rightmost position of the left-of-parent node. */
			coord_last_unit (& coord, left_parent_lh.node);

			ret = 0;
		}
	}

	/* Get the item plugin. */
	iplug = item_plugin_by_coord (& coord);

	assert ("jmacd-2040", iplug->utmost_child != NULL);

	/* Get the rightmost child of this coord, which is the child to the left of the scan position. */
	if ((ret = iplug->utmost_child (& coord, RIGHT_SIDE, UTMOST_GET_CHILD, & child_left, NULL))) {
		goto done;
	}

	/* If the child is not in memory, stop. */
	if (child_left == NULL) {
		scan->stop = 1;
		ret = 0;
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
	    (ret = longterm_lock_znode (& scan->node_lock, JZNODE (scan->node), ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
		return ret;
	}

	return 0;
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
