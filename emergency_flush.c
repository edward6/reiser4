/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Implementation of emergency flush.
 */

/*
 * OVERVIEW:
 *
 *   Reiser4 maintains all meta data in the single balanced tree. This tree is
 *   maintained in the memory in the form different from what will be
 *   ultimately written to the disk. Roughly speaking, before writing tree
 *   node to the disk, some complex process (flush.[ch]) is to be
 *   performed. Flush is main necessary preliminary step before writing pages
 *   back to the disk, but it has some characteristics that make it completely
 *   different from traditional ->writepage():
 *   
 *      1 it is not local, that is it operates on a big number of nodes,
 *      possibly far away from the starting node, both in tree and disk order.
 *
 *      2 it can involve reading of nodes from the disk
 *      (for example, bitmap nodes are read during extent allocation that is
 *      deferred until flush).
 *
 *      3 it can allocate unbounded amount of memory (during insertion of
 *      allocated extents).
 *
 *      4 it participates in the locking protocol which reiser4 uses to
 *      implement concurrent tree modifications.
 *
 *      5 it is CPU consuming and long
 *
 *   As a result, flush reorganizes some part of reiser4 tree and produces
 *   large queue of nodes ready to be submitted for io (as a matter of fact,
 *   flush write clustering is so good that it used to hit BIO_MAX_PAGES all
 *   the time, until checks were added for this).
 *
 *   Items (3) and (4) alone make flush unsuitable for being called directly
 *   from reiser4 ->vm_writeback() callback, because of OOM and deadlocks
 *   against threads waiting for memory.
 *
 *   So, flush is performed from within balance_dirty_page() path when dirty
 *   pages are generated. If balance_dirty_page() fails to throttle writers
 *   and page replacement finds dirty page on the inactive list, we resort to
 *   "emergency flush" in our ->vm_writeback(). Emergency flush is relatively
 *   dumb algorithm, implemented in this file, that tries to write tree nodes
 *   to the disk without taking locks and without thoroughly optimizing tree
 *   layout. We only want to call emergency flush in desperate situations,
 *   because it is going to produce sub-optimal disk layouts.
 *
 * DELAYED PARENT UPDATE
 *
 *   Important point of emergency flush is that update of parent is sometimes
 *   delayed: we don't update parent immediately if:
 *
 *    1 Child was just allocated, but parent is locked. Waiting for parent
 *    lock in emergency flush is impossible (deadlockable).
 *
 *    2 Part of extent was allocated, but parent has not enough space to
 *    insert allocated extent unit. Balancing in emergency flush is
 *    impossible, because it will possibly wait on locks.
 *
 *   When we delay update of parent node, we mark it as such (and possibly
 *   also mark children to simplify delayed update later). Question: when
 *   parent should be really updated?
 *
 * WHERE TO WRITE PAGE INTO?
 *
 *  
 *******HISTORICAL SECTION****************************************************
 *
 *   So, it was decided that flush has to be performed from a separate
 *   thread. Reiser4 has a thread used to periodically commit old transactions,
 *   and this thread can be used for the flushing. That is, flushing thread
 *   does flush and accumulates nodes prepared for the IO on the special
 *   queue. reiser4_vm_writeback() submits nodes from this queue, if queue is
 *   empty, it only wakes up flushing thread and immediately returns.
 *
 *   Still there are some problems with integrating this stuff into VM
 *   scanning:
 *
 *      1 As ->vm_writeback() returns immediately without actually submitting
 *      pages for IO, throttling on PG_writeback in shrink_list() will not
 *      work. This opens a possibility (on a fast CPU), of try_to_free_pages()
 *      completing scanning and calling out_of_memory() before flushing thread
 *      managed to add anything to the queue.
 *
 *      2 It is possible, however unlikely, that flushing thread will be
 *      unable to flush anything, because there is not enough memory. In this
 *      case reiser4 resorts to the "emergency flush": some dumb algorithm,
 *      implemented in this file, that tries to write tree nodes to the disk
 *      without taking locks and without thoroughly optimizing tree layout. We
 *      only want to call emergency flush in desperate situations, because it
 *      is going to produce sub-optimal disk layouts.
 *
 *      3 Nodes prepared for IO can be from the active list, this means that
 *      they will not be met/freed by shrink_list() after IO completion. New
 *      blk_congestion_wait() should help with throttling but not
 *      freeing. This is not fatal though, because inactive list refilling
 *      will ultimately get to these pages and reclaim them.
 *
 * REQUIREMENTS
 *
 *   To make this work we need at least some hook inside VM scanning which
 *   gets triggered after scanning (or scanning with particular priority)
 *   failed to free pages. This is already present in the
 *   mm/vmscan.c:set_shrinker() interface.
 *
 *   Another useful thing that we would like to have is passing scanning
 *   priority down to the ->vm_writeback() that will allow file system to
 *   switch to the emergency flush more gracefully.
 *
 * POSSIBLE ALGORITHMS
 *
 *   1 Start emergency flush from ->vm_writeback after reaching some priority.
 *   This allows to implement simple page based algorithm: look at the page VM
 *   supplied us with and decide what to do.
 *
 *   2 Start emergency flush from shrinker after reaching some priority.
 *   This delays emergency flush as far as possible.
 *
 *******END OF HISTORICAL SECTION**********************************************
 *
 */

#include "forward.h"
#include "debug.h"

static int flushable(const jnode * node);

/**
 * try to flush @page to the disk
 */
int
emergency_flush(struct page *page, struct writeback_control *wbc)
{
	struct super_block *sb;
	jnode *node;
	int result;

	assert("nikita-2721", page != NULL);
	assert("nikita-2722", wbc != NULL);
	assert("nikita-2723", PageDirty(page));
	assert("nikita-2724", PageLocked(page));

	sb = page->mapping->host->i_sb;
	node = jprivate(pg);

	if (node == NULL)
		return 0;

	jref(node);
	reiser4_stat_add_at_level(jnode_get_level(node), emergency_flush);

	result = 0;
	spin_lock_jnode(node);
	if (flushable(node)) {
		int sendit;

		sendit = 0;
		if (JF_ISSET(node, JNODE_RELOC)) {
			if (!blocknr_is_fake(jnode_get_block(node))) {
				/*
				 * not very likely case: @node is in relocate
				 * set, block number is already assigned, but
				 * @node wasn't yet submitted for io.
				 */
				sendit = 1;
			} else {
			}
		} else if (JF_ISSET(node, JNODE_RELOC)) {
		} else {
		}
		if (sendit) {
			result = page_io(page, node, WRITE, GFP_NOFS | __GFP_HIGH);
			if (result == 0)
				--wbc->nr_to_write;
		}
	}
	spin_unlock_jnode(node);
	jput(node);
	return result;
}

static int
flushable(const jnode * node)
{
	assert("nikita-2725", node != NULL);
	assert("nikita-2726", spin_jnode_is_locked(node));

	if (atomic_read(&node->d_count) != 0)
		return 0;
	if (jnode_is_loaded(node))
		return 0;
	if (JF_ISSET(node, JNODE_FLUSH_QUEUED))
		return 0;
	if (jnode_is_znode(node) && znode_is_locked(JZNODE(node)))
		return 0;
	return 1;
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
