
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

static void slum_scan_init             (slum_scan *scan);
static void slum_scan_cleanup          (slum_scan *scan);
static int  slum_scan_left_finished    (slum_scan *scan);
static int  slum_scan_left_extent      (slum_scan *scan, jnode *node);
static int  slum_scan_left_formatted   (slum_scan *scan, znode *node);
static void slum_scan_set_current      (slum_scan *scan, jnode *node);
static int  slum_scan_left             (slum_scan *scan, jnode *node);

/* Perform encryption, allocate-on-flush, and squeezing-left of slums. */
int flush_jnode_slum (jnode *node)
{
	int ret;
	slum_scan scan;

	/* Somewhere in here, ZNODE_RELOC and ZNODE_WANDERED are set. */
	/* Lots to do in here. */

	slum_scan_init (& scan);

	/* Scan the slum.  FIXME_JMACD: TEMPORARILY DISABLED TO KEEP CODE FUNCTIONING. */
	if (0 && (ret = slum_scan_left (& scan, node))) {
		slum_scan_cleanup (& scan);
		return ret;
	}

	/* Squeeze, allocate, encrypt, and flush the slum. */
	     
	/* The txnmgr expects this to clean the node.  (i.e., move it to the
	 * clean list).  That's all this does for now. */
	jnode_set_clean (node);

	return 0;
}


/* Initialize the slum_scan data structure. */
static void slum_scan_init (slum_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
}

/* Release any resources held by the slum scan, e.g., release locks, free memory, etc. */
static void slum_scan_cleanup (slum_scan *scan)
{
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

	if (jnode_is_unformatted (scan->node)) {

		/* FIXME_JMACD: Don't know how to do this. */

		ret = 0;

	} else {
		/* Formatted node case: */
		znode *node = JZNODE (scan->node);

		/* Lock the node itself, which is necessary for getting its parent (FIXME_JMACD: not sure why). */
		if ((ret = reiser4_lock_znode (& node_lh, node, ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI))) {
			goto done;
		}

		/* Get the parent read locked. */
		if ((ret = reiser4_get_parent (& parent_lh, node, ZNODE_READ_LOCK, 1))) {
			goto done;
		}

		/* FIXME_JMACD: Do we still need the node lock? */

		/* Make the child's position "hint" up-to-date. */
		if ((ret = find_child_ptr (parent_lh.node, node, & coord))) {
			goto done;
		}

		/* Shift the coord to the left. */
		if ((ret = coord_prev (& coord)) != 0) {
			/* If coord_prev returns 1, coord is already leftmost of its node. */

			/* Lock the left-of-parent node, but don't read into memory. */
			if ((ret = reiser4_get_left_neighbor (& left_parent_lh, parent_lh.node, ZNODE_READ_LOCK, 0))) {
				goto done;
			}

			/* Release parent lock -- don't need it any more. */
			reiser4_done_lh (& parent_lh);

			/* Set coord to the rightmost position of the left-of-parent node. */
			coord.node = left_parent_lh.node;
			coord_last_unit (& coord);
		}

		iplug = item_plugin_by_coord (& coord);
		child_left = NULL;

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

/* Gets the sibling of an unformatted jnode using its index, only if it is in memory, and
 * reference it. */
static jnode*
jnode_get_neighbor_in_memory (jnode *node, unsigned long node_index)
{
	/* FIXME_JMACD: jref () */
	return NULL;
}

static unsigned long
jnode_get_index (jnode *node)
{
	/* FIXME_JMACD: */
	return 0;
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

	assert ("jmacd-1401", slum_scan_left_finished (scan));
	
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
	if (left == NULL && znode_get_level (left) == LEAF_LEVEL && ! slum_scan_left_finished (scan)) {
		return slum_scan_left_using_parent (scan);
	}

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
			/* FIXME_JMACD: do something with deadlock here? */
			return ret;
		}

	} while (! slum_scan_left_finished (scan));

	return 0;
}

/********************************************************************************/
/* OLDER PSEUDO CODE
/********************************************************************************/

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
