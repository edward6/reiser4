
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

/* Perform encryption, allocate-on-flush, and squeezing-left of slums. */
int flush_znode_slum (znode *node UNUSED_ARG)
{
	/* Somewhere in here, ZNODE_RELOC and ZNODE_WANDERED are set. */

	/* Lots to do in here.  The txnmgr expects this to clean the node. */
	znode_set_clean (node);

	/* Squeeze the slum. */

	return 0;
}

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


#if YOU_CAN_COMPILE_PSEUDO_CODE
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
