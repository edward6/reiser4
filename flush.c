/* Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* The design document for this file is at www.namesys.com/flush-alg.html. */

#include "reiser4.h"

/********************************************************************************
 * IMPLEMENTATION NOTES
 ********************************************************************************/

/* PARENT-FIRST: Some terminology: A parent-first traversal is a way of assigning a total
 * order to the nodes of the tree in which the parent is placed before its children, which
 * are ordered (recursively) in left-to-right order.  We have the notion of "forward"
 * parent-first order (the ordinary case, just described) and "reverse" parent-first
 * order, which inverts the relationship.  When we speak of a "parent-first preceder", it
 * describes the node that "came before in forward parent-first order" (alternatively the
 * node that "comes next in reverse parent-first order").  When we speak of a
 * "parent-first follower", it describes the node that "comes next in forward parent-first
 * order" (alternatively the node that "came before in reverse parent-first order").
 *
 * The following pseudo-code prints the nodes of a tree in forward parent-first order:
 *
 * void parent_first (node)
 * {
 *   print_node (node);
 *   if (node->level > leaf) {
 *     for (i = 0; i < num_children; i += 1) {
 *       parent_first (node->child[i]);
 *     }
 *   }
 * }
 */

/* JUST WHAT ARE WE TRYING TO OPTIMIZE, HERE?  The idea is to optimize block allocation so
 * that a left-to-right scan of the tree's data (i.e., the leaves in left-to-right order)
 * can be accomplished with sequential reads, which results in reading nodes in their
 * parent-first order.  This is a read-optimization aspect of the flush algorithm, and
 * there is also a write-optimization aspect, which is that we wish to make large
 * sequential writes to the disk by allocating or reallocating blocks so that they can be
 * written in sequence.  Sometimes the read-optimization and write-optimization goals
 * conflict with each other, as we discuss in more detail below.
 */

/* STATE BITS: The flush code revolves around the state of the jnodes it covers.  Here are
 * the relevant jnode->state bits and their relevence to flush:
 *
 *   JNODE_DIRTY: If a node is dirty, it must be flushed.  But in order to be written it
 *   must be allocated first.  In order to be considered allocated, the jnode must have
 *   exactly one of { JNODE_WANDER, JNODE_RELOC } set.  These two bits are exclusive, and
 *   all dirtied jnodes eventually have one of these bits set during each transaction.
 *
 *   JNODE_CREATED: The node was freshly created in its transaction and has no previous
 *   block address, so it is unconditionally assigned to be relocated, although this is
 *   mainly for code-convenience.  It is not being 'relocated' from anything, but in
 *   almost every regard it is treated as part of the relocate set.  The JNODE_CREATED bit
 *   remains set even after JNODE_RELOC is set, so the actual relocate can be
 *   distinguished from the created-and-allocated set easily: relocate-set members
 *   (belonging to the preserve-set) have (JNODE_RELOC) set and created-set members which
 *   have no previous location to preserve have (JNODE_RELOC | JNODE_CREATED) set.
 *
 *   JNODE_WANDER: The flush algorithm made the decision to maintain the pre-existing
 *   location for this node and it will be written to the wandered-log.  FIXME(E): The
 *   following NOTE needs to be fixed by adding a wander_list to the atom.  NOTE: In this
 *   case, flush sets the node to be clean!  By returning the node to the clean list,
 *   where the log-writer expects to find it, flush simply pretends it was written even
 *   though it has not been.  Clean nodes with JNODE_WANDER set cannot be released from
 *   memory (checked in reiser4_releasepage()).
 *
 *   JNODE_RELOC: The flush algorithm made the decision to relocate this block (if it was
 *   not created, see note above).  A block with JNODE_RELOC set is eligible for
 *   early-flushing and may be submitted during flush_empty_queues.  When the JNODE_RELOC
 *   bit is set on a znode, the parent node's internal item is modified and the znode is
 *   rehashed.
 *
 *   JNODE_FLUSH_QUEUED: This bit is set when a call to flush enters the jnode into its
 *   flush queue.  This means the jnode is not on any clean or dirty list, instead it is
 *   on a flush_position->capture_list indicating that it is in a queue that will be
 *   submitted for writing when the flush finishes.  This prevents multiple concurrent
 *   flushes from attempting to flush the same node.
 *
 *   (DEAD STATE BIT) JNODE_FLUSH_BUSY: This bit was set during the bottom-up
 *   squeeze-and-allocate on a node while its children are actively being squeezed and
 *   allocated.  This flag was created to avoid submitting a write request for a node
 *   while its children are still being allocated and squeezed.  However, this flag is no
 *   longer needed because flush_empty_queue is only called in one place after flush
 *   finishes.  It used to be that flush_empty_queue was called periodically during flush
 *   when there was a fixed queue, but that is no longer done.  See the changes on August
 *   6, 2002 when this support was removed.
 *
 * With these state bits, we describe a test used frequently in the code below,
 * jnode_is_flushprepped() (and the spin-lock-taking jnode_check_flushprepped()).  The
 * test for "flushprepped" returns true if any of the following are true:
 *
 *   - The node is not dirty
 *   - The node has JNODE_RELOC set
 *   - The node has JNODE_WANDER set
 *
 * If either the node is not dirty or it has already been processed by flush (and assigned
 * JNODE_WANDER or JNODE_RELOC), then it is prepped.  If jnode_is_flushprepped() returns
 * true then flush has work to do on that node.
 */

/* FLUSH_PREP_ONCE_PER_TRANSACTION: Within a single transaction a node is never
 * flushprepped twice (unless an explicit call to flush_unprep is made as described in
 * detail below).  For example a node is dirtied, allocated, and then early-flushed to
 * disk and set clean.  Before the transaction commits, the page is dirtied again and, due
 * to memory pressure, the node is flushed again.  The flush algorithm will not relocate
 * the node to a new disk location, it will simply write it to the same, previously
 * relocated position again.
 */

/* THE BOTTOM-UP VS. TOP-DOWN ISSUE: This code implements a bottom-up algorithm where we
 * start at a leaf node and allocate in parent-first order by iterating to the right.  At
 * each step of the iteration, we check for the right neighbor.  Before advancing to the
 * right neighbor, we check if the current position and the right neighbor share the same
 * parent.  If they do not share the same parent, the parent is allocated before the right
 * neighbor.  This process goes recursively up the tree as long as the right neighbor and
 * the current position have different parents, then it allocates the
 * right-neighbors-with-different-parents on the way back down.  This process is described
 * in more detail in flush_squalloc_changed_ancestor and the recursive function
 * flush_squalloc_one_changed_ancestor.  But the purpose here is not to discuss the
 * specifics of the bottom-up approach as it is to contrast the bottom-up and top-down
 * approaches.
 *
 * The top-down algorithm was implemented earlier (April-May 2002).  In the top-down
 * approach, we find a starting point by scanning left along each level past dirty nodes,
 * then going up and repeating the process until the left node and the parent node are
 * clean.  We then perform a parent-first traversal from the starting point, which makes
 * allocating in parent-first order trivial.  After one subtree has been allocated in this
 * manner, we move to the right, try moving upward, then repeat the parent-first
 * traversal.
 *
 * Both approaches have problems that need to be addressed.  Both are approximately the
 * same amount of code, but the bottom-up approach has advantages in the order it acquires
 * locks which, at the very least, make it the better approach.  At first glance each one
 * makes the other one look simpler, so it is important to remember a few of the problems
 * with each one.
 *
 * Main problem with the top-down approach: When you encounter a clean child during the
 * parent-first traversal, what do you do?  You would like to avoid searching through a
 * large tree of nodes just to find a few dirty leaves at the bottom, and there is not an
 * obvious solution.  One of the advantages of the top-down approach is that during the
 * parent-first traversal you check every child of a parent to see if it is dirty.  In
 * this way, the top-down approach easily handles the main problem of the bottom-up
 * approach: unallocated children.
 *
 * The unallocated children problem is that before writing a node to disk we must make
 * sure that all of its children are allocated.  Otherwise, the writing the node means
 * extra I/O because the node will have to be written again when the child is finally
 * allocated.
 *
 * WE HAVE NOT YET ELIMINATED THE UNALLOCATED CHILDREN PROBLEM.  Except for bugs, this
 * should not cause any file system corruption, it only degrades I/O performance because a
 * node may be written when it is sure to be written at least one more time in the same
 * transaction when the remaining children are allocated.  What follows is a description
 * of how we will solve the problem.
 */

/* HANDLING UNALLOCATED CHILDREN: During flush we may allocate a parent node then,
 * proceeding in parent first order, allocate some of its left-children, then encounter a
 * clean child in the middle of the parent.  We do not allocate the clean child, but there
 * may remain unallocated (dirty) children to the right of the clean child.  If we were to
 * stop flushing at this moment and write everything to disk, the parent might still
 * contain unallocated children.

 * We could try to allocate all the descendents of every node that we allocate, but this
 * is not necessary.  Doing so could result in allocating the entire tree: if the root
 * node is allocated then every unallocated node would have to be allocated before
 * flushing.  Actually, we do not have to write a node just because we allocate it.  It is
 * possible to allocate but not write a node during flush, when it still has unallocated
 * children.  However, this approach is probably not optimal for the following reason.
 *
 * The flush algorithm is designed to allocate nodes in parent-first order in an attempt
 * to optimize reads that occur in the same order.  Thus we are read-optimizing for a
 * left-to-right scan through all the leaves in the system, and we are hoping to
 * write-optimize at the same time because those nodes will be written together in batch.
 * What happens, however, if we assign a block number to a node in its read-optimized
 * order but then avoid writing it because it has unallocated children?  In that
 * situation, we lose out on the write-optimization aspect because a node will have to be
 * written again to the its location on the device, later, which likely means seeking back
 * to that location.
 *
 * So there are tradeoffs. We can choose either:
 *
 * A. Allocate all unallocated children to preserve both write-optimization and
 * read-optimization, but this is not always desirable because it may mean having to
 * allocate and flush very many nodes at once.
 *
 * B. Defer writing nodes with unallocated children, keep their read-optimized locations,
 * but sacrifice write-optimization because those nodes will be written again.
 *
 * C. Defer writing nodes with unallocated children, but do not keep their read-optimized
 * locations.  Instead, choose to write-optimize them later, when they are written.  To
 * facilitate this, we "undo" the read-optimized allocation that was given to the node so
 * that later it can be write-optimized, thus "unpreparing" the flush decision.  This is a
 * case where we disturb the FLUSH_PREP_ONCE_PER_TRANSACTION rule described above.  By a
 * call to flush_unprep() we will: if the node was wandered, unset the JNODE_WANDER bit;
 * if the node was relocated, unset the JNODE_RELOC bit, non-deferred-deallocate its block
 * location, and set the JNODE_CREATED bit, effectively setting the node back to an
 * unallocated state.
 *
 * We will take the following approach in v4.0: for twig nodes we will always finish
 * allocating unallocated children (A).  For nodes with (level > TWIG) with will defer
 * writing and choose write-optimization (C).
 *
 * To summarize, there are several parts to a solution that avoids the problem with
 * unallocated children:
 *
 * 1. When flush reaches a stopping point (e.g., a clean node), it should continue calling
 * squeeze-and-allocate on any remaining unallocated children.  FIXME: Difficulty to
 * implement: should be simple -- amounts to adding a while loop to jnode_flush, see
 * comments in that function.
 *
 * 2. When flush reaches flush_empty_queue(), some of the (level > TWIG) nodes may still
 * have unallocated children.  If the twig level has unallocated children it is an
 * assertion failure.  If a higher-level node has unallocated children, then it should be
 * explicitly de-allocated by a call to flush_unprep().  FIXME: Difficulty to implement:
 * should be simple.
 *
 * 3. (CPU-Optimization) Checking whether a node has unallocated children may consume more
 * CPU cycles than we would like, and it is possible (but medium complexity) to optimize
 * this somewhat in the case where large sub-trees are flushed.  The following observation
 * helps: if both the left- and right-neighbor of a node are processed by the flush
 * algorithm then the node itself is guaranteed to have all of its children allocated.
 * However, the cost of this check may not be so expensive after all: it is not needed for
 * leaves and flush can guarantee this property for twigs.  That leaves only (level >
 * TWIG) nodes that have to be checked, so this optimization only helps if at least three
 * (level > TWIG) nodes are flushed in one pass, and the savings will be very small unless
 * there are many more (level > TWIG) nodes.  But if there are many (level > TWIG) nodes
 * then the number of blocks being written will be very large, so the savings may be
 * insignificant.  That said, the idea is to maintain both the left and right edges of
 * nodes that are processed in flush.  When flush_empty_queue() is called, a relatively
 * simple test will tell whether the (level > TWIG) node is on the edge.  If it is on the
 * edge, the slow check is necessary, but if it is in the interior then it can be assumed
 * to have all of its children allocated.  FIXME: medium complexity to implement, but
 * simple to verify given that we must have a slow check anyway.
 *
 * 4. (Optional) This part is optional, not for v4.0--flush should work independently of
 * whether this option is used or not.  Called RAPID_SCAN, the idea is to amend the
 * left-scan operation to take unallocated children into account.  Normally, the left-scan
 * operation goes left as long as adjacent nodes are dirty up until some large maximum
 * value (FLUSH_SCAN_MAXNODES) at which point it stops and begins flushing.  But scan-left
 * may stop at a position where there are unallocated children to the left with the same
 * parent.  When RAPID_SCAN is enabled, the ordinary scan-left operation stops after
 * FLUSH_RELOCATE_THRESHOLD, which is much smaller than FLUSH_SCAN_MAXNODES, then procedes
 * with a rapid scan.  The rapid scan skips all the interior children of a node--if the
 * leftmost child of a twig is dirty, check its left neighbor (the rightmost child of the
 * twig to the left).  If the left neighbor of the leftmost child is also dirty, then
 * continue the scan at the left twig and repeat.  This option will cause flush to
 * allocate more twigs in a single pass, but it also has the potential to write many more
 * nodes than would otherwise be written without the RAPID_SCAN option.  RAPID_SCAN
 * was partially implemented, code removed August 12, 2002 by JMACD.
 */

/* FLUSH CALLED ON NON-LEAF LEVEL.  Most of our design considerations assume that the
 * starting point for flush is a leaf node, but actually the flush code cares very little
 * about whether or not this is true.  It is possible that all the leaf nodes are flushed
 * and dirty parent nodes still remain, in which case jnode_flush() is called on a
 * non-leaf argument.  Flush doesn't care--it treats the argument node as if it were a
 * leaf, even when it is not.  This is a simple approach, and there may be a more optimal
 * policy but until a problem with this approach is discovered, simplest is probably best.
 *
 * NOTE: In this case, the ordering produced by flush is parent-first only if you ignore
 * the leafs.  This is done as a matter of simplicity and there is only one (shaky)
 * justification.  When an atom commits, it flushes all leaf level nodes first, followed
 * by twigs, and so on.  With flushing done in this order, if flush is eventually called
 * on a non-leaf node it means that (somehow) we reached a point where all leaves are
 * clean and only internal nodes need to be flushed.  If that it the case, then it means
 * there were no leaves that were the parent-first preceder/follower of the parent.  This
 * is expected to be a rare case, which is why we do nothing special about it.  However,
 * memory pressure may pass an internal node to flush when there are still dirty leaf
 * nodes that need to be flushed, which could prove our original assumptions
 * "inoperative".  If this needs to be fixed, then flush_scan_left/right should have
 * special checks for the non-leaf levels.  For example, instead of passing from a node to
 * the left neighbor, it should pass from the node to the left neighbor's rightmost
 * descendent (if dirty).
 */

/* REPACKING AND RESIZING.  We walk the tree in 4MB-16MB chunks, dirtying everything and
 * putting it into a transaction.  We tell the allocator to allocate the blocks as far as
 * possible towards one end of the logical device--the left (starting) end of the device
 * if we are walking from left to right, the right end of the device if we are walking
 * from right to left.  We then make passes in alternating directions, and as we do this
 * the device becomes sorted such that tree order and block number order fully correlate.
 * 
 * Resizing is done by shifting everything either all the way to the left or all the way
 * to the right, and then reporting the last block.
 */

/* RELOCATE DECISIONS: The code makes a decision to relocate in several places.  This
 * descibes the policy from the highest level:
 *
 * The FLUSH_RELOCATE_THRESHOLD parameter: If we count this many consecutive nodes on the
 * leaf level during flush-scan (right, left), then we unconditionally decide to relocate
 * leaf nodes.
 *
 * Otherwise, there are two contexts in which we make a decision to relocate:
 *
 * 1. The REVERSE PARENT-FIRST context: Implemented in flush_reverse_relocate_test().
 * During the initial stages of flush, after scan-left completes, we want to ask the
 * question: should we relocate this leaf node and thus dirty the parent node.  Then if
 * the node is a leftmost child its parent is its own paren-first preceder, thus we repeat
 * the question at the next level up, and so on.  In these cases we are moving in the
 * reverse-parent first direction.
 *
 * There is another case which is considered the reverse direction, which comes at the end
 * of a twig in flush_reverse_relocate_end_of_twig().  As we finish processing a twig we may
 * reach a point where there is a clean twig to the right with a dirty leftmost child.  In
 * this case, we may wish to relocate the child by testing if it should be relocated
 * relative to its parent.
 *
 * 2. The FORWARD PARENT-FIRST context: Testing for forward relocation is done in
 * flush_allocate_znode.  What distinguishes the forward parent-first case from the
 * reverse-parent first case is that the preceder has already been allocated in the
 * forward case, whereas in the reverse case we don't know what the preceder is until we
 * finish "going in reverse".  That simplifies the forward case considerably, and there we
 * actually use the block allocator to determine whether, e.g., a block closer to the
 * preceder is available.
 */

/* SQUEEZE_LEFT_EDGE: Unimplemented idea for future consideration.  The idea is, once we
 * finish scan-left and find a starting point, if the parent's left neighbor is dirty then
 * squeeze the parent's left neighbor and the parent.  This may change the
 * flush-starting-node's parent.  Repeat until the child's parent is stable.  If the child
 * is a leftmost child, repeat this left-edge squeezing operation at the next level up.
 * Note that we cannot allocate extents during this or they will be out of parent-first
 * order.  There is also some difficult coordinate maintenence issues.  We can't do a tree
 * search to find coordinates again (because we hold locks), we have to determine them
 * from the two nodes being squeezed.  Looks difficult, but has potential to increase
 * space utilization. */

/* NEW FLUSH QUEUEING / UNALLOCATED CHILDREN / MEMORY PRESSURE DEADLOCK (NFQUCMPD)
 *
 * Several ideas have been discussed to address a combination of these three issues.  This
 * discussion largely obsoletes the previous discussion above on the subject of
 * UNALLOCATED CHILDREN, though not entirely.  To summarize the three problems:
 *
 * Currently the jnode_flush routine builds a queue of all the nodes that it allocates
 * during a single squeeze-and-allocate traversal.  The queue may grow quite large as a
 * large number of adjacent dirty nodes are allocated.  Then the flush_empty_queue
 * function creates a BIO and calls submit_bio for all the contiguous block ranges that
 * were allocated.  There are several interrelated problems.
 *
 * First there is memory pressure: if we wait until there is no memory left to flush we
 * are in serious trouble because flush can require allocation for new blocks, especially
 * as extents are allocated.  A secondary but similar problem is deadlock.  If a thread
 * that requests memory causes memory pressure while holding a lock, which is entirely
 * possible, it may prevent flush from making any progress.  We have discussed these cases
 * and concluded that in the absolute worst case the complex, lock-taking,
 * memory-allocating flush algorithm may not be able to free memory once memory gets
 * tight, and the solution for the absolute worse case is an EMERGENCY FLUSH (discussed
 * after this section).  But we cannot accept that emergency flush be the common case
 * under memory pressure.
 *
 * The second problem is that flush queues too many nodes before submitting them to the
 * disk.  We would like to flush a little bit at a time without breaking the flush
 * algorithm.  As discussed above for the issue of unallocated children, we decided to
 * treat twig and leaf nodes specially--always allocating all children of a twig to ensure
 * proper read- and write-optimization of those levels.  We would like to modify the flush
 * algorithm to return control after it finishes squeezing all the childre of a single
 * twig, allowing the queue of nodes prepared for writing to be consumed somewhat before
 * continuing.
 *
 * With the modification that the flush algorithm will stop after finishing a twig, we can
 * impose a new flush-queueing design that will achieve the goal of avoiding emergency
 * flush in memory pressure in the common case.  To implement this we will create a new
 * kmalloced object called a "struct flush_handle" (?).  Every flush_handle is associated
 * with an atom, an atom maintains its list of flush_handles (instead of the
 * flush_position list it currently maintains), and these lists are joined when atoms
 * fuse.  A flush_handle object is passed into the jnode_flush() routine.  A flush_handle
 * contains at least these fields (plus a semaphore and/or spinlock):
 *
 *   txn_atom           *atom;            -- The current atom of this flush_handle--maintained during atom fusion.
 *   flushers_list_link  flushers_link;   -- A list link of all flush_handles for an atom.
 *   capture_list_head   queue;           -- A list of jnodes (in allocation order), when a jnode is allocated it is
 *                                           moved off the dirty list onto this list.
 *   atomic_t            number_queued;   -- Number of jnodes in the queue.
 *   atomic_t            number_prepped;  -- Number of jnodes ready for submit_bio.
 *   atomic_t            number_bios_out; -- Number of BIOs/completion events outstanding.
 *   __u32               state;           -- What state the handle is in.
 *
 * When a submit_bio request is issued for a flush_handle, the completion will decrement
 * the number_bios_out and not try to remove the flush_handle from any list (if the
 * becomes completely unused).  When the atom is locked (for some reason), we can find
 * unused flush_handles and either free them or reuse them.
 *
 * A flush_handle can have these states:
 *
 *   EMPTY_QUEUE -- In this state the handle can be freed as long as the atom is locked
 *   (in order to remove it from the list).  For this state:
 *     (number_queued == 0 && number_prepped == 0)
 * 
 *   NON_EMPTY_QUEUE -- In this state the handle is not being used by any current flusher
 *   but it has prepared nodes ready to for submit_bio (to be put "in-flight").  For this
 *   state:
 *     (number_queued == number_prepped && number_queued > 0)
 * 
 *   CURRENT_FLUSHER -- A call to jnode_flush is currently filling this queue.  This state
 *   implies that the queue is being populated with nodes ready for flushing, but not all
 *   of the queue nodes are fully "prepared".  For this state:
 *     (number_queued <= number_prepped && number_queued >= 0)
 *
 * To manage these filling these queues we have a number of dedicated flushing threads.
 * In addition, any atom that is trying to commit may be flushed by the thread that is
 * closing it.  The dedicated flushing threads attempt to maintain a certain minimum
 * number of outstanding write requests.  In addition to the in-flight requests, the
 * dedicated flushing threads also attempt to maintain some additional number of queued
 * and prepped jnodes that are ready to write but not yet submitted.  When the number of
 * in-flight requests falls beneath the threshold, more BIOs are submitted.  When the
 * number of prepped nodes falls beneath some other threshold, more flushing work is
 * performed.
 *
 * Flush is never called directly from a memory pressure handler.  Instead, a memory
 * pressure handler finds a flush_handle with number_prepped > 0 and calls submit_bio
 * until the number of in-flight requests exceeds the "nr_to_flush" parameter supplied by
 * the VM.
 *
 * This will require tuning to maintain a proper balance, but it should attain the goal:
 * (1) the disk is kept busy (2) there is almost always enough allocated and prepped nodes
 * ready to submit to the disk and thus free memory.
 *
 * What is required of the flush code to implement this strategy?  I will place comments
 * in the code below marked with FIXME_NFQUCMPD.  I hope that no one ever uses this login.
 */

/* EMERGENCY FLUSH: In the worst case, we must have a way to assign block numbers without
 * any memory allocation.  This requires that we can allocate a node without updating its
 * parent immediately.  We can walk the dirty list and assign block numbers, and somehow
 * the parent must "know" to update itself later.  This may be difficult to implement
 * efficiently, so it may result in special code that is only activated when it knows that
 * emergency flushing has occurred.
 */

/********************************************************************************
 * DECLARATIONS:
 ********************************************************************************/

/* The flush_scan data structure maintains the state of an in-progress flush-scan on a
 * single level of the tree.  A flush-scan is used for counting the number of adjacent
 * nodes to flush, which is used to determine whether we should relocate, and it is also
 * used to find a starting point for flush.  A flush-scan object can scan in both right
 * and left directions via the flush_scan_left() and flush_scan_right() interfaces.  The
 * right- and left-variations are similar but perform different functions.  When scanning
 * left we (optionally perform rapid scanning and then) longterm-lock the endpoint node.
 * When scanning right we are simply counting the number of adjacent, dirty nodes. */
struct flush_scan {

	/* The current number of nodes scanned on this level. */
	unsigned  count;

	/* There may be a maximum number of nodes for a scan on any single level.  When
	 * going leftward, max_count is determined by FLUSH_SCAN_MAXNODES (see reiser4.h) */
	unsigned max_count;

	/* Direction: Set to one of the sideof enumeration: { LEFT_SIDE, RIGHT_SIDE }. */
	sideof direction;

	/* Initially @stop is set to false then set true once some condition stops the
	 * search (e.g., we found a clean node before reaching max_count or we found a
	 * node belonging to another atom). */
	int       stop;

	/* The current scan position.  If @node is non-NULL then its reference count has
	 * been incremented to reflect this reference. */
	jnode    *node;

	/* A handle for zload/zrelse of current scan position node. */
	load_count node_load;

	/* During left-scan, if the final position (a.k.a. endpoint node) is formatted the
	 * node is locked using this lock handle.  The endpoint needs to be locked for
	 * transfer to the flush_position object after scanning finishes. */
	lock_handle node_lock;

	/* When the position is unformatted, its parent, coordinate, and parent
	 * zload/zrelse handle. */
	lock_handle parent_lock;
	coord_t     parent_coord;
	load_count parent_load;

	/* The block allocator preceder hint.  Sometimes flush_scan determines what the
	 * preceder is and if so it sets it here, after which it is copied into the
	 * flush_position.  Otherwise, the preceder is computed later. */
	reiser4_block_nr preceder_blk;
};

/* An encapsulation of the current flush point and all the parameters that are passed
 * through the entire squeeze-and-allocate stage of the flush routine.  A single
 * flush_position object is constructed after left- and right-scanning finishes. */
struct flush_position {
	jnode                *point;           /* The current position, if it is formatted (with a minor exception,
						* allowing an unformatted node to be set here, explained in jnode_flush,
						* below). */
	lock_handle           point_lock;      /* The current position lock, if it is formatted. */
	lock_handle           parent_lock;     /* Parent of the current position, if it is unformatted. */
	coord_t               parent_coord;    /* Parent coordinate of the current position, if unformatted. */
	load_count            point_load;      /* Loaded point */
	load_count            parent_load;     /* Loaded parent */
	reiser4_blocknr_hint  preceder;        /* The flush 'hint' state. */
	int                   leaf_relocate;   /* True if enough leaf-level nodes were
						* found to suggest a relocate policy. */
	int                  *nr_to_flush;     /* If called under memory pressure,
						* indicates how many nodes the VM asked to flush. */
	int                   alloc_cnt;       /* The number of nodes allocated during squeeze and allococate. */
	int                   prep_or_free_cnt;/* The number of nodes prepared for write (allocate) or squeezed and freed. */
	capture_list_head     queue;           /* The flush queue holds allocated but not-yet-submitted jnodes that are
						* actually written when flush_empty_queue() is called. */
 	int                   queue_num;       /* The current number of queue entries. */

	flushers_list_link    flushers_link;   /* A list link of all flush_positions active for an atom. */
	txn_atom             *atom;            /* The current atom of this flush_position--maintained during atom fusion. */
};

/* The flushers list maintains a per-atom list of active flush_positions.  This is because
 * each concurrent flush maintains its own, private queue of allocated nodes.  When a node
 * is placed on the flush queue it is removed from the atom's dirty list.  When the node
 * is finally written to disk, it is returned to the atom's clean list.  Remember that
 * atoms can fuse during flush--the flushers lists are fused and each flush_position and
 * every jnode in the flush queues are updated to reflect the new combined atom. */
TS_LIST_DEFINE(flushers, struct flush_position, flushers_link);

/* Flush-scan helper functions. */
static void          flush_scan_init              (flush_scan *scan);
static void          flush_scan_done              (flush_scan *scan);
static int           flush_scan_set_current       (flush_scan *scan, jnode *node, unsigned add_size, const coord_t *parent);
static int           flush_scan_finished          (flush_scan *scan);
static int           flush_scanning_left          (flush_scan *scan);

/* Flush-scan algorithm. */
static int           flush_scan_left              (flush_scan *scan, flush_scan *right, jnode *node, unsigned limit);
static int           flush_scan_right             (flush_scan *scan, jnode *node, unsigned limit);
static int           flush_scan_common            (flush_scan *scan, flush_scan *other);
static int           flush_scan_formatted         (flush_scan *scan);
static int           flush_scan_extent            (flush_scan *scan, int skip_first);
static int           flush_scan_extent_coord      (flush_scan *scan, const coord_t *in_coord);

/* Initial flush-point ancestor allocation. */
static int           flush_alloc_ancestors        (flush_position *pos);
static int           flush_alloc_one_ancestor     (coord_t *coord, flush_position *pos);
static int           flush_set_preceder           (const coord_t *coord_in, flush_position *pos);

/* Main flush algorithm.  Note on abbreviation: "squeeze and allocate" == "squalloc". */
static int           flush_forward_squalloc                (flush_position *pos);
static int           flush_squalloc_one_changed_ancestor (znode *node, int call_depth, flush_position *pos);
static int           flush_squalloc_changed_ancestors    (flush_position *pos);

/* Flush squeeze implementation. */
/*static*/ int       squalloc_right_neighbor      (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig          (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig_cut      (coord_t * to, reiser4_key * to_key, znode *left);
static int           squeeze_right_non_twig       (znode *left, znode *right);
static int           shift_one_internal_unit      (znode *left, znode *right);

/* Flush reverse parent-first relocation routines. */
static int           flush_reverse_relocate_if_close_enough    (const reiser4_block_nr *pblk, const reiser4_block_nr *nblk);
static int           flush_reverse_relocate_test               (jnode *node, const coord_t *parent_coord, flush_position *pos);
static int           flush_reverse_relocate_check_dirty_parent (jnode *node, const coord_t *parent_coord, flush_position *pos);
static int           flush_reverse_relocate_end_of_twig        (flush_position *pos);

/* Flush allocate write-queueing functions: */
static int           flush_allocate_znode         (znode *node, coord_t *parent_coord, flush_position *pos);
static int           flush_allocate_znode_update  (znode *node, coord_t *parent_coord, flush_position *pos);
static int           flush_rewrite_jnode          (jnode *node);
static int           flush_queue_jnode            (jnode *node, flush_position *pos);
static int           flush_empty_queue            (flush_position *pos);

/* Flush helper functions: */
static int           jnode_lock_parent_coord      (jnode *node, coord_t *coord, lock_handle *parent_lh, load_count *parent_zh, znode_lock_mode mode);
static int           znode_get_utmost_if_dirty    (znode *node, lock_handle *right_lock, sideof side, znode_lock_mode mode);
static int           znode_same_parents           (znode *a, znode *b);

/* Flush position functions */
static int           flush_pos_init               (flush_position *pos, int *nr_to_flush);
static int           flush_pos_valid              (flush_position *pos);
static void          flush_pos_done               (flush_position *pos);
static int           flush_pos_stop               (flush_position *pos);
static int           flush_pos_on_twig_level        (flush_position *pos);
static int           flush_pos_to_child_and_alloc (flush_position *pos);
static int           flush_pos_to_parent          (flush_position *pos);
static int           flush_pos_set_point          (flush_position *pos, jnode *node);
static void          flush_pos_release_point      (flush_position *pos);
static int           flush_pos_lock_parent        (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, load_count *parent_load, znode_lock_mode mode);

/* Flush debug functions */
static const char*   flush_pos_tostring           (flush_position *pos);
static const char*   flush_jnode_tostring         (jnode *node);
static const char*   flush_znode_tostring         (znode *node);
static const char*   flush_flags_tostring         (int flags);

static flush_params *flush_get_params( void );

/* This flush_cnt variable is used to track the number of concurrent flush operations,
 * useful for debugging.  It is initialized in txnmgr.c out of laziness (because flush has
 * no static initializer function...) */
ON_DEBUG (atomic_t flush_cnt;)

/********************************************************************************
 * TODO LIST (no particular order):
 ********************************************************************************/

/* I have labelled most of the legitimate FIXME comments in this file with letters to
 * indicate which issue they relate to.  There are a few miscellaneous FIXMEs with
 * specific names mentioned instead that need to be inspected/resolved. */

/* A. Deal with the IO completion issue described at the end of jnode_flush(), then remove
 * the temporary-hack txn_wait_on_io() in txnmgr.c.  Currently it is possible for a
 * transaction to commit before all of its IO has completed... Solution described
 * below. */

/* B. There is an issue described in flush_reverse_relocate_test having to do with an
 * imprecise is_preceder? check having to do with partially-dirty extents.  The code that
 * sets preceder hints and computes the preceder is basically untested.  Careful testing
 * needs to be done that preceder calculations are done correctly, since if it doesn't
 * affect correctness we will not catch this stuff during regular testing. */

/* C. EINVAL, EDEADLK, ENAVAIL, ENOENT handling.  It is unclear which of these are
 * considered expected but unlikely conditions.  Flush currently returns 0 (i.e., success
 * but no progress, i.e., restart) whenever it receives any of these in jnode_flush().
 * Many of the calls that may produce one of these return values (i.e.,
 * longterm_lock_znode, reiser4_get_parent, reiser4_get_neighbor, ...) check some of these
 * values themselves and, for instance, stop flushing instead of resulting in a restart.
 * If any of these results are true error conditions then flush will go into a busy-loop,
 * as we noticed during testing when a corrupt tree caused find_child_ptr to return
 * ENOENT.  It needs careful thought and testing of corner conditions.
 */

/* D. Atomicity of flush_prep against deletion and flush concurrency.  Suppose a created
 * block is assigned a block number then early-flushed to disk.  It is dirtied again and
 * flush is called again.  Concurrently, that block is deleted, and the de-allocation of
 * its block number does not need to be deferred, since it is not part of the preserve set
 * (i.e., it didn't exist before the transaction).  I think there may be a race condition
 * where flush writes the dirty, created block after the non-deferred deallocated block
 * number is re-allocated, making it possible to write deleted data on top of non-deleted
 * data.  Its just a theory, but it needs to be thought out. */

/* E. Never put JNODE_WANDER blocks in the flush queue.  This is easy to implement, but it
 * is done for historical reasons related to the time when we had no log-writing and the
 * test layout.  If (WRITE_LOG == 0) then wandered blocks in the flush queue makes sense
 * (and the test layout doesn't support WRITE_LOG, I think?), but once (WRITE_LOG == 1)
 * placing wandered blocks in the flush queue can only cause more BIO objects to be
 * allocated than might otherwise be required.  We need to create a wander_queue to solve
 * this properly. */

/* F. bio_alloc() failure is not handled gracefully. */

/* G. Unallocated children. */

/* H. Add a WANDERED_LIST to the atom to clarify the placement of wandered blocks. */

/* I. SHORT LIST:
 *
 * FIXME: Rename flush-scan to scan-point, (flush-pos to flush-point?)
 */

/********************************************************************************
 * JNODE_FLUSH: MAIN ENTRY POINT
 ********************************************************************************/

/* This is the main entry point for flushing a jnode, called by the transaction manager
 * when an atom closes (to commit writes) and called by the VM under memory pressure (via
 * page_cache.c:page_common_writeback() to early-flush dirty blocks).
 *
 * The "argument" @node tells flush where to start.  From there, flush searches through
 * the adjacent nodes to find a better place to start the parent-first traversal, during
 * which nodes are squeezed and allocated (squalloc).  To find a "better place" to start
 * squalloc first we perform a flush_scan.
 *
 * Flush-scanning may be performed in both left and right directions, but for different
 * purposes.  When scanning to the left, we are searching for a node that precedes a
 * sequence of parent-first-ordered nodes which we will then flush in parent-first order.
 * During flush-scanning, we also take the opportunity to count the number of consecutive
 * leaf nodes.  If this number is past some threshold (FLUSH_RELOCATE_THRESHOLD), then we
 * make a decision to reallocate leaf nodes (thus favoring write-optimization).
 *
 * Since the flush argument node can be anywhere in a sequence of dirty leaves, there may
 * also be dirty nodes to the right of the argument.  If the scan-left operation does not
 * count at least FLUSH_RELOCATE_THRESHOLD nodes then we follow it with a right-scan
 * operation to see whether there is, in fact, enough nodes to meet the relocate
 * threshold.  Each right- and left-scan operation uses a single flush_scan object.
 *
 * After left-scan and possibly right-scan, we prepare a flush_position object with the
 * starting flush point or parent coordinate, which was determined using scan-left.
 * 
 * Next we call the main flush routine, flush_forward_squalloc, which iterates along the
 * leaf level, squeezing and allocating nodes (and placing them into the flush queue).
 *
 * After flush_forward_squalloc returns we take extra steps to ensure that all the children
 * of the final twig node are allocated--this involves repeating flush_forward_squalloc
 * until we finish at a twig with no unallocated children.
 *
 * Finally, we call flush_empty_queue to submit write-requests to disk.  If we encounter
 * any above-twig nodes during flush_empty_queue that still have unallocated children, we
 * flush_unprep them.
 *
 * Flush treats several "failure" cases as non-failures, essentially causing them to start
 * over.  EDEADLK is one example.  FIXME:(C) EINVAL, ENAVAIL, ENOENT: these should
 * probably be handled properly rather than restarting, but there are a bunch of cases to
 * audit.
 */
int jnode_flush (jnode *node, int *nr_to_flush, int flags)
{
	int ret = 0;
	flush_position flush_pos;
	flush_scan right_scan;
	flush_scan left_scan;

	assert ("jmacd-76619", lock_stack_isclean (get_current_lock_stack ()));

	/* Flush-concurrency debug code */
	ON_DEBUG (atomic_inc (& flush_cnt);
		  trace_on (TRACE_FLUSH, "flush enter: pid %ul %u concurrent procs\n", current_pid, atomic_read (& flush_cnt));
		  trace_if (TRACE_FLUSH, if (atomic_read (& flush_cnt) > 1) { info ("flush concurrency\n"); }););

	spin_lock_jnode (node);

	/* A special case for znode-above-root.  The above-root (fake) znode is captured
	 * and dirtied when the tree height changes or when the root node is relocated.
	 * This causes atoms to fuse so that changes at the root are serialized.  However,
	 * this node is never flushed.  This special case used to be in lock.c to prevent
	 * the above-root node from ever being captured, but now that it is captured we
	 * simply prevent it from flushing.  The log-writer code relies on this to
	 * properly log superblock modifications of the tree height. */
	if (jnode_is_znode(node) && znode_above_root(JZNODE(node))) {
		/* Just pass dirty znode-above-root to overwrite set. */
		jnode_set_wander(node);
		spin_unlock_jnode(node);
		jnode_set_clean(node);
		trace_on (TRACE_FLUSH, "flush aboveroot %s %s\n", flush_jnode_tostring (node), flush_flags_tostring (flags));
		goto clean_out;
	}

	/* A race is possible where node is not dirty or worse, not connected, by this
	 * point.  It is possible since more than one process may call jnode_flush
	 * concurrently and the node may already be clean by the time we obtain the
	 * spinlock above.  Likewise, a node may be deleted at this point.  It is possible
	 * for a znode to be unconnected as well, since these nodes are taken off the
	 * dirty list (a search-by-key never will encounter an unconnected node, but we
	 * are bypassing that mechanism here). */
	if (! jnode_is_dirty (node) ||
	    (jnode_is_znode (node) && !znode_is_connected (JZNODE (node))) ||
	    JF_ISSET (node, JNODE_HEARD_BANSHEE) ||
	    JF_ISSET (node, JNODE_FLUSH_QUEUED)) {
		if (nr_to_flush != NULL) {
			(*nr_to_flush) = 0;
		}
		spin_unlock_jnode (node);
		trace_on (TRACE_FLUSH, "flush nothing %s %s\n", flush_jnode_tostring (node), flush_flags_tostring (flags));
		goto clean_out;
	}

	/* Flush may be called on a jnode that has already been flushed once in this
	 * transaction.  In this case, the flush algorithm has already run and made a
	 * decision to relocate/overwrite this node and given it a new/temporary location.
	 * The node was then flushed and subsequently dirtied again.  Now flush is called
	 * again and jnode_is_flushprepped returns true.  At this point we simply re-submit
	 * the block to disk using the previously decided location. */
	if (jnode_is_flushprepped (node)) {

		trace_on (TRACE_FLUSH, "flush rewrite %s %s\n", flush_jnode_tostring (node), flush_flags_tostring (flags));
		ret = flush_rewrite_jnode (node);

		assert ("jmacd-97755", spin_jnode_is_not_locked (node));

		if (nr_to_flush != NULL) {
			(*nr_to_flush) = 1;
		}

		goto clean_out;
	}

	spin_unlock_jnode (node);

	trace_on (TRACE_FLUSH, "flush squalloc %s %s\n", flush_jnode_tostring (node), flush_flags_tostring (flags));

	/* Initialize a flush position.  Currently this cannot fail but if any memory
	 * allocation, locks, etc. were needed then this would be the place to fail before
	 * flush really gets going. */
	if ((ret = flush_pos_init (& flush_pos, nr_to_flush))) {
		goto clean_out;
	}

	flush_scan_init (& right_scan);
	flush_scan_init (& left_scan);

	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_BRIEF));*/
	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_CHECK));*/

	/* First scan left and remember the leftmost scan position.  If the leftmost
	 * position is unformatted we remember its parent_coord.  We scan until counting
	 * FLUSH_SCAN_MAXNODES. */
	if ((ret = flush_scan_left (& left_scan, & right_scan, node, flush_get_params()->scan_maxnodes))) {
		goto failed;
	}

	/* Then possibly go right to decide if we will use a policy of relocating leaves.
	 * This is only done if we did not scan past (and count) enough nodes during the
	 * leftward scan.  If we do scan right, we only care to go far enough to establish
	 * that at least FLUSH_RELOCATE_THRESHOLD number of nodes are being flushed.  The
	 * scan limit is the difference between left_scan.count and the threshold. */
	if ((left_scan.count < flush_get_params()->relocate_threshold) &&
	    (ret = flush_scan_right (& right_scan, node, flush_get_params()->relocate_threshold - left_scan.count))) {
		goto failed;
	}

	/* Only the right-scan count is needed, release any rightward locks right away. */
	flush_scan_done (& right_scan);

	/* ... and the answer is: we should relocate leaf nodes if at least
	 * FLUSH_RELOCATE_THRESHOLD nodes were found. */
	flush_pos.leaf_relocate = (left_scan.count + right_scan.count >= flush_get_params()->relocate_threshold);

	/*assert ("jmacd-6218", jnode_check_dirty (left_scan.node));*/

	/* Funny business here.  We set the 'point' in the flush_position at prior to
	 * starting flush_forward_squalloc regardless of whether the first point is
	 * formatted or unformatted.  Without this there would be an invariant, in the
	 * rest of the code, that if the flush_position is unformatted then
	 * flush_position->point is NULL and flush_position->parent_{lock,coord} is set,
	 * and if the flush_position is formatted then flush_position->point is non-NULL
	 * and no parent info is set.
	 *
	 * This seems lazy, but it makes the initial calls to flush_reverse_relocate_test
	 * (which ask "is it the pos->point the leftmost child of its parent") much easier
	 * because we know the first child already.  Nothing is broken by this, but the
	 * reasoning is subtle.  Holding an extra reference on a jnode during flush can
	 * cause us to see nodes with HEARD_BANSHEE during squalloc, because nodes are not
	 * removed from sibling lists until they have zero reference count.  Flush would
	 * never observe a HEARD_BANSHEE node on the left-edge of flush, nodes are only
	 * deleted to the right.  So if nothing is broken, why fix it? */
	if ((ret = flush_pos_set_point (& flush_pos, left_scan.node))) {
		goto failed;
	}

	/* Now setup flush_pos using scan_left's endpoint. */
	if (jnode_is_unformatted (left_scan.node)) {
		coord_dup (& flush_pos.parent_coord, & left_scan.parent_coord);
		move_lh (& flush_pos.parent_lock, & left_scan.parent_lock);
		move_load_count (& flush_pos.parent_load, & left_scan.parent_load);
	} else {
		move_lh (& flush_pos.point_lock, & left_scan.node_lock);
	}

	/* In some cases, we discover the parent-first preceder during the
	 * leftward scan.  Copy it. */
	flush_pos.preceder.blk = left_scan.preceder_blk;
	flush_scan_done (& left_scan);

	/* Check for relocation and allocate ancestors of the flush position.  First
	 * perform a relocation check of the flush point (in reverse parent-first
	 * context--see flush_reverse_relocate_test), then if the node is a leftmost child
	 * and the parent is dirty repeat this process at the next level up.  Once
	 * reaching the highest ancestor, allocate on the way back down.  In otherwords,
	 * this operation recurses up in reverse parent-first order and then allocates on
	 * the way back.  This initializes the flush operation for the main
	 * flush_forward_squalloc loop, which continues to allocate in forward parent-first
	 * order. */
	if ((ret = flush_alloc_ancestors (& flush_pos))) {
		goto failed;
	}

	/* Do the main rightward-bottom-up squeeze and allocate loop. */
	if ((ret = flush_forward_squalloc (& flush_pos))) {
		/* FIXME(C): This ENAVAIL check is an ugly, UGLY hack to prevent a certain
		 * deadlocks by trying to prevent atoms from fusing during flushing.  We
		 * allow -ENAVAIL code to be returned from flush_forward_squalloc and
		 * continue to flush_empty_queue.  The proper solution, I feel, is to
		 * catch -EINVAL everywhere it may be generated, not at the top level like
		 * this.  For example, every call to jnode_lock_parent_coord should check
		 * ENAVAIL, perhaps should also check not-same-atom condition, then do the
		 * Right Thing. */
		if (ret != -ENAVAIL) {
			goto failed;
		}
		/* FIXME(C): Should be this: */
		/* goto failed; */
	}

	/* FIXME_NFQUCMPD: Here, handle the twig-special case for unallocated children.
	 * First, the flush_pos_stop() and flush_pos_valid() routines should be modified
	 * so that flush_pos_stop() sets a flush_position->stop flag to 1 without
	 * releasing the current position immediately--instead release it in
	 * flush_pos_done().  This is a better implementation than the current one anyway.
	 *
	 * It is not clear that all fields of the flush_position should not be released,
	 * but at the very least the parent_lock, parent_coord, and parent_load should
	 * remain held because they are hold the last twig when flush_pos_stop() is
	 * called.
	 *
	 * When we reach this point in the code, if the parent_coord is set to after the
	 * last item then we know that flush reached the end of a twig (and according to
	 * the new flush queueing design, we will return now).  If parent_coord is not
	 * past the last item, we should check if the current twig has any unallocated
	 * children to the right (we are not concerned with unallocated children to the
	 * left--in that case the twig itself should not have been allocated).  If the
	 * twig has unallocated children to the right, set the parent_coord to that
	 * position and then repeat the call to flush_forward_squalloc.
	 *
	 * Testing for unallocated children may be defined in two ways: if any internal
	 * item has a fake block number, it is unallocated; if any extent item is
	 * unallocated then all of its children are unallocated.  But there is a more
	 * aggressive approach: if there are any dirty children of the twig to the right
	 * of the current position, we may wish to relocate those nodes now.  Checking for
	 * potential relocation is more expensive as it requires knowing whether there are
	 * any dirty children that are not unallocated.  The extent_needs_allocation
	 * should be used after setting the correct preceder.
	 *
	 * When we reach the end of a twig at this point in the code, if the flush can
	 * continue (when the queue is ready) it will need some information on the future
	 * starting point.  That should be stored away in the flush_handle using a seal, I
	 * believe.  Holding a jref() on the future starting point may break other code
	 * that deletes that node.
	 */

	/* FIXME_NFQUCMPD: Also, we don't want to do any flushing when flush is called
	 * above the twig level.  If the VM calls flush above the twig level, do nothing
	 * and return (but figure out why this happens).  The txnmgr should be modified to
	 * only flush its leaf-level dirty list.  This will do all the necessary squeeze
	 * and allocate steps but leave unallocated branches and possibly unallocated
	 * twigs (when the twig's leftmost child is not dirty).  After flushing the leaf
	 * level, the remaining unallocated nodes should be given write-optimized
	 * locations.  (Possibly, the remaining unallocated twigs should be allocated just
	 * before their leftmost child.)
	 */
   
	/* Write anything left in the queue. */
	ret = flush_empty_queue (& flush_pos);

	/* Any failure reaches this point. */
   failed:

	if (nr_to_flush != NULL) {
		if (ret == 0) {
			trace_on (TRACE_FLUSH, "flush_jnode wrote %u blocks\n", flush_pos.prep_or_free_cnt);
			(*nr_to_flush) = flush_pos.prep_or_free_cnt;
		} else {
			(*nr_to_flush) = 0;
		}
	}

	if (ret == -EINVAL || ret == -EDEADLK || ret == -ENAVAIL || ret == -ENOENT) {
		/* FIXME(C): Except for EDEADLK, these should probably be handled properly
		 * in each case.  They already are handled in many cases. */ 
		/* Something bad happened, but difficult to avoid...  Try again! */
		trace_on (TRACE_FLUSH, "flush restartable failure: %d\n", ret);
		ret = 0;
	}

	if (ret != 0) {
		warning ("jmacd-16739", "flush failed: %d", ret);
	}

	flush_pos_done (& flush_pos);
	flush_scan_done (& left_scan);
	flush_scan_done (& right_scan);

	/* The clean_out label is reached by calls to jnode_flush that return before
	 * initializing the flush_position and the two flush_scan objects.  After those
	 * objects are initialized any abnormal return goes to the 'failed' label. */
 clean_out:

	/* Wait for io completion.  (FIXME_NFQUCMPD)
	 *
	 * FIXME(A): I THINK THIS IS KILLING COMMIT PERFORMANCE!
	 *
	 * FIXME(A): JMACD->ZAM,HANS: I don't think this will work, conditionally waiting for
	 * an IO completion here.  Certainly it is easiest if we allocate the io_handle on
	 * the stack, but the check for JNODE_FLUSH_COMMIT doesn't work because a call to
	 * early-flush will not wait here.  If the transaction commits shortly after
	 * memory pressure causes early flushing, then the transaction will not wait for
	 * the early-flush writes to complete.  I propose this solution:
	 *
	 * 1. Allocate io_handles w/ kmalloc
	 * 2. Maintain per-atom list of active io_handles
	 * 3. Fuse lists when atoms commit
	 * 4. Wait on all io_handles prior in PRE_COMMIT.
	 * 5. After this is accomplished, remove txn_wait_on_io() from txnmgr.c
	 *
	 * The alternative is to undconditionally call done_io_handle here, not just when
	 * JNODE_FLUSH_COMMIT is called, but that will make performance suck.
	 */

	ON_DEBUG (atomic_dec (& flush_cnt));

	return ret;
}

/********************************************************************************
 * REVERSE PARENT-FIRST RELOCATION POLICIES
 ********************************************************************************/

/* This implements the is-it-close-enough-to-its-preceder? test for relocation in the
 * reverse parent-first relocate context.  Here all we know is the preceder and the block
 * number.  Since we are going in reverse, the preceder may still be relocated as well, so
 * we can't ask the block allocator "is there a closer block available to relocate?" here.
 * In the _forward_ parent-first relocate context (not here) we actually call the block
 * allocator to try and find a closer location. */
static int flush_reverse_relocate_if_close_enough (const reiser4_block_nr *pblk,
						   const reiser4_block_nr *nblk)
{
	reiser4_block_nr dist;

	assert ("jmacd-7710", *pblk != 0 && *nblk != 0);
	assert ("jmacd-7711", ! blocknr_is_fake (pblk));
	assert ("jmacd-7712", ! blocknr_is_fake (nblk));

	/* Distance is the absolute value. */
	dist = (*pblk > *nblk) ? (*pblk - *nblk) : (*nblk - *pblk);

	/* If the block is less than FLUSH_RELOCATE_DISTANCE blocks away from its preceder
	 * block, do not relocate. */
	if (dist <= flush_get_params()->relocate_distance) {
		return 0;
	}

	return 1;
}

/* This function is a predicate that tests for relocation.  Always called in the
 * reverse-parent-first context, when we are asking whether the current node should be
 * relocated in order to expand the flush by dirtying the parent level (and thus
 * proceeding to flush that level).  When traversing in the forward parent-first direction
 * (not here), relocation decisions are handled in two places: flush_allocate_znode() and
 * extent_needs_allocation(). */
static int flush_reverse_relocate_test (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	reiser4_block_nr pblk = 0;
	reiser4_block_nr nblk = 0;

	assert ("jmacd-8989", ! jnode_is_root (node));

	/* New nodes are treated as if they are being relocated. */
	if (jnode_created (node) || (pos->leaf_relocate && jnode_get_level (node) == LEAF_LEVEL)) {
		return 1;
	}

	/* Find the preceder.  FIXME(B): When the child is an unformatted, previously
	 * existing node, the coord may be leftmost even though the child is not the
	 * parent-first preceder of the parent.  If the first dirty node appears somewhere
	 * in the middle of the first extent unit, this preceder calculation is wrong.
	 * Needs more logic in here. */
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

	return flush_reverse_relocate_if_close_enough (& pblk, & nblk);
}

/* This function calls flush_reverse_relocate_test to make a reverse-parent-first
 * relocation decision and then, if yes, it marks the parent dirty. */
static int flush_reverse_relocate_check_dirty_parent (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	int ret;

	if (! znode_check_dirty (parent_coord->node)) {

		if ((ret = flush_reverse_relocate_test (node, parent_coord, pos)) < 0) {
			return ret;
		}

		if (ret == 1) {
			assert ("jmacd-18923", znode_is_write_locked (parent_coord->node));
			znode_set_dirty (parent_coord->node);
		}
	}

	return 0;
}

/* This function is called when flush_forward_squalloc reaches the end of a twig node.  At
 * this point the leftmost child of the right twig may be considered for relocation.  This
 * is another case of the reverse parent-first context relocate test (where we are testing
 * the child against its parent which precedes it in parent-first order).  The right twig
 * may already be dirty, in which case this step is unnecessary--we will check the child
 * for relocation in forward parent-first context when we get there.  If neither the right
 * twig or its leftmost child are dirty then we stop flushing here.  But if the right twig
 * is clean, we need to check its leftmost child for dirtiness and relocation, possibly
 * dirtying the right twig so we can continue the flush. */
static int flush_reverse_relocate_end_of_twig (flush_position *pos)
{
	int ret;
	jnode *child = NULL;
	lock_handle right_lock;
	load_count right_load;
	coord_t right_coord;

	/* FIXME_NFQUCMPD: This is NOT the place to check for an end-of-twig condition,
	 * which we need to do to stop after a twig, because this function is not always
	 * called at the end of a twig, it is only called when the last item in the twig
	 * is an extent (because we don't know the leaf-level's right neighbor). */

	init_lh (& right_lock);
	init_load_count (& right_load);

	/* Not using get_utmost_if_dirty because then we would not discover
	 * a dirty unformatted leftmost child of a clean twig. */
	if ((ret = reiser4_get_right_neighbor (& right_lock, pos->parent_lock.node, ZNODE_WRITE_LOCK, 0))) {
		/* If -ENAVAIL,-ENOENT,-EDEADLK,-EINVAL we are finished at the end-of-twig. */
		if (ret == -ENAVAIL || ret == -ENOENT || ret == -EDEADLK || ret == -EINVAL) {

			/* Now finished with twig node. */
			trace_on (TRACE_FLUSH_VERB, "end_of_twig: STOP (end of twig, no right): %s\n", flush_pos_tostring (pos));
			ret = flush_pos_stop (pos);
		}
		goto exit;
	}

	trace_on (TRACE_FLUSH_VERB, "end_of_twig: right node %s\n", flush_znode_tostring (right_lock.node));

	/* If the right twig is dirty then we don't have to check the child. */
	if (znode_check_dirty (right_lock.node)) {
		ret = 0;
		goto exit;
	}

	if ((ret = incr_load_count_znode (& right_load, right_lock.node))) {
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
		ret = flush_pos_stop (pos);
		goto exit;
	}

	/* Now see if the child should be relocated. */
	if ((ret = flush_reverse_relocate_check_dirty_parent (child, & right_coord, pos))) {
		goto exit;
	}

	/* All we are interested in here is possibly dirtying the right twig.  The child
	 * will be allocated after its ancestors are processed by the next
	 * flush_squalloc_changed_ancestors. */

 exit:
	if (child != NULL) { jput (child); }
	done_lh (& right_lock);
	done_load_count (& right_load);
	return ret;
}

/********************************************************************************
 * INITIAL ALLOCATE ANCESTORS STEP (REVERSE PARENT-FIRST ALLOCATION BEFORE FORWARD
 * PARENT-FIRST LOOP BEGINS)
 ********************************************************************************/

/* This step occurs after the left- and right-scans are completed, before starting the
 * forward parent-first traversal.  Here we attempt to allocate ancestors of the starting
 * flush point, which means continuing in the reverse parent-first direction to the
 * parent, grandparent, and so on (as long as the child is a leftmost child).  This
 * routine calls a recursive process, flush_alloc_one_ancestor, which does the real work,
 * except there is special-case handling here for the first ancestor, which may be a twig.
 * At each level (here and flush_alloc_one_ancestor), we check for relocation and then, if
 * the child is a leftmost child, repeat at the next level.  On the way back down (the
 * recursion), we allocate the ancestors in parent-first order.
 */
static int flush_alloc_ancestors (flush_position *pos)
{
	int ret = 0;
	lock_handle plock;
	load_count pload;
	coord_t pcoord;

	trace_on (TRACE_FLUSH_VERB, "flush alloc ancestors: %s\n", flush_pos_tostring (pos));

	/* FIXME(D): This check has no atomicity--the node is not spinlocked--so what good
	 * is it?  It needs to be moved into some kind of spinlock protection, probably
	 * flush_reverse_relocate_check_dirty_parent or flush_reverse_relocate_test,
	 * definetly flush_allocate_znode. */
	if (jnode_check_flushprepped (pos->point)) {
		trace_on (TRACE_FLUSH_VERB, "flush concurrency: %s already allocated\n", flush_pos_tostring (pos));
		return 0;
	}

	coord_init_invalid (& pcoord, NULL);
	init_lh (& plock);
	init_load_count (& pload);

	if (flush_pos_on_twig_level (pos) || ! znode_is_root (JZNODE (pos->point))) {
		/* Lock the parent (it may already be locked, thus the special case). */
		if ((ret = flush_pos_lock_parent (pos, & pcoord, & plock, & pload, ZNODE_WRITE_LOCK))) {
			goto exit;
		}

		/* The parent may not be dirty, in which case we should decide whether to
		 * relocate the child now. */
		if ((ret = flush_reverse_relocate_check_dirty_parent (pos->point, & pcoord, pos))) {
			goto exit;
		}

		/* FIXME_NFQUCMPD: We only need to allocate the twig (if child is
		 * leftmost) and the leaf/child, so recursion is not needed.  Levels above
		 * the twig will be allocated for write-optimization before the
		 * transaction commits.  */
		
		/* Do the recursive step, allocating zero or more of our ancestors. */
		ret = flush_alloc_one_ancestor (& pcoord, pos);
	}

	/* Finally, allocate the current flush point if it is formatted.  This leaves us
	 * with the current point allocated, ready to call the flush_forward_squalloc
	 * loop. */
	if (ret == 0 && ! flush_pos_on_twig_level (pos)) {
		ret = flush_allocate_znode (JZNODE (pos->point), & pcoord, pos);
	}

 exit:
	done_load_count (& pload);
	done_lh (& plock);
	return ret;
}

/* This is the recursive step described in flush_alloc_ancestors, above.  Ignoring the
 * call to flush_set_preceder, which is the next function described, this checks if the
 * child is a leftmost child and returns if it is not.  If the child is a leftmost child
 * it checks for relocation, possibly dirtying the parent.  Then it performs the recursive
 * step. */
static int flush_alloc_one_ancestor (coord_t *coord, flush_position *pos)
{
	int ret = 0;
	lock_handle alock;
	load_count aload;
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
	if (znode_check_flushprepped (coord->node) || ! coord_is_leftmost_unit (coord)) {
		return 0;
	}

	init_lh (& alock);
	init_load_count (& aload);
	coord_init_invalid (& acoord, NULL);

	/* Only ascend to the next level if it is a leftmost child, but write-lock the
	 * parent in case we will relocate the child. */
	if (! znode_is_root (coord->node)) {

		if ((ret = jnode_lock_parent_coord (ZJNODE (coord->node), & acoord, & alock, & aload, ZNODE_WRITE_LOCK))) {
			/* FIXME(C): check EINVAL, EDEADLK */
			goto exit;
		}

		if ((ret = flush_reverse_relocate_check_dirty_parent (ZJNODE (coord->node), & acoord, pos))) {
			goto exit;
		}

		/* Recursive call. */
		if (! znode_check_flushprepped (acoord.node) &&
		    (ret = flush_alloc_one_ancestor (& acoord, pos))) {
			goto exit;
		}
	}

	/* Note: we call allocate with the parent write-locked (except at the root) in
	 * case we relocate the child, in which case it will modify the parent during this
	 * call. */
	ret = flush_allocate_znode (coord->node, & acoord, pos);

 exit:
	done_load_count (& aload);
	done_lh (& alock);
	return ret;
}

/* During the reverse parent-first flush_alloc_ancestors process described above there is
 * a call to this function at the twig level.  During flush_alloc_ancestors we may ask:
 * should this node be relocated (in reverse parent-first context)?  We repeat this
 * process as long as the child is the leftmost child, eventually reaching an ancestor of
 * the flush point that is not a leftmost child.  The preceder of that ancestors, which is
 * not a leftmost child, is actually on the leaf level.  The preceder of that block is the
 * left-neighbor of the flush point.  The preceder of that block is the rightmost child of
 * the twig on the left.  So, when flush_alloc_ancestors passes upward through the twig
 * level, it stops momentarily to remember the block of the rightmost child of the twig on
 * the left and sets it to the flush_position's preceder_hint.
 *
 * There is one other place where we may set the flush_position's preceder hint, which is
 * during scan-left.
 */
static int flush_set_preceder (const coord_t *coord_in, flush_position *pos)
{
	int ret;
	coord_t coord;
	lock_handle left_lock;

	coord_dup (& coord, coord_in);

	init_lh (& left_lock);

	/* FIXME(B): Same FIXME as in "Find the preceder" in flush_reverse_relocate_test.
	 * coord_is_leftmost_unit is not the right test if the unformatted child is in the
	 * middle of the first extent unit. */
	if (! coord_is_leftmost_unit (& coord)) {
		coord_prev_unit (& coord);
	} else {
		if ((ret = reiser4_get_left_neighbor (& left_lock, coord.node, ZNODE_READ_LOCK, GN_SAME_ATOM))) {
			/* If we fail for any reason it doesn't matter because the
			 * preceder is only a hint.  We are low-priority at this point, so
			 * this must be the case. */
			if (ret == -EAGAIN || ret == -ENAVAIL || ret == -ENOENT || ret == -EINVAL || ret == -EDEADLK) { ret = 0; }
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

/********************************************************************************
 * MAIN SQUEEZE AND ALLOCATE LOOP (THREE BIG FUNCTIONS)
 ********************************************************************************/

/* This procedure implements the outer loop of the flush algorithm.  To put this in
 * context, here is the general list of steps taken by the flush routine as a whole:
 *
 * 1. Scan-left
 * 2. Scan-right (maybe)
 * 3. Allocate initial ancestors
 * 4. <handle extents>
 * 5. <squeeze and allocate changed ancestors of the next position to-the-right,
 *     then update position to-the-right>
 * 6. <repeat from #4 until flush is stopped>
 *
 * This procedure implements the loop in steps 4 through 6 in the above listing.
 *
 * Step 4: if the current flush position is an extent item (position on the twig level),
 * it allocates the extent (allocate_extent_item_in_place) then shifts to the next
 * coordinate.  If the next coordinate's leftmost child needs flushprep, we will continue.
 * If the next coordinate is an internal item, we descend back to the leaf level,
 * otherwise we repeat a step #4 (labeled ALLOC_EXTENTS below).  If the "next coordinate"
 * brings us past the end of the twig level, then we call
 * flush_reverse_relocate_end_of_twig to possibly dirty the next (right) twig, prior to
 * step #5 which moves to the right.
 *
 * Step 5: calls flush_squalloc_changed_ancestors, which initiates a recursive call up the
 * tree to allocate any ancestors of the next-right flush position that are not also
 * ancestors of the current position.  Those ancestors (in top-down order) are the next in
 * parent-first order.  We squeeze adjacent nodes on the way up until the right node and
 * current node share the same parent, then allocate on the way back down.  Finally, this
 * step sets the flush position to the next-right node.  Then repeat steps 4 and 5.
 */
static int flush_forward_squalloc (flush_position *pos)
{
	int ret = 0;

	/* If there is a race and the current node is already prepped, why continue? */
	if (jnode_check_flushprepped (pos->point)) {
		return 0;
	}

 ALLOC_EXTENTS:
	/* Step 4: Allocate the current extent (if current position is an extent). */
	if (flush_pos_valid (pos) && flush_pos_on_twig_level (pos)) {

		assert ("jmacd-8712", item_is_extent (& pos->parent_coord));

		trace_on (TRACE_FLUSH_VERB, "allocate_extent_in_place: %s\n", flush_pos_tostring (pos));

		/* This allocates extents up to the end of the current extent item and
		 * returns pos->parent_coord set to the next item.  FIXME: We may wish to
		 * do partial extent allocation at some point.  If we have all of memory
		 * holding dirty pages for one unallocated extent, this
		 * allocate_extent_item_in_place call will try to allocate everything.  If
		 * the disk is fragmented and we are low on memory, this may be a bad
		 * idea.  Perhaps extent allocation should be aware of this... */
		if ((ret = allocate_extent_item_in_place (& pos->parent_coord, pos))) {
			goto exit;
		}

		coord_next_unit (& pos->parent_coord);

		/* If we are not past the end of the node we consider the next
		 * position--first to see if it needs flushprep, then to see if it is
		 * another extent or an internal item. */
		if (! coord_is_after_rightmost (& pos->parent_coord)) {

			jnode *child;
			int keep_going;

			/* If child is not flushprepped then repeat, otherwise stop here. */
			if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child)) || (child == NULL)) {
				goto exit;
			}

			keep_going = ! jnode_check_flushprepped (child);

			jput (child);

			if (keep_going) {

 				trace_on (TRACE_FLUSH_VERB, "sq_r unformatted_right_is_dirty: %s type %s\n",
					  flush_pos_tostring (pos), item_is_extent (& pos->parent_coord) ? "extent" : "internal");

				/* If the flush position is not an extent item (at this
				 * twig), we should descend to the formatted child. */
				if (! item_is_extent (& pos->parent_coord) && (ret = flush_pos_to_child_and_alloc (pos))) {
					goto exit;
				}

				trace_on (TRACE_FLUSH_VERB, "sq_r unformatted_goto_step2: %s\n", flush_pos_tostring (pos));
				goto ALLOC_EXTENTS;
			} else {
				/* Finished. */
				ret = 0;
				goto exit;
			}
		}

		/* We are about to try to allocate the right twig by calling
		 * flush_squalloc_changed_ancestors in the flush_pos_on_twig_level state.
		 * However, the twig may need to be dirtied first if its left-child will
		 * be relocated. */
		if ((ret = flush_reverse_relocate_end_of_twig (pos))) {
			goto exit;
		}
	}

	if (flush_pos_valid (pos)) {
		/* Step 5: Formatted and unformatted cases.  Squeeze upward, allocate
		 * downward, for any ancestors that are not in common between current
		 * position and its right neighbor. */
		if ((ret = flush_squalloc_changed_ancestors (pos))) {
			goto exit;
		}
	}

	if (flush_pos_valid (pos)) {
		/* Repeat. */
		trace_on (TRACE_FLUSH_VERB, "sq_r repeat: %s\n", flush_pos_tostring (pos));
		goto ALLOC_EXTENTS;
	}

 exit:
	return ret;
}

/* This implements "step 5" described in flush_forward_squalloc.  This is the entry point
 * for the recursive function flush_squalloc_one_changed_ancestor, described above.  This
 * function mainly deals with special cases related to the twig-level and extents, then
 * initiates the upward-recursive call, then establishes the next flush position after the
 * recursive call is complete.  */
static int flush_squalloc_changed_ancestors (flush_position *pos)
{
	int ret;
	int on_twig_level;
	lock_handle right_lock;
	znode *node;

	/* The node used for the recursive call is either a twig (if position is an
	 * extent) or the current leaf. */
	if ((on_twig_level = flush_pos_on_twig_level (pos))) {
		/* If we are checking for changed ancestors, we must have reached the
		 * end-of-twig situation described in flush_forward_squalloc.  Otherwise
		 * we would have repeated extent allocation or descended to a formatted
		 * leaf. */
		assert ("jmacd-9812", coord_is_after_rightmost (& pos->parent_coord));

		node = pos->parent_lock.node;
	} else {
		node = JZNODE (pos->point);
	}

	trace_on (TRACE_FLUSH_VERB, "sq_r changed ancestors before: %s\n", flush_pos_tostring (pos));

	assert ("jmacd-9814", znode_is_write_locked (node));

	init_lh (& right_lock);

	/* Recursive step: the on_twig_level argument passed in as the call_depth requires
	 * a note--its quite subtle.  If we are on the twig level,
	 * flush_squalloc_one_changed_ancestor behaves as if it is already at a recursion
	 * depth of one, otherwise we start at zero.  Read the comments there for more
	 * explanation. */
	if ((ret = flush_squalloc_one_changed_ancestor (node, /*call_depth*/on_twig_level, pos))) {
		warning ("jmacd-61432", "sq1_ca failed: %d", ret);
		goto exit;
	}

	if (! flush_pos_valid (pos)) {
		goto exit;
	}

	trace_on (TRACE_FLUSH_VERB, "sq_rca after sq_ca recursion: %s\n", flush_pos_tostring (pos));

	/* In the unformatted case, we may have shifted new contents into the current
	 * twig.  In that case, we should return to the main loop for extent allocation.
	 * If the first-shifted item is an internal item, then descend back to the leaf
	 * level. */
	if (on_twig_level && ! coord_is_after_rightmost (& pos->parent_coord)) {

		trace_on (TRACE_FLUSH_VERB, "sq_rca unformatted after: %s\n", flush_pos_tostring (pos));

		/* Then, if we are positioned at a formatted item, allocate & descend. */
		if (item_is_internal (& pos->parent_coord)) {
			ret = flush_pos_to_child_and_alloc (pos);
		}

		/* That's all. */
		goto exit;
	}

	/* Now advance to the right neighbor.  We may repeat at this point when handling the */
 repeat: assert ("jmacd-1092", znode_is_write_locked (node));

	if ((ret = znode_get_utmost_if_dirty (node, & right_lock, RIGHT_SIDE, ZNODE_WRITE_LOCK))) {

		jnode *child;
		int keep_going;

		/* If we get ENAVAIL it means the right neighbor is not dirty.  If we are
		 * at the leaf level there may be an unformatted node to the right, so we
		 * only break here if there is an error or ENAVAIL not on the leaf
		 * level. */
		if (ret != -ENAVAIL || znode_get_level (node) != LEAF_LEVEL) {
			if (ret == -ENAVAIL) {
				trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (ENAVAIL, ancestors allocated): %s\n", flush_pos_tostring (pos));
				ret = flush_pos_stop (pos);
			} else {
				warning ("jmacd-61433", "znode_get_if_dirty failed: %d", ret);
			}
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "sq_rca no right at leaf, to parent: %s\n", flush_pos_tostring (pos));

		/* We are leaving node now. */
		/*JF_CLR (node, JNODE_FLUSH_BUSY)*/

		/* We are on the leaf level and we got ENAVAIL to the right, but we may
		 * have a unformatted node to the right, so go up to the twig level. */
		if ((ret = flush_pos_to_parent (pos))) {
			warning ("jmacd-61435", "flush_pos_to_parent failed: %d", ret);
			goto exit;
		}

		/* Now on the twig level, update local variables. */
		assert ("jmacd-9259", flush_pos_on_twig_level (pos));
		assert ("jmacd-9260", ! coord_is_after_rightmost (& pos->parent_coord));
		on_twig_level = 1;
		node = pos->parent_lock.node;

		/* We are interested in the next item. */
		coord_next_item (& pos->parent_coord);

		/* ... but we may be at the end of a twig. */
		if (coord_is_after_rightmost (& pos->parent_coord)) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca to right twig: %s\n", flush_pos_tostring (pos));

			/* Otherwise, we may want to dirty the right twig if its
			 * leftmost child (an extent) is dirty. */
			if ((ret = flush_reverse_relocate_end_of_twig (pos))) {
				goto exit;
			}

			/* If end_of_twig stops the flush, we are finished. */
			if (! flush_pos_valid (pos)) {
				goto exit;
			}

			/* Now repeat the get_right_if_dirty step locally. */
			goto repeat;
		}

		/* If positioned over a formatted node, then the preceding
		 * get_right_if_dirty would have succeeded if the formatted neighbor was
		 * in memory.  Therefore it must not be, so we are finished. */
		if (item_is_internal (& pos->parent_coord)) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca stop at twig, next is internal: %s\n", flush_pos_tostring (pos));
			ret = flush_pos_stop (pos);
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "sq_rca check right twig child: %s\n", flush_pos_tostring (pos));

		/* Finally, we must now be positioned over an extent, but does it need flushprep? */
		if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child))) {
			goto exit;
		}

		if (child == NULL) {
			ret = flush_pos_stop (pos);
			goto exit;
		}

		keep_going = ! jnode_check_flushprepped (child);

		jput (child);

		/* If it doesn't need flushprep, stop now. */
		if (! keep_going) {
			trace_on (TRACE_FLUSH_VERB, "sq_rca stop at twig, child already flushprepped: %s\n", flush_pos_tostring (pos));
			ret = flush_pos_stop (pos);
			goto exit;
		}

		/* In this case, continue the outer flush_forward_squalloc loop. */
		ret = 0;
		goto exit;
	}

	trace_on (TRACE_FLUSH_VERB, "sq_rca ready to move right %s\n", flush_znode_tostring (right_lock.node));

	/* We have a new right and it should have been flushprepped by the call to
	 * flush_squalloc_one_changed_ancestor.  However, a concurrent thread could
	 * possibly insert a new node, so just stop if ! flushprepped. */
	if (! jnode_check_flushprepped (ZJNODE (right_lock.node))) {
		trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (right not allocated): %s\n", flush_pos_tostring (pos));
		ret = flush_pos_stop (pos);
		goto exit;
	}

	/* Note: The above check and comment are correct, I think, but there is still a
	 * subtle issue worth explaining.  If the node is flushprepped already, how do we
	 * know that it was our thread and not some concurrent thread that did the work?
	 * The answer is: squalloc_one_changed_ancestor stops the flush if it finds a node
	 * to the right that is already flushprepped. */

	/* Now to finish--update the flush position with the new coordinate/point. */
	if (on_twig_level) {
		done_load_count (& pos->parent_load);
		done_lh (& pos->parent_lock);
		move_lh (& pos->parent_lock, & right_lock);
		if ((ret = incr_load_count_znode (& pos->parent_load, pos->parent_lock.node))) {
			warning ("jmacd-61438", "zload failed: %d", ret);
			goto exit;
		}
		coord_init_first_unit (& pos->parent_coord, pos->parent_lock.node);

		/* If the first entry of the new twig is an internal item, descend to the
		 * leaf level. */
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

/* This implements the recursive part of step 5 described in flush_forward_squalloc,
 * including squeezing on the way up, as long as the node and its right neighbor have
 * different parents, then allocating the right side on the way back down.  This is a
 * complicated beast.  The call_depth paramter tells us how high in the recursion we are.
 * Note that the greater (recursive) call_depth, the higher tree level.  When the comments
 * below discuss "lower-level", it means lower smaller call_depth as well.  */
static int flush_squalloc_one_changed_ancestor (znode *node, int call_depth, flush_position *pos)
{
	int ret;
	int same_parents;
	int shifted_nodes_below;
	lock_handle right_lock;
	lock_handle parent_lock;
	load_count right_load;
	load_count parent_load;
	load_count node_load;
	coord_t at_right, right_parent_coord;

	init_lh (& right_lock);
	init_lh (& parent_lock);
	init_load_count (& right_load);
	init_load_count (& parent_load);
	init_load_count (& node_load);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] %s\n", call_depth, flush_znode_tostring (node));

	if ((ret = incr_load_count_znode (& node_load, node))) {
		warning ("jmacd-61424", "zload failed: %d", ret);
		goto exit;
	}

	/* FIXME_NFQUCMPD: We want to squeeze and allocate one twig at a time, thus: We do
	 * not need to allocate in this routine at all.  The calling function,
	 * flush_squalloc_changed_ancestors, can handle allocating the leaf level (rename
	 * this to flush_squeeze_one_changed_ancestor?).  Allocating the twig level is
	 * already done in flush_alloc_ancestors.  But this is where we detect that we
	 * have reached the end of a twig--see below.
	 */

 RIGHT_AGAIN:
	/* First get the right neighbor. */
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

	/* If the right node is already flushprepped then we should stop now.  Note: there
	 * may be refinements here.  There is a region to the right that was already
	 * squeezed, but it could be that squeezing it with this node (and then squeezing
	 * to the right) will reduce by a node. */
	if (znode_check_flushprepped (right_lock.node)) {
		trace_on (TRACE_FLUSH_VERB, "sq1_ca: STOP (right already prepped): %s\n", flush_pos_tostring (pos));
		ret = flush_pos_stop (pos);
		goto exit;
	}

	if ((ret = incr_load_count_znode (& right_load, right_lock.node))) {
		warning ("jmacd-61426", "zload failed: %d", ret);
		goto exit;
	}

	/* Set a coordinate to the after last item in node before we squeeze it.  After
	 * squeezing, we will know whether any items were shifted by checking if this
	 * coordinate is still after the last item. */
	coord_init_after_last_item (& at_right, node);

	assert ("jmacd-7866", ! node_is_empty (right_lock.node));

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before right neighbor %s\n", call_depth, flush_znode_tostring (right_lock.node));

	/* We found the right znode (and locked it), now squeeze from right into current
	 * node position. */
	if ((ret = squalloc_right_neighbor (node, right_lock.node, pos)) < 0) {
		warning ("jmacd-61427", "squalloc_right_neighbor failed: %d", ret);
		goto exit;
	}

	/* Now check whether there are shifted nodes below this level using the coordinate
	 * set prior to squeezing.  */
	shifted_nodes_below = ! coord_is_after_rightmost (& at_right);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after right neighbor %s: shifted_nodes_below = %u\n",
		  call_depth, flush_znode_tostring (right_lock.node), shifted_nodes_below);

	/* In general, shifted_nodes_below indicates whether we should stop the upward
	 * recursion now because, after shifting, this node is the common parent of the
	 * level below and the level below's right neighbor.  However, there is one case
	 * where we unset shifted_nodes_below, that is if all the shifted nodes were
	 * unformatted nodes that were allocated during shifting by
	 * extent_copy_and_allocated.
	 *
	 * We are only interested if this node is the current parent_coord->node. Here,
	 * check if everything that was shifted is an extent item.  If we reach a
	 * non-extent item then leave the parent_coord at that position--its our next
	 * flush position.  Otherwise, unset shifted_nodes_below. */
	if (shifted_nodes_below && node == pos->parent_coord.node) {

		assert ("jmacd-1732", ! coord_is_after_rightmost (& pos->parent_coord));

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before (shifted & unformatted): %s\n", call_depth, flush_pos_tostring (pos));
		/*trace_if (TRACE_FLUSH_VERB, print_coord ("present coord", & pos->parent_coord, 0));*/

		coord_next_unit (& pos->parent_coord);

		/*trace_if (TRACE_FLUSH_VERB, print_coord ("after next_unit", & pos->parent_coord, 0));*/
		assert ("jmacd-1731", coord_is_existing_unit (& pos->parent_coord));

		while (coord_is_existing_unit (& pos->parent_coord) &&
		       item_is_extent (& pos->parent_coord)) {
			assert ("jmacd-8612", ! extent_is_unallocated (& pos->parent_coord));
			coord_next_unit (& pos->parent_coord);
		}

		if (! coord_is_existing_unit (& pos->parent_coord)) {
			/* All the shifted nodes were allocated -- so unset
			 * shifted_nodes_below. */
			shifted_nodes_below = 0;
		}

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after (shifted & unformatted): shifted_nodes_below = %u: %s\n",
			  call_depth, shifted_nodes_below, flush_pos_tostring (pos));
	}

	/* The next two if-stmts depend on call_depth, which is initially set to
	 * on_twig_level because when allocating for unformatted nodes the first call is
	 * effectively at level 1: */

	/* We are concerned with shifted_nodes_below because a lower tree-level in the
	 * recursion called us thinking that its node and right neighbor have different
	 * parents.  Now, if we shifted anything it means the lower-level's "same parent"
	 * test is true now, meaning we should stop the recursion.
	 *
	 * So, if the test for (shifted_nodes_below == 0 || call_depth == 0) is true, it
	 * indicates that there is no reason to stop the recursion.  If there is no reason
	 * to stop the recursion and the right node was completely emptied, then we should
	 * squeeze the next right node (after the empty right node is removed from the
	 * tree, i.e., after we release right_lock). */
	if ((shifted_nodes_below == 0 || call_depth == 0) && node_is_empty (right_lock.node)) {
		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] right again: %s\n", call_depth, flush_pos_tostring (pos));
		done_load_count (& right_load);
		done_lh (& right_lock);

		/* We have released a node. */
		pos->prep_or_free_cnt += 1;

		goto RIGHT_AGAIN;
	}

	/* If anything was shifted and we are not at zero call depth, it indicates that
	 * the current node is now the common parent of the level below.  If that is true,
	 * we should continue allocating below us, stop the recursion and return here. */
	if (shifted_nodes_below && call_depth > 0) {
		ret = 0;
		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] shifted & not leaf: %s\n", call_depth, flush_pos_tostring (pos));
		goto exit;
	}

	assert ("jmacd-18231", ! node_is_empty (right_lock.node));

	/* We still have the right node locked and nothing more could be shifted out of
	 * the right node.  So as far as this invocation of flush is concerned, we have
	 * decided what the node's right neighbor is.  At this point we check whether the
	 * node and its right neighbor have the same parent.  If they do NOT have the same
	 * parent then we recurse upwards, squeezing the level above and allocating any of
	 * the right's ancestors before we allocate the right node below, after the
	 * recursive call (on the way back down). */
	if (! (same_parents = znode_same_parents (node, right_lock.node))) {

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before (not same parents): %s\n", call_depth, flush_pos_tostring (pos));

		if ((ret = reiser4_get_parent (& parent_lock, node, ZNODE_WRITE_LOCK, 0))) {
			/* FIXME(C): check ENAVAIL, EINVAL, EDEADLK */
			warning ("jmacd-61428", "reiser4_get_parent failed: %d", ret);
			goto exit;
		}

		if ((ret = flush_squalloc_one_changed_ancestor (parent_lock.node, call_depth + 1, pos))) {
			warning ("jmacd-61429", "sq1_ca recursion failed: %d", ret);
			goto exit;
		}

		done_lh (& parent_lock);

		/* FIXME_NFQUCMPD: When we get here and (node->level == TWIG_LEVEL), we've
		 * finished allocating a twig and squeezing all of its ancestors.
		 * right_node is the next twig to squeeze.  Perhaps it should be saved
		 * into flush_position or the flush_handle right now (using a seal). */

		trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] after (not same parents): %s\n", call_depth, flush_pos_tostring (pos));
	}

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] ready to enqueue node %s\n", call_depth, flush_znode_tostring (node));

	/* Now finished with node. */
	/* JF_CLR (node, JNODE_FLUSH_BUSY) */

	/* No reason to hold onto the node data now, can release it early. */
	done_load_count (& node_load);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] ready to allocate right %s\n", call_depth, flush_znode_tostring (right_lock.node));

	/* Allocate the right node if it was not already allocated.  We already checked if
	 * the right node was flushprepped above, before squeezing, so why check it now?
	 * Why not?  The call to flush_allocate_znode acquires a spinlock */
	if (! znode_check_flushprepped (right_lock.node)) {
		if ((ret = jnode_lock_parent_coord (ZJNODE (right_lock.node), & right_parent_coord, & parent_lock, & parent_load, ZNODE_WRITE_LOCK))) {
			/* FIXME(C): check EINVAL, EDEADLK */
			warning ("jmacd-61430", "jnode_lock_parent_coord failed: %d", ret);
			goto exit;
		}

		if ((ret = flush_allocate_znode (right_lock.node, & right_parent_coord, pos))) {
			warning ("jmacd-61431", "flush_allocate_znode failed: %d", ret);
			goto exit;
		}
	}

	/* That's it!  We made it through the most complex routine in the entire flush
	 * algorithm.  If you were reading these comments and understood them,
	 * congratulate yourself. :) */
	ret = 0;
 exit:
	done_load_count (& node_load);
	done_load_count (& right_load);
	done_load_count (& parent_load);
	done_lh (& right_lock);
	done_lh (& parent_lock);
	return ret;
}

/********************************************************************************
 * SQUEEZE CODE
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
	int old_items;
	int old_free_space;

	assert ("nikita-2246", znode_get_level (left) == znode_get_level (right));
	init_carry_pool (& pool);
	init_carry_level (& todo, & pool);

	old_items = node_num_items (left);
	old_free_space = znode_free_space (left);
	ret = shift_everything_left (right, left, & todo);

	/*
	 * FIXME-VS: urgently added squeeze statistics
	 */
	if (znode_get_level (left) == LEAF_LEVEL) {
		reiser4_stat_flush_add (squeezed_leaves);
		reiser4_stat_flush_add_few (squeezed_leaf_items, node_num_items (left) - old_items);
		reiser4_stat_flush_add_few (squeezed_leaf_bytes, old_free_space - znode_free_space (left));
	}


	UNDER_SPIN_VOID (dk, current_tree, update_znode_dkeys (left, right));

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
	assert ("nikita-2435", znode_is_write_locked (left));
	assert ("nikita-2436", znode_is_write_locked (right));

	if (REISER4_DEBUG) {
		spin_lock_tree (current_tree);
		assert ("nikita-2434", left->right == right);
		spin_unlock_tree (current_tree);
	}

	coord_init_first_unit (&coord, right);

	if (REISER4_DEBUG && !node_is_empty (left)) {
		coord_t last;
		reiser4_key right_key;
		reiser4_key left_key;

		coord_init_last_unit (&last, left);

		assert ("nikita-2463", 
			keyle (item_key_by_coord (&last, &left_key), 
			       item_key_by_coord (&coord, &right_key)));
	}

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
		UNDER_SPIN_VOID (dk, current_tree,
				 update_znode_dkeys (left, right));

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
int jnode_check_flushprepped (jnode *node)
{
	/* It must be clean or relocated or wandered.  New allocations are set to relocate. */
	assert ("jmacd-71275", spin_jnode_is_not_locked (node));
	return UNDER_SPIN (jnode, node, jnode_is_flushprepped (node));
}

int znode_check_flushprepped (znode *node)
{
	return jnode_check_flushprepped (ZJNODE (node));
}

/* Audited by: umka (2002.06.11) */
void jnode_set_block( jnode *node /* jnode to update */,
		      const reiser4_block_nr *blocknr /* new block nr */ )
{
	assert( "nikita-2020", node  != NULL );
	assert( "umka-055", blocknr != NULL );
	node -> blocknr = *blocknr;
}

/* Make the final relocate/wander decision during forward parent-first squalloc for a
 * znode.  For unformatted nodes this is done in plugin/item/extent.c:extent_needs_allocation(). */
static int flush_allocate_znode (znode *node, coord_t *parent_coord, flush_position *pos)
{
	int ret;

	/* FIXME(D): We have the node write-locked and should have checked for ! 
	 * allocated() somewhere before reaching this point, but there can be a race, so
	 * this assertion is bogus. */
	assert ("jmacd-7987", ! jnode_check_flushprepped (ZJNODE (node)));
	assert ("jmacd-7988", znode_is_write_locked (node));
	assert ("jmacd-7989", coord_is_invalid (parent_coord) || znode_is_write_locked (parent_coord->node));

	if (znode_created (node) || znode_is_root (node)) {
		/* No need to decide with new nodes, they are treated the same as
		 * relocate. If the root node is dirty, relocate. */
		goto best_reloc;

	} else if (pos->leaf_relocate != 0 && znode_get_level (node) == LEAF_LEVEL) {

		/* We have enough nodes to relocate no matter what. */
		goto best_reloc;
	} else if (pos->preceder.blk == 0) {

		/* If we don't know the preceder, leave it where it is. */
		jnode_set_wander (ZJNODE (node));
	} else {
		/* Make a decision based on block distance. */
		reiser4_block_nr dist;
		reiser4_block_nr nblk = *znode_get_block (node);

		assert ("jmacd-6172", ! blocknr_is_fake (& nblk));
		assert ("jmacd-6173", ! blocknr_is_fake (& pos->preceder.blk));
		assert ("jmacd-6174", pos->preceder.blk != 0);

		if (pos->preceder.blk == nblk - 1) {
			/* Ideal. */
			jnode_set_wander (ZJNODE (node));
		} else {

			dist = (nblk < pos->preceder.blk) ? (pos->preceder.blk - nblk) : (nblk - pos->preceder.blk);

			/* See if we can find a closer block (forward direction only). */
			pos->preceder.max_dist = min ((reiser4_block_nr) flush_get_params()->relocate_distance, dist);
			pos->preceder.level    = znode_get_level (node);

			if ((ret = flush_allocate_znode_update (node, parent_coord, pos)) && (ret != -ENOSPC)) {
				return ret;
			}

			if (ret == 0) {
				/* Got a better allocation. */
				jnode_set_reloc (ZJNODE (node));
			} else if (dist < flush_get_params()->relocate_distance) {
				/* The present allocation is good enough. */
				jnode_set_wander (ZJNODE (node));
			} else {
				/* Otherwise, try to relocate to the best position. */
			best_reloc:
				pos->preceder.max_dist = 0;
				if ((ret = flush_allocate_znode_update (node, parent_coord, pos))) {
					return ret;
				}
				/* set JNODE_RELOC bit _after_ node gets allocated */
				jnode_set_reloc (ZJNODE (node));
			}
		}
	}

	/* This is the new preceder. */
	pos->preceder.blk = *znode_get_block (node);
	pos->alloc_cnt += 1;

	spin_lock_znode (node);

	assert ("jmacd-4277", ! blocknr_is_fake (& pos->preceder.blk));
	/*assert ("jmacd-4278", ! ZF_ISSET (node, JNODE_FLUSH_BUSY));*/

	/*ZF_SET (node, JNODE_FLUSH_BUSY);*/
	trace_on (TRACE_FLUSH, "alloc: %s\n", flush_znode_tostring (node));

	/* Queue it now, releases lock. */
	return flush_queue_jnode (ZJNODE (node), pos);
}

/* A subroutine of flush_allocate_znode, this is called first to see if there is a close
 * position to relocate to.  It may return ENOSPC if there is no close position.  If there
 * is no close position it may not relocate.  This takes care of updating the parent node
 * with the relocated block address. */
static int flush_allocate_znode_update (znode *node, coord_t *parent_coord, flush_position *pos)
{
	int ret;
	reiser4_block_nr blk;
	reiser4_block_nr len = 1;
	lock_handle fake_lock;

	pos->preceder.block_stage = BLOCK_NOT_COUNTED;

	if ((ret = reiser4_alloc_blocks (& pos->preceder, & blk, & len))) {
		return ret;
	}

	 /* FIXME: JMACD->ZAM: In the dealloc_block call, it is unclear to me whether I
	  * need to pass BLOCK_NOT_COUNTED or whether setting preceder.block_stage above
	  * is correct. */
	if (! ZF_ISSET (node, JNODE_CREATED) &&
	    (ret = reiser4_dealloc_block (znode_get_block (node), 1 /* defer */, 0 /* BLOCK_NOT_COUNTED */))) {
		return ret;
	}

	init_lh (& fake_lock);

	if (! znode_is_root (node)) {

		internal_update (parent_coord, blk);

		znode_set_dirty (parent_coord->node);

	} else {
		znode *fake = zget (current_tree, &FAKE_TREE_ADDR, NULL, 0 , GFP_KERNEL);

		if (IS_ERR (fake)) { ret = PTR_ERR(fake); goto exit; }

		/* We take a longterm lock on the fake node in order to change the root
		 * block number.  This may cause atom fusion. */
		if ((ret = longterm_lock_znode (& fake_lock, fake, ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI))) {
			/* The fake node cannot be deleted, and we must have priority
			 * here, and may not be confused with ENOSPC. */
			assert ("jmacd-74412", ret != -EINVAL && ret != -EDEADLK && ret != -ENOSPC);
			zput (fake);
			goto exit;
		}

		UNDER_SPIN_VOID (tree, current_tree,
				 current_tree->root_block = blk);

		znode_set_dirty(fake);

		zput (fake);
	}

	ret = znode_rehash (node, & blk);
 exit:
	done_lh (& fake_lock);
	return ret;
}

/* Enter a jnode into the flush queue. */
static int flush_queue_jnode (jnode *node, flush_position *pos)
{
	assert ("jmacd-65551", spin_jnode_is_locked (node));

	/* FIXME(D): See atomicity comment in flush_rewrite_jnode. */
	if (! jnode_is_dirty (node) || JF_ISSET (node, JNODE_HEARD_BANSHEE) || JF_ISSET (node, JNODE_FLUSH_QUEUED)) {
		spin_unlock_jnode (node);
		return 0;
	}

	JF_SET (node, JNODE_FLUSH_QUEUED);

	// assert ("zam-670", PageDirty(jnode_page(node)));
	assert ("jmacd-1771", jnode_is_flushprepped (node));

	{
		txn_atom * atom;

		atom = atom_get_locked_by_jnode (node);
		assert ("zam-661", atom != NULL);

		/* flush_pos is added to the atom->flushers list the first time a jnode is queued. */
		if (capture_list_empty(&pos->queue)) {
			flushers_list_push_back (&atom->flushers, pos);
			assert ("zam-664", pos->queue_num == 0);
		}

		pos->queue_num ++;
		atom->num_queued ++;

		capture_list_remove (node);
		capture_list_push_back(&pos->queue, jref (node));

		spin_unlock_atom (atom);
	}

	/*trace_if (TRACE_FLUSH, if (jnode_is_unformatted (node)) { info ("queue: %s\n", flush_jnode_tostring (node)); });*/

	spin_unlock_jnode (node);

	return 0;
}

/* Take jnode from flush queue and put it on clean_nodes list, called during
 * flush_empty_queue. */
static void flush_dequeue_jnode (flush_position * pos, jnode * node)
{
	txn_atom * atom;

	spin_lock_jnode (node);
	atom = atom_get_locked_by_jnode (node);

	assert ("zam-645", JF_ISSET (node, JNODE_FLUSH_QUEUED));

	JF_CLR (node, JNODE_FLUSH_QUEUED);
	JF_CLR (node, JNODE_DIRTY);

	capture_list_remove (node);
	capture_list_push_back (&atom->clean_nodes, node);

	pos->queue_num --;
	atom->num_queued --;

	if (capture_list_empty (&pos->queue)) {
		flushers_list_remove (pos);
		assert ("zam-663", pos->queue_num == 0);
	}

	spin_unlock_atom (atom);
	spin_unlock_jnode (node);

	jput (node);
}

/* This is called by the extent code for each jnode after allocation has been performed.
 * Contrast with thef flush_allocate_znode() routine, which does znode allocation and then
 * calls flush_queue_jnode, the unformatted allocation is handled by the extent plugin and
 * simply queued by this function. */
int flush_enqueue_unformatted (jnode *node, flush_position *pos)
{
	/* flush_queue_jnode expects the jnode to be locked. */
	spin_lock_jnode (node);
	return flush_queue_jnode (node, pos);
}

/* This is an I/O completion callback which is called after the result of a submit_bio has
 * completed.  Its task is to notify any waiters that are waiting, either for an
 * individual page or an atom (via the io_handle) which may be waiting to commit. */
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

		end_page_writeback (pg);
	}

	io_handle_end_io (bio);

	bio_put (bio);
}

/* Write some of the the flush_position->queue contents to disk.
 */
static int flush_empty_queue (flush_position *pos)
{
	int flushed = 0; /* Track number of jnodes we've flushed already */
	int ret = 0;

	jnode * node;

	trace_on (TRACE_FLUSH, "flush_empty_queue with %u queued\n", pos->queue_num);

	if (pos->queue_num == 0) {
		return 0;
	}

	/* We can safely traverse this flush queue without locking of atom and nodes
	 * because only this thread can add nodes to it and all already queued nodes are
	 * protected from moving out by JNODE_FLUSH_QUEUED bit */
	node = capture_list_front (&pos->queue);
	while (!capture_list_end (&pos->queue, node)) {
		jnode * check = node;
		struct page *cpage;

		assert ("jmacd-71235", check != NULL);
		assert ("jmacd-71236", JF_ISSET (check, JNODE_FLUSH_QUEUED));

		node = capture_list_next (node);

		/* FIXME(D): See the atomicity comment in flush_rewrite_jnode. */
		if (! jnode_check_dirty (check) || JF_ISSET (check, JNODE_HEARD_BANSHEE)) {
			flush_dequeue_jnode (pos, check);
			trace_on (TRACE_FLUSH, "flush_empty_queue not dirty %s\n", flush_jnode_tostring (check));
			continue;
		}

		assert ("jmacd-71236", jnode_check_flushprepped (check));

		/* Increase number of flushed jnodes */
		flushed ++;

		/* FIXME(E): JMACD->ZAM: I think that WANDER nodes should never be put in the
		 * queue at all, they should simply be ignored by jnode_flush_queue or
		 * something similar.  Then we don't need this special case here or below
		 * (See the NOTE*** mark below). */
		if (WRITE_LOG && JF_ISSET (check, JNODE_WANDER)) {
			/* Log-writer expects these to be on the clean list.  They cannot
			 * leave memory and will remain captured. */
			flush_dequeue_jnode (pos, check);

			trace_on (TRACE_FLUSH, "flush_empty_queue skips wandered %s\n", flush_jnode_tostring (check));
			continue;
		}

		ret = jload (check);
		if (ret != 0) {
			warning ("nikita-2423", "Failure to load jnode: %i", ret);
			info_jnode ("jnode", check);
			break;
		}
		/* Lock the first page, test writeback. */
		cpage = jnode_lock_page (check);
		spin_unlock_jnode (check);
		assert ("jmacd-78199", cpage != NULL);

		/* ZAM writes: This situation should be impossible with new flush queue
		 * implementation */
		assert ("jmacd-89162", ! PageWriteback (cpage));

		if (1) {

			/* Find consecutive nodes. */
			struct bio *bio;
			/*jnode *prev = check;*/
			int nr, i;
			struct super_block *super;
			int blksz;
			int max_j;

			super = cpage->mapping->host->i_sb;
			assert( "jmacd-2029", super != NULL );

			/* FIXME: Should eliminate these #if lines, fix ulevel to support
			 * the operations: */
#if REISER4_USER_LEVEL_SIMULATION
			max_j = pos->queue_num;
#else
 			max_j = min (pos->queue_num, (bdev_get_queue (super->s_bdev)->max_sectors >> (super->s_blocksize_bits - 9)));
#endif

			nr = 1;

			while (1) {
				struct page *npage;

				if (capture_list_end(&pos->queue, node) || nr > max_j)
					break;

				npage = jnode_lock_page (node);
				spin_unlock_jnode (node);

				if ((WRITE_LOG && JF_ISSET (node, JNODE_WANDER)) /* NOTE*** Wandered blocks should not enter the queue.  See the note above */ ||
				    /*JF_ISSET (node, JNODE_FLUSH_BUSY) ||*/
				    (*jnode_get_block (node) != *jnode_get_block (check) + 1) ||
				    PageWriteback (npage)) {
					unlock_page (npage);
					break;
				}

				nr ++;
				node = capture_list_next (node);
			}

			/* FIXME: JMACD->NIKITA: Is this GFP flag right? */
			if ((bio = bio_alloc (GFP_NOIO, nr)) == NULL) {
				/* FIXME(F): EEEEK, Pages are all locked right now.  Help! */
				ret = -ENOMEM;
				warning ("jmacd-987123", "Self destruct");
				break;
			}

			blksz = super->s_blocksize;
			assert( "jmacd-2028", blksz == ( int ) PAGE_CACHE_SIZE );

			bio->bi_sector = *jnode_get_block (check) * (blksz >> 9);
			bio->bi_bdev   = super->s_bdev;
			bio->bi_end_io = flush_bio_write;

			trace_on (TRACE_FLUSH_VERB, "flush_empty_queue writes");

			for (node = check, i = 0; i < nr; i++) {
				struct page *pg;
				jnode * tmp = node;

				node = capture_list_next (node);

				trace_on (TRACE_FLUSH_VERB, " %s", flush_jnode_tostring (tmp));

				ret = jload (tmp);
				if (ret != 0) {
					warning ("nikita-2430", "Failure to load jnode: %i", ret);
					info_jnode ("jnode", tmp);
					continue;
				}

				pg = jnode_page (tmp);
				assert ("jmacd-71442", super == pg->mapping->host->i_sb);

				assert ("jmacd-74233", !PageWriteback (pg));
				SetPageWriteback (pg);
				set_page_clean_nolock(pg);

				unlock_page (pg);

				/* Prepare node to being written by calling the io_hook.
				 * This checks, among other things, that there are no
				 * unallocated children of this node.
				 */
				/* FIXME(G): JMACD->?: The io_hook is temporarily disabled
				 * until we actually solve the unallocated-children issue.
				 * Until then, it is possible that we will write nodes
				 * with unallocated children, it just means those nodes
				 * will be dirtied again when the children are
				 * allocated. */
				/*jnode_ops (tmp)->io_hook (tmp, pg, WRITE);*/

				bio->bi_io_vec[i].bv_page   = pg;
				bio->bi_io_vec[i].bv_len    = blksz;
				bio->bi_io_vec[i].bv_offset = 0;

				flush_dequeue_jnode(pos, tmp);
				jrelse (tmp);
			}

			bio->bi_vcnt = nr;
			bio->bi_size   = blksz * nr;

			pos->prep_or_free_cnt += nr;

			trace_on (TRACE_FLUSH_VERB, "\n");
			trace_on (TRACE_FLUSH, "flush_empty_queue %u consecutive blocks: BIO %p\n", nr, bio);

			/* FIXME(B): JMACD->ZAM: 'check' is not the last written location,
			 * bio->bi_vec[i] is? */
			reiser4_update_last_written_location (super, jnode_get_block (check));

			ret = current_atom_add_bio(bio);

			if (ret) {
				bio_put (bio);
				/* FIXME: we should unlock pages before quit */
				return ret;
			}

			submit_bio (WRITE, bio);
		}
		jrelse (check);
	}

	blk_run_queues ();
	trace_if (TRACE_FLUSH, if (ret == 0) { info ("flush_empty_queue wrote %u\n", pos->queue_num); });

	return ret;
}

/* This writes a single page when it is flushed after an earlier allocation within the
 * same txn. */
static int flush_rewrite_jnode (jnode *node)
{
	struct page *pg;
	int ret;

	/* FIXME(D): Have to be absolutely sure that HEARD_BANSHEE isn't set when we write,
	 * otherwise if the page was a fresh allocation the dealloc of that block might
	 * have been non-deferred, and then we could trash otherwise-allocated data? */
	assert ("jmacd-53312", spin_jnode_is_locked (node));
	assert ("jmacd-53313", jnode_is_dirty (node));
	assert ("jmacd-53314", ! JF_ISSET (node, JNODE_HEARD_BANSHEE));
	assert ("jmacd-53315", ! JF_ISSET (node, JNODE_FLUSH_QUEUED));
	assert ("jmacd-53316", jnode_is_flushprepped (node));

	/* If this node is being wandered, just set it clean and return. */
	if ((WRITE_LOG && JF_ISSET (node, JNODE_WANDER))) {
		spin_unlock_jnode (node);
		jnode_set_clean (node);
		return 0;
	}

	/* FIXME: JMACD->NIKITA: This spinlock does very little.  Why?  Races are
	 * everywhere and I'm confused.  What do you think? */
	spin_unlock_jnode (node);

	pg = jnode_lock_page (node);
	spin_unlock_jnode (node);
	if (pg == NULL) {
		return 0;
	}

	jnode_set_clean (node);

	/*
	 * FIXME-NIKITA not sure about this. mpage.c:mpage_writepages() does
	 * this,
	 */
	if (unlikely (PageWriteback (pg))) {
		unlock_page (pg);
		return 0;
	}

	/* NOTE: this page can be not dirty due to mpage.c:mpage_writepages
	 * strange behavior which is in cleaning page right before calling
	 * reiser4 hook (reiser4_writepage) */
	// assert ("jmacd-76515", PageDirty (pg));

	ret = write_one_page (pg, 0 /* no wait */);

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
				    load_count *parent_zh,
				    znode_lock_mode parent_mode)
{
	int ret;

	assert ("nikita-2375",
		jnode_is_unformatted (node) || jnode_is_znode (node));
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

		if ((ret = incr_load_count_znode (parent_zh, parent_lh->node))) {
			return ret;
		}

	} else {
		/* Formatted node case: */
		assert ("jmacd-2061", ! znode_is_root (JZNODE (node)));

		if ((ret = reiser4_get_parent (parent_lh, JZNODE (node), parent_mode, 0))) {
			return ret;
		}

		/* Make the child's position "hint" up-to-date.  (Unless above
		 * root, which caller must check.) */
		if (coord != NULL) {

			if ((ret = incr_load_count_znode (parent_zh, parent_lh->node))) {
				warning ("jmacd-976812", "incr_load_count_znode failed: %d", ret);
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

	assert ("jmacd-6334", znode_is_connected (node));

	spin_lock_tree (current_tree);
	neighbor = side == RIGHT_SIDE ? node->right : node->left;
	if (neighbor != NULL) {
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

	if ((ret = reiser4_get_neighbor (lock, node, mode, GN_SAME_ATOM | (side == LEFT_SIDE ? GN_GO_LEFT  : 0)))) {
		/* May return -ENOENT or -ENAVAIL. */
		/* FIXME(C): check EINVAL, EDEADLK */
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
	assert ("jmacd-7011", znode_is_write_locked (a));
	assert ("jmacd-7012", znode_is_write_locked (b));

	return UNDER_SPIN (tree, current_tree, 
			   (znode_parent_nolock (a) == znode_parent_nolock (b)));
}

/********************************************************************************
 * FLUSH SCAN
 ********************************************************************************/

/* Initialize the flush_scan data structure. */
static void flush_scan_init (flush_scan *scan)
{
	memset (scan, 0, sizeof (*scan));
	init_lh (& scan->node_lock);
	init_lh (& scan->parent_lock);
	init_load_count (& scan->parent_load);
	init_load_count (& scan->node_load);
	coord_init_invalid (& scan->parent_coord, NULL);
}

/* Release any resources held by the flush scan, e.g., release locks, free memory, etc. */
static void flush_scan_done (flush_scan *scan)
{
	done_load_count (& scan->node_load);
	if (scan->node != NULL) {
		jput (scan->node);
		scan->node = NULL;
	}
	done_load_count (& scan->parent_load);
	done_lh (& scan->parent_lock);
	done_lh (& scan->node_lock);
}

/* Returns true if flush scanning is finished. */
static int flush_scan_finished (flush_scan *scan)
{
	return scan->stop || scan->count >= scan->max_count;
}

/* Return true if the scan should continue to the @tonode.  True if the node meets the
 * same_atom_dirty condition.  If not, deref the "left" node and stop the scan. */
static int flush_scan_goto (flush_scan *scan, jnode *tonode)
{
	int go = txn_same_atom_dirty (scan->node, tonode, 1, 0);

	if (! go) {
		scan->stop = 1;
		trace_on (TRACE_FLUSH_VERB, "flush %s scan stop: stop at node %s\n", flush_scanning_left (scan) ? "left" : "right", flush_jnode_tostring (scan->node));
		trace_on (TRACE_FLUSH_VERB, "flush %s scan stop: do not cont at %s\n", flush_scanning_left (scan) ? "left" : "right", flush_jnode_tostring (tonode));
		jput (tonode);
	}

	return go;
}

/* Set the current scan->node, refcount it, increment count by the @add_count (number to
 * count, e.g., skipped unallocated nodes), deref previous current, and copy the current
 * parent coordinate. */
static int flush_scan_set_current (flush_scan *scan, jnode *node, unsigned add_count, const coord_t *parent)
{
	/* Release the old references, take the new reference. */
	done_load_count (& scan->node_load);

	if (scan->node != NULL) {
		jput (scan->node);
	}

	scan->node  = node;
	scan->count += add_count;

	/* This next stmt is somewhat inefficient.  The flush_scan_extent_coord code could
	 * delay this update step until it finishes and update the parent_coord only once.
	 * It did that before, but there was a bug and this was the easiest way to make it
	 * correct. */
	if (parent != NULL) {
		coord_dup (& scan->parent_coord, parent);
	}

	/* Failure may happen at the incr_load_count call, but the caller can assume the reference
	 * is safely taken. */
	return incr_load_count_jnode (& scan->node_load, node);
}

/* Return true if scanning in the leftward direction. */
static int flush_scanning_left (flush_scan *scan)
{
	return scan->direction == LEFT_SIDE;
}

/* Performs leftward scanning starting from either kind of node.  Counts the starting
 * node.  The right-scan object is passed in for the left-scan in order to copy the parent
 * of an unformatted starting position.  This way we avoid searching for the unformatted
 * node's parent when scanning in each direction.  If we search for the parent once it is
 * set in both scan objects.  The limit parameter tells flush-scan when to stop.
 *
 * Rapid scanning is used only during scan_left, where we are interested in finding the
 * 'leftpoint' where we begin flushing.  We are interested in stopping at the left child
 * of a twig that does not have a dirty left neighbor.  THIS IS A SPECIAL CASE.  The
 * problem is finding a way to flush only those nodes without unallocated children, and it
 * is difficult to solve in the bottom-up flushing algorithm we are currently using.  The
 * problem can be solved by scanning left at every level as we go upward, but this would
 * basically bring us back to using a top-down allocation strategy, which we already tried
 * (see BK history from May 2002), and has a different set of problems.  The top-down
 * strategy makes avoiding unallocated children easier, but makes it difficult to
 * propertly flush dirty children with clean parents that would otherwise stop the
 * top-down flush, only later to dirty the parent once the children are flushed.  So we
 * solve the problem in the bottom-up algorithm with a special case for twigs and leaves
 * only.
 *
 * The first step in solving the problem is this rapid leftward scan.  After we determine
 * that there are at least enough nodes counted to qualify for FLUSH_RELOCATE_THRESHOLD we
 * are no longer interested in the exact count, we are only interested in finding a the
 * best place to start the flush.  We could choose one of two possibilities:
 *
 * 1. Stop at the leftmost child (of a twig) that does not have a dirty left neighbor.
 * This requires checking one leaf per rapid-scan twig
 *
 * 2. Stop at the leftmost child (of a twig) where there are no dirty children of the twig
 * to the left.  This requires checking possibly all of the in-memory children of each
 * twig during the rapid scan.
 *
 * For now we implement the first policy.
 */
static int flush_scan_left (flush_scan *scan, flush_scan *right, jnode *node, unsigned limit)
{
	int ret;

	scan->max_count  = limit;
	scan->direction = LEFT_SIDE;

	if ((ret = flush_scan_set_current (scan, jref (node), 1 /* count starting node */, NULL /* no parent coord */))) {
		return ret;
	}

	if ((ret = flush_scan_common (scan, right))) {
		return ret;
	}

	/* Before rapid scanning, we need a lock on scan->node so that we can get its
	 * parent, only if formatted. */
	if (jnode_is_znode (scan->node) &&
	    (ret = longterm_lock_znode (& scan->node_lock, JZNODE (scan->node), ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI))) {
		/* EINVAL means the node was deleted, DEADLK should be impossible here
		 * because we've asserted that the lockstack is clean. */
		assert ("jmacd-34113", ret != -EDEADLK);
		return ret;
	}

	/* Rapid_scan would go here (with limit set to FLUSH_RELOCATE_THRESHOLD). */
	return 0;
}

/* Performs rightward scanning... Does not count the starting node.  The limit parameter
 * is described in flush_scan_left.  If the starting node is unformatted then the
 * parent_coord was already set during scan_left.  The rapid_after parameter is not used
 * during right-scanning.
 *
 * scan_right is only called if the scan_left operation does not count at least
 * FLUSH_RELOCATE_THRESHOLD nodes for flushing.  Otherwise, the limit parameter is set to
 * the difference between scan-left's count and FLUSH_RELOCATE_THRESHOLD, meaning
 * scan-right counts as high as FLUSH_RELOCATE_THRESHOLD and then stops. */
static int flush_scan_right (flush_scan *scan, jnode *node, unsigned limit)
{
	int ret;

	scan->max_count  = limit;
	scan->direction = RIGHT_SIDE;

	if ((ret = flush_scan_set_current (scan, jref (node), 0 /* do not count starting node */, NULL /* no parent coord */))) {
		return ret;
	}

	return flush_scan_common (scan, NULL);
}

/* Common code to perform left or right scanning. */
static int flush_scan_common (flush_scan *scan, flush_scan *other)
{
	int ret;

	assert ("nikita-2376", scan->node != NULL);
	assert ("nikita-2377",
		jnode_is_unformatted (scan->node) || jnode_is_znode (scan->node));

	/* Special case for starting at an unformatted node.  Optimization: we only want
	 * to search for the parent (which requires a tree traversal) once.  Obviously, we
	 * shouldn't have to call it once for the left scan and once for the right scan.
	 * For this reason, if we search for the parent during scan-left we then duplicate
	 * the coord/lock/load into the scan-right object. */
	if (jnode_is_unformatted (scan->node)) {

		if (coord_is_invalid (& scan->parent_coord)) {

			if ((ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, & scan->parent_load, ZNODE_WRITE_LOCK))) {
				/* FIXME(C): check EINVAL, EDEADLK */
				return ret;
			}

			assert ("jmacd-8661", other != NULL);

			/* Duplicate the reference into the other flush_scan. */
			coord_dup (& other->parent_coord, & scan->parent_coord);
			copy_lh (& other->parent_lock, & scan->parent_lock);
			copy_load_count (& other->parent_load, & scan->parent_load);
		}

		/* The common scan code is structured as a loop that repeatedly calls:
		 *
		 * \_ flush_scan_formatted
		 *    \_ flush_scan_extent
		 *       \_ flush_scan_extent_coord
		 *
		 * But if we start at an unformatted node (extent) then we begin with a
		 * call to flush_scan_extent then fall into the loop at a formatted
		 * position below.
		 */
		if ((ret = flush_scan_extent (scan, 0 /* skip_first = false (i.e., starting position is unformatted) */))) {
			return ret;
		}
	}

	/* This loop expects to start at a formatted position, which explains the
	 * flush_scan_extent case above.  After scanning from a formatted position the
	 * code then checks for an extent and scans past all extents until the next
	 * formatted position, then returns and repeats this loop. */
	while (! flush_scan_finished (scan)) {

		if ((ret = flush_scan_formatted (scan))) {
			return ret;
		}
	}

	return 0;
}

/* Performs left- or rightward scanning starting from a formatted node. Follow left
 * pointers under tree lock as long as:
 *
 * - node->left/right is non-NULL
 * - node->left/right is connected, dirty
 * - node->left/right belongs to the same atom
 * - scan has not reached maximum count
 */
static int flush_scan_formatted (flush_scan *scan)
{
	int ret;
	znode *neighbor = NULL;

	assert ("jmacd-1401", ! flush_scan_finished (scan));

	do {
		znode *node = JZNODE (scan->node);

		/* Node should be connected, but if not stop the scan. */
		if (! znode_is_connected (node)) {
			scan->stop = 1;
			break;
		}

		/* Lock the tree, check-for and reference the next sibling. */
		spin_lock_tree (current_tree);

		/* It may be that a node is inserted or removed between a node and its
		 * left sibling while the tree lock is released, but the flush-scan count
		 * does not need to be precise.  Thus, we release the tree lock as soon as
		 * we get the neighboring node. */
		if ((neighbor = flush_scanning_left (scan) ? node->left : node->right) != NULL) {
			zref (neighbor);
		}

		spin_unlock_tree (current_tree);

		/* If neighbor is NULL at the leaf level, need to check for an unformatted
		 * sibling using the parent--break in any case. */
		if (neighbor == NULL) {
			break;
		}

		trace_on (TRACE_FLUSH_VERB, "format scan %s %s\n",
			  flush_scanning_left (scan) ? "left" : "right",
			  flush_znode_tostring (neighbor));

		/* Check the condition for going left, break if it is not met.  This also
		 * releases (jputs) the neighbor if false. */
		if (! flush_scan_goto (scan, ZJNODE (neighbor))) {
			break;
		}

		/* Advance the flush_scan state to the left, repeat. */
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

	/* Otherwise, check for an extent at the parent level then possibly continue. */
	{
		int ret;
		lock_handle end_lock;

		init_lh (& end_lock);

		/* Need the node locked to get the parent lock, We have to
		   take write lock since there is at least one call path
		   where this znode is already write-locked by us. */
		if ((ret = longterm_lock_znode (& end_lock, JZNODE (scan->node), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
			/* EINVAL or EDEADLK here mean... try again!  At this point we've
			 * scanned too far and can't back out, just start over. */
			return ret;
		}

		/* This is a write-lock since we may start flushing from this locked coordinate. */
		ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, & scan->parent_load, ZNODE_WRITE_LOCK);
		/* FIXME(C): check EINVAL, EDEADLK */

		done_lh (& end_lock);

		if (ret != 0) { return ret; }

		/* With the parent coordinate set, call flush_scan_extent.  If the next
		 * position is not an extent, flush_scan_extent will detect it and do
		 * nothing. */
		return flush_scan_extent (scan, 1 /* skip_first = true (i.e., current position is formatted) */);
	}
}

/* Performs leftward scanning starting from a (possibly) unformatted node.  Skip_first
 * indicates that the scan->node is set to a formatted node and we are interested in
 * continuing at the next neighbor only if it is unformatted.  When called initially from
 * flush_scan_common, it sets skip_first=0 (because the first node is unformatted), but
 * when called from flush_scan_formatted, it sets skip_first=1 (because the current
 * position is formatted).  After one iteration through the loop below, skip_first is
 * reset to zero and the flush .  */
static int flush_scan_extent (flush_scan *scan, int skip_first)
{
	int ret = 0;
	lock_handle next_lock;
	load_count next_load;
	coord_t next_coord;
	jnode *child;

	init_lh (& next_lock);
	init_load_count (& next_load);

	for (; ! flush_scan_finished (scan); skip_first = 0) {

		/* If not skipping the first item (only true the first iteration of this
		 * loop when called from flush_scan_formatted), then we call
		 * flush_scan_extent_coord, which scans as many extent units as it can
		 * until either finding a non-dirty jnode, a formatted node (internal
		 * unit), or reaching the end of node. */
		if (skip_first == 0) {

			assert ("jmacd-1230", item_is_extent (& scan->parent_coord));

			if ((ret = flush_scan_extent_coord (scan, & scan->parent_coord))) {
				goto exit;
			}

			if (flush_scan_finished (scan)) {
				break;
			}
		} else {
			/* In this case, apply the same end-of-node logic but don't scan
			 * the first coordinate. */
			assert ("jmacd-1231", item_is_internal (& scan->parent_coord));
		}

		/* Either way, the invariant is that scan->parent_coord is set to the
		 * parent of scan->node.  Now get the next item. */
		coord_dup (& next_coord, & scan->parent_coord);
		coord_sideof_unit (& next_coord, scan->direction);

		/* If off-the-end of the twig, try the next twig. */
		if (coord_is_after_sideof_unit (& next_coord, scan->direction)) {

			/* We take the write lock because we may start flushing from this coordinate. */
			ret = znode_get_utmost_if_dirty (next_coord.node, & next_lock, scan->direction, ZNODE_WRITE_LOCK);

			if (ret == -ENAVAIL) { scan->stop = 1; ret = 0; break; }

			if (ret != 0) { goto exit; }

			if ((ret = incr_load_count_znode (& next_load, next_lock.node))) {
				goto exit;
			}

			coord_init_sideof_unit (& next_coord, next_lock.node, sideof_reverse (scan->direction));
		}

		/* If skip_first was set, then we are only interested in continuing if the
		 * next item is an extent.  If this is not the case, stop now.  Otherwise,
		 * if the next item is an extent we will return to the flush_scan_common
		 * loop and the next call will be to scan_formatted() to handle this
		 * case. */
		if (! item_is_extent (& next_coord) && skip_first) {
			scan->stop = 1;
			break;
		}

		/* Get the next child. */
		if ((ret = item_utmost_child (& next_coord, sideof_reverse (scan->direction), & child))) {
			goto exit;
		}

		/* If the next child is not in memory, stop here. */
		if (child == NULL) {
			scan->stop = 1;
			break;
		}

		assert ("nikita-2374",
			jnode_is_unformatted (child) || jnode_is_znode (child));

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
		if (jnode_is_znode (child)) {
			break;
		}

		/* Otherwise, repeat the above loop with next_coord. */
		if (next_load.node != NULL) {
			done_lh (& scan->parent_lock);
			move_lh (& scan->parent_lock, & next_lock);
			move_load_count (& scan->parent_load, & next_load);
		}

		assert ("jmacd-1239", item_is_extent (& scan->parent_coord));
	}

	assert ("jmacd-6233", flush_scan_finished (scan) || jnode_is_znode (scan->node));
 exit:
	if (jnode_is_znode (scan->node)) {
		done_lh (& scan->parent_lock);
		done_load_count (& scan->parent_load);
	}

	done_load_count (& next_load);
	done_lh (& next_lock);
	return ret;
}


/* Performs leftward scanning starting from an unformatted node and its parent coordinate.
 * This scan continues, advancing the parent coordinate, until either it encounters a
 * formatted child or it finishes scanning this node.
 *
 * If unallocated, the entire extent must be dirty and in the same atom.  (Actually, I'm
 * not sure this is last property (same atom) is enforced, but it should be the case since
 * one atom must write the parent and the others must read the parent, thus fusing?).  In
 * any case, the code below asserts this case for unallocated extents.  Unallocated
 * extents are thus optimized because we can skip to the endpoint when scanning.
 *
 * It returns control to flush_scan_extent, handles these terminating conditions, e.g., by
 * loading the next twig.
 */
static int flush_scan_extent_coord (flush_scan *scan, const coord_t *in_coord)
{
	coord_t coord;
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist;
	reiser4_block_nr unit_start;
	struct inode *ino = NULL;
	struct page *pg;
	int ret = 0, allocated, incr;

	coord_dup (& coord, in_coord);

	assert ("jmacd-1404", ! flush_scan_finished (scan));
	assert ("jmacd-1405", jnode_get_level (scan->node) == LEAF_LEVEL);
	assert ("jmacd-1406", jnode_is_unformatted (scan->node));

	/* The scan_index variable corresponds to the current page index of the
	 * unformatted block scan position. */
	scan_index = jnode_get_index (scan->node);

	assert ("jmacd-7889", item_is_extent (& coord));

	trace_on (TRACE_FLUSH_VERB, "%s scan starts %lu: %s\n",
		  (flush_scanning_left (scan) ? "left" : "right"),
		  scan_index,
		  flush_jnode_tostring (scan->node));

 repeat:
	/* If the get_inode call is expensive we can be a bit more clever and only call
	 * get_inode when the extent item changes, not just the extent unit.  As it is,
	 * this repeats the get_inode call for every unit even when the OID doesn't
	 * change. */
	extent_get_inode (& coord, & ino);

	if (ino == NULL) {
		scan->stop = 1;
		return 0;
	}

	trace_on (TRACE_FLUSH_VERB, "%s scan index %lu: parent %p inode %lu\n",
		  (flush_scanning_left (scan) ? "left" : "right"),
		  scan_index, coord.node, ino->i_ino);

	/* Get the values of this extent unit: */
	allocated  = ! extent_is_unallocated (& coord);
	unit_index = extent_unit_index (& coord);
	unit_width = extent_unit_width (& coord);
	unit_start = extent_unit_start (& coord);

	assert ("jmacd-7187", unit_width > 0);
	assert ("jmacd-7188", scan_index >= unit_index);
	assert ("jmacd-7189", scan_index <= unit_index + unit_width - 1);

	/* Depending on the scan direction, we set different maximum values for scan_index
	 * (scan_max) and the number of nodes that would be passed if the scan goes the
	 * entire way (scan_dist).  Incr is an integer reflecting the incremental
	 * direction of scan_index. */
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
			/* Note: On the very first pass through this block we test the
			 * current position (pg of the starting scan_index, which we know
			 * is dirty/same atom by pre-condition).  Its redundent but it
			 * makes this code simpler. */
			pg = reiser4_lock_page (ino->i_mapping, scan_index);

			if (pg == NULL) {
				goto stop_same_parent;
			}

			neighbor = jnode_of_page (pg);

			unlock_page (pg);
			page_cache_release (pg);

			if (IS_ERR(neighbor)) {
				ret = PTR_ERR(neighbor);
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
		pg = reiser4_lock_page (ino->i_mapping, scan_max);

		if (pg == NULL) {
			impossible ("jmacd-8337", "unallocated node index %lu ino %lu not in memory", scan_max, ino->i_ino);
			ret = -EIO;
			goto exit;
		}

		neighbor = jnode_of_page (pg);

		unlock_page (pg);
		page_cache_release (pg);

		if (IS_ERR(neighbor)) {
			ret = PTR_ERR(neighbor);
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "unalloc scan index %lu: %s\n", scan_index, flush_jnode_tostring (neighbor));

		assert ("jmacd-3551", ! jnode_check_flushprepped (neighbor) && txn_same_atom_dirty (neighbor, scan->node, 0, 0));

		if ((ret = flush_scan_set_current (scan, neighbor, scan_dist, & coord))) {
			goto exit;
		}
	}

	if (coord_sideof_unit (& coord, scan->direction) == 0 && item_is_extent (& coord)) {
		/* Continue as long as there are more extent units. */

		scan_index = extent_unit_index (& coord) + (flush_scanning_left (scan) ? extent_unit_width (& coord) - 1 : 0);
		assert ("vs-835", ino);
		iput (ino);
		goto repeat;
	}

	if (0) {
	stop_same_parent:

		/* If we are scanning left and we stop in the middle of an allocated
		 * extent, we know the preceder immediately.. */
		if (flush_scanning_left (scan) && unit_start != 0) {
			/* FIXME(B): Someone should step-through and verify that this preceder
			 * calculation is indeed correct. */
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

	ret = 0;
 exit:
	if (ino != NULL) { iput (ino); }
	return ret;
}

/********************************************************************************
 * FLUSH POS HELPERS
 ********************************************************************************/

/* Initialize the fields of a flush_position. */
static int flush_pos_init (flush_position *pos, int *nr_to_flush)
{
	capture_list_init (&pos->queue);
	flushers_list_clean (pos);

	pos->queue_num = 0;
	pos->point = NULL;
	pos->leaf_relocate = 0;
	pos->alloc_cnt = 0;
	pos->prep_or_free_cnt = 0;
	pos->nr_to_flush = nr_to_flush;

	coord_init_invalid (& pos->parent_coord, NULL);

	blocknr_hint_init (& pos->preceder);
	init_lh (& pos->point_lock);
	init_lh (& pos->parent_lock);
	init_load_count (& pos->parent_load);
	init_load_count (& pos->point_load);

	return 0;
}

/* The flush loop inside flush_forward_squalloc periodically checks flush_pos_valid to
 * determine when "enough flushing" has been performed.  This will return true until one
 * of the following conditions is met:
 *
 * 1. the number of flush-queued nodes has reached the kernel-supplied "int *nr_to_flush"
 * parameter, meaning we have flushed as many blocks as the kernel requested.  When
 * flushing to commit, this parameter is NULL.
 *
 * 2. flush_pos_stop() is called because squalloc discovers that the "next" node in the
 * flush order is either non-existant, not dirty, or not in the same atom.
 */
static int flush_pos_valid (flush_position *pos)
{
	if (pos->nr_to_flush != NULL && pos->prep_or_free_cnt >= *pos->nr_to_flush) {
		return 0;
	}
	return pos->point != NULL || lock_mode (& pos->parent_lock) != ZNODE_NO_LOCK;
}

/* Return jnode back to atom's lists */
static void invalidate_flush_queue (struct flush_position * pos)
{
	if (capture_list_empty(&pos->queue)) return;


	while (1) {
		jnode * cur = capture_list_pop_front (&pos->queue);
		txn_atom * atom;

		spin_lock_jnode (cur);
		atom = atom_get_locked_by_jnode (cur);

		/*JF_CLR (cur, JNODE_FLUSH_BUSY);*/
		JF_CLR (cur, JNODE_FLUSH_QUEUED);

		pos->queue_num --;
		atom->num_queued --;

		if (jnode_is_dirty(cur)) capture_list_push_back (&atom->dirty_nodes[jnode_get_level(cur)], cur);
		else                     capture_list_push_back (&atom->clean_nodes, cur);

		spin_unlock_jnode (cur);

		if (capture_list_empty(&pos->queue)) {
			flushers_list_remove(pos);
			spin_unlock_atom (atom);
			break;
		}

		spin_unlock_atom (atom);
	}
}

/* Release any resources of a flush_position.  Called when jnode_flush finishes. */
static void flush_pos_done (flush_position *pos)
{
	invalidate_flush_queue (pos);
	flush_pos_stop (pos);
	blocknr_hint_done (& pos->preceder);
}

/* Reset the point and parent.  Called during flush subroutines to terminate the
 * flush_forward_squalloc loop. */
static int flush_pos_stop (flush_position *pos)
{
	done_load_count (& pos->parent_load);
	done_load_count (& pos->point_load);
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_lh (& pos->point_lock);
	done_lh (& pos->parent_lock);
	coord_init_invalid (& pos->parent_coord, NULL);
	return 0;
}

/* When the flush_position is on a twig and just finished allocating an extent, if the
 * next item is an internal item, this function descends to the child and, if the child is
 * not flushprepped, it sets the flush_position to continue at the leaf level.  */
static int flush_pos_to_child_and_alloc (flush_position *pos)
{
	int ret;
	jnode *child;

	assert ("jmacd-6078", flush_pos_on_twig_level (pos));
	assert ("jmacd-6079", lock_mode (& pos->point_lock) == ZNODE_NO_LOCK);
	assert ("jmacd-6080", pos->point_load.d_ref == 0);

	trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: %s\n", flush_pos_tostring (pos));

	/* Get the child if it is memory, lock it, unlock the parent. */
	if ((ret = item_utmost_child (& pos->parent_coord, LEFT_SIDE, & child))) {
		return ret;
	}

	if (child == NULL) {
		trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: STOP (no child): %s\n", flush_pos_tostring (pos));
		goto stop;
	}

	if (jnode_check_flushprepped (child)) {
		trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: STOP (already flushprepped): %s\n", flush_pos_tostring (pos));
		jput (child);
		goto stop;
	}

	assert ("jmacd-8861", jnode_is_znode (child));

	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}

	pos->point = child;

	if ((ret = longterm_lock_znode (& pos->point_lock, JZNODE (child), ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI))) {
		return ret;
	}

	if ((ret = flush_allocate_znode (JZNODE (child), & pos->parent_coord, pos))) {
		return ret;
	}

	if (0) {
	stop:
		ret = flush_pos_stop (pos);
		return ret;
	}

	/* And keep going... */
	done_load_count (& pos->parent_load);
	done_lh (& pos->parent_lock);
	coord_init_invalid (& pos->parent_coord, NULL);
	return 0;
}

/* The flush position is positioned at the leaf level and wants to shift to the parent
 * level.  Releases the flush_position->point_lock, acquires flush_position->parent_lock
 * and sets the parent coordinate. */
static int flush_pos_to_parent (flush_position *pos)
{
	int ret;

	assert ("jmacd-6078", ! flush_pos_on_twig_level (pos));

	/* Lock the parent, find the coordinate. */
	if ((ret = jnode_lock_parent_coord (pos->point, & pos->parent_coord, & pos->parent_lock, & pos->parent_load, ZNODE_WRITE_LOCK))) {
		/* FIXME(C): check EINVAL, EDEADLK */
		return ret;
	}

	/* When this is called, we have already tried the sibling link of the znode in
	 * question, therefore we are not interested in saving ->point. */
	done_load_count (& pos->point_load);
	done_lh (& pos->point_lock);

	/* Note: we leave the point set, but unlocked/unloaded. */
	/* This is a bad idea if the child can be deleted.... but it helps the call to
	 * left_relocate.  Needs a better solution.  Related to "Funny business" comment
	 * above. */
	return 0;
}

/* Return true if the flush position is set to the parent coordinate of some node on the
 * leaf level.  This is done as part of handling unformatted nodes. */
static int flush_pos_on_twig_level (flush_position *pos)
{
	return pos->parent_lock.node != NULL;
}

/* Release all references associated with flush_position->point: a reference count,
 * load_count, and lock_handle. */
static void flush_pos_release_point (flush_position *pos)
{
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_load_count (& pos->point_load);
	done_lh (& pos->point_lock);
}

/* This function changes the current value of flush_position->point by releasing the old
 * point (its ref_count, load_count, and lock_handle) then refcounting and load_counting
 * the new point.  Setting the new point's lock_handle is left to the calling code. */
static int flush_pos_set_point (flush_position *pos, jnode *node)
{
	flush_pos_release_point (pos);
	pos->point = jref (node);
	return incr_load_count_jnode (& pos->point_load, node);
}

/* This function implements a special case to lock the parent coordinate of the current
 * flush point.  If the flush position is on the twig level then the parent coordinate is
 * already locked--otherwise we actually lock the parent coordinate. */
static int flush_pos_lock_parent (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, load_count *parent_load, znode_lock_mode mode)
{
	int ret;

	if (flush_pos_on_twig_level (pos)) {
		/* In this case we already have the parent locked. */
		znode_lock_mode have_mode = lock_mode (& pos->parent_lock);

		/* For now we only deal with the case where the previously requested
		 * parent lock has the proper mode.  Otherwise we have to release the lock
		 * here and get a new one. */
		assert ("jmacd-9923", have_mode == mode);
		copy_lh (parent_lock, & pos->parent_lock);
		if ((ret = incr_load_count_znode (parent_load, parent_lock->node))) {
			return ret;
		}
		coord_dup (parent_coord, & pos->parent_coord);
		return 0;

	} else {
		assert ("jmacd-9922", ! znode_is_root (JZNODE (pos->point)));
		assert ("jmacd-9924", lock_mode (& pos->parent_lock) == ZNODE_NO_LOCK);
		/* FIXME(C): check EINVAL, EDEADLK */
		return jnode_lock_parent_coord (pos->point, parent_coord, parent_lock, parent_load, mode);
	}
}

/* Return the flush_position's block allocator hint. */
reiser4_blocknr_hint* flush_pos_hint (flush_position *pos)
{
	return & pos->preceder;
}

/* Return true if we have decided to unconditionally relocate leaf nodes, thus write
 * optimizing. */
int flush_pos_leaf_relocate (flush_position *pos)
{
	return pos->leaf_relocate;
}

/* During atom fusion, splice together the list of current flush positions. */
void flush_fuse_queues (txn_atom *large, txn_atom *small)
{
	flush_position *pos;

	assert ("zam-659", spin_atom_is_locked (large));
	assert ("zam-660", spin_atom_is_locked (small));

	for (pos = flushers_list_front (&small->flushers);
	     /**/! flushers_list_end   (&small->flushers, pos);
	     pos = flushers_list_next (pos)) {
		jnode * scan;

		pos->atom = large;

		for (scan = capture_list_front (&pos->queue);
		     /**/ ! capture_list_end   (&pos->queue, scan);
		     scan = capture_list_next (scan)) {
			UNDER_SPIN_VOID (jnode, scan, scan->atom = large);
		}
	}

	flushers_list_splice (&large->flushers, & small->flushers);
	large->num_queued += small->num_queued;
	small->num_queued = 0;
}

void flush_init_atom (txn_atom * atom)
{
	flushers_list_init (& atom->flushers);
}

static flush_params *flush_get_params( void )
{
	return &get_current_super_private() -> flush;
}

//#if REISER4_DEBUG
#if 1
static void flush_jnode_tostring_internal (jnode *node, char *buf)
{
	const char* state;
	char atom[32];
	char block[32];
	char items[32];
	int fmttd;
	int dirty;
	int lockit;

	lockit = spin_trylock_jnode (node);

	fmttd = jnode_is_znode (node);
	dirty = JF_ISSET (node, JNODE_DIRTY);

	sprintf (block, " block=%llu", *jnode_get_block (node));

	if (JF_ISSET (node, JNODE_WANDER)) {
		state = dirty ? "wandr,dirty" : "wandr";
	} else if (JF_ISSET (node, JNODE_RELOC) && JF_ISSET (node, JNODE_CREATED)) {
		state = dirty ? "creat,dirty" : "creat";
	} else if (JF_ISSET (node, JNODE_RELOC)) {
		state = dirty ? "reloc,dirty" : "reloc";
	} else if (JF_ISSET (node, JNODE_CREATED)) {
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
		 JF_ISSET (node, JNODE_FLUSH_QUEUED) ? " fq" : "");

	if (lockit == 1) { spin_unlock_jnode (node); }
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
	load_count load;
	fmtbuf[0] = 0;

	init_load_count (& load);

	if (pos->parent_lock.node != NULL) {

		assert ("jmacd-79123", pos->parent_lock.node == pos->parent_load.node);

		strcat (fmtbuf, "par:");
		flush_jnode_tostring_internal (ZJNODE (pos->parent_lock.node), fmtbuf);

		if (incr_load_count_znode (& load, pos->parent_lock.node)) {
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

	done_load_count (& load);
	return fmtbuf;
}

static const char*   flush_flags_tostring         (int flags)
{
	switch (flags) {
	case JNODE_FLUSH_COMMIT: return "(commit)";
	case JNODE_FLUSH_MEMORY_FORMATTED: return "(memory-z)";
	case JNODE_FLUSH_MEMORY_UNFORMATTED: return "(memory-j)";
	default:
		return "(unknown)";
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
