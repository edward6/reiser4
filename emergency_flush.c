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
 *
 *
 */

#include "forward.h"
#include "debug.h"


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

