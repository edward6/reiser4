/* Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* The design document for this file is at www.namesys.com/flush-alg.html. */

#include "reiser4.h"

/********************************************************************************
 * IMPLEMENTATION NOTES
 ********************************************************************************/

/* STATE BITS: The flush code revolves around the state of the jnodes it covers.  Here are
 * the relevant jnode->state bits and their relevence to flush:
 *
 *   JNODE_DIRTY: If a node is dirty, it must be flushed.  But in order to be flushed it
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
 *   location for this node and it will be written to the wandered-log.  FIXME(H): The
 *   following NOTE needs to be fixed by adding a wander_list to the atom.  NOTE: In this
 *   case, flush sets the node to be clean!  By returning the node to the clean list,
 *   where the log-writer expects to find it, flush simply pretends it was written even
 *   though it has not been.  Clean nodes with JNODE_WANDER set cannot be released from
 *   memory (checked in reiser4_releasepage()).
 *
 *   JNODE_RELOC: The flush algorithm made the decision to relocate this block (if it was
 *   not created, see note above).  A block with JNODE_RELOC set is elligible for
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
 *   (REMOVED STATE BIT) JNODE_FLUSH_BUSY: This bit was set during the bottom-up
 *   squeeze-and-allocate on a node while its children are actively being squeezed and
 *   allocated.  This flag was created to avoid submitting a write request for a node
 *   while its children are still being allocated and squeezed.  However, this flag is no
 *   longer needed because flush_empty_queue is only called in one place after flush
 *   finishes.  It used to be that flush_empty_queue was called periodically during flush
 *   when there was a fixed queue, but that is no longer done.  See the changes on August
 *   6, 2002 and August 9, 2002 when this support was removed.
 *
 * With these state bits, we describe a test used frequently in the code below,
 * jnode_is_allocated() (and the spin-lock-taking jnode_check_allocated()).  The test for
 * "allocated" returns true if any of the following are true:
 *
 *   - The node is not dirty
 *   - The node has JNODE_RELOC set
 *   - The node has JNODE_WANDER set
 *
 * If either the node is clean or it has already been processed by flush, then it is
 * allocated.  If jnode_is_allocated() returns true then flush has work to do on that
 * node.
 */

/* ALLOCATE_ONCE_PER_TRANSACTION: Within a single transaction a node is never allocated
 * more than once (unless explicit steps are taken to deallocate and reset the
 * RELOC/WANDER bit, see discussion below of level > TWIG with unallocated children).  For
 * example a node is dirtied, allocated, and then early-flushed to disk and set clean.
 * Before the transaction commits, the page is dirtied again and, due to memory pressure,
 * the node is flushed again.  The flush algorithm will not relocate the node to a new
 * disk location, it will simply write it to the same, previously relocated position
 * again.
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
 *
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
 * read-optimization, but this is not always desireable because it may mean having to
 * allocate and flush very many nodes at once.
 *
 * B. Defer writing nodes with unallocated children, keep their read-optimized locations,
 * but sacrifice write-optimization because those nodes will be written again.
 *
 * C. Defer writing nodes with unallocated children, but do not keep their read-optimized
 * locations.  Instead, choose to write-optimize them later, when they are written.  To
 * facilitate this, we "undo" the read-optimized allocation that was given to the node so
 * that later it can be write-optimized.  This is a case where we disturb the
 * ALLOCATE_ONCE_PER_TRANSACTION rule described above.
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
 * explicitely de-allocated.  This is a case where we disturb the
 * ALLOCATE_ONCE_PER_TRANSACTION rule described above.  When a (level > TWIG) node with
 * unallocated children is encountered in flush_empty_queue(), the relocated position is
 * de-allocated and the node is returned to an unallocated (i.e., CREATED) state.  Note:
 * Returning it to a CREATED state makes sense even if the node had an allocation prior to
 * the transaction because its prior location has already been deallocated with the block
 * allocator (deferred).  FIXME: Difficulty to implement: should be simple.
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
 * nodes than would otherwise be written without the RAPID_SCAN option.  FIXME: RAPID_SCAN
 * is partially implemented.
 */

/* FLUSH CALLED ON NON-LEAF LEVEL.  Most of our design considerations assume that the
 * starting point for flush is a leaf node, but actually the flush code cares very little
 * about whether or not this is true.  It is possible that all the leaf nodes are flushed
 * and dirty parent nodes still remain, in which case jnode_flush() is called on a
 * non-leaf argument.  Flush doesn't care--it treats the argument node as if it were a
 * leaf, even when it is not.  This is a simple approach, and there may be a more optimal
 * policy but until a problem with this approach is discovered, simplest is probably best.
 */

/* REPACKING AND RESIZING.  Is the purpose of a resizer to move blocks from their current
 * positions in areas of the disk that are being reclaimed?  I don't think flush will
 * participate in that process.  Somehow the tree should be scanned to locate the parent
 * of all those blocks.  Those blocks can be simply marked dirty, and the block allocator
 * can be informed of a pending resize, causing it to allocate blocks outside the area
 * being reclaimed.
 *
 * A repacker will be based on this code in the following way.  A traversal of the tree is
 * made in parent-first order.  When the repacker finds an area of the tree that is
 * significantly fragmented (using some criterion) it will dirty those nodes and depend on
 * flush to relocate them to a contiguous location on disk.  Most of the details are in
 * defining the criteria used to select blocks for repacking.

FIXME: Restate the above.

 */

/********************************************************************************
 * DECLARATIONS:
 ********************************************************************************/

/* The following RAPID_SCAN define disables the partially-implemented rapid_scan option to
 * flush.  Eventually, after a little more work this can be experimented with as a mount
 * option.  It enables a more expansize/agressive flushing--Hans is concerned that this
 * may lead to "precipitation".  The flush algorithm works with or without rapid_scan, and
 * acts more conservatively without it.  Described in more detail below. */
#define USE_RAPID_SCAN 0

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
	 * going leftward, max_count is determined by FLUSH_SCAN_MAXNODES (see reiser4.h)
	 * if RAPID_SCAN is disabled and FLUSH_RELOCATE_THRESHOLD if RAPID_SCAN is
	 * enabled.  (This is because RAPID_SCAN will skip past clean and dirty nodes on
	 * the level, therefore there is no need to count precisely once the THRESHOLD has
	 * been reached.)  When going rightward, the max_count is determined by the number
	 * of nodes required to reach FLUSH_RELOCATE_THRESHOLD. */
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
	data_handle node_load;

	/* During left-scan, if the final position (a.k.a. endpoint node) is formatted the
	 * node is locked using this lock handle.  The endpoint needs to be locked for two
	 * reasons: RAPID_SCAN requires the lock before starting, and otherwise the lock
	 * is transferred to the flush_position object after scanning finishes. */
	lock_handle node_lock;

	/* When the position is unformatted, its parent, coordinate, and parent
	 * zload/zrelse handle. */
	lock_handle parent_lock;
	coord_t     parent_coord;
	data_handle parent_load;

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
	data_handle           point_load;      /* Loaded point */
	data_handle           parent_load;     /* Loaded parent */
	reiser4_blocknr_hint  preceder;        /* The flush 'hint' state. */
	int                   leaf_relocate;   /* True if enough leaf-level nodes were
						* found to suggest a relocate policy. */
	int                  *nr_to_flush;     /* If called under memory pressure,
						* indicates how many nodes the VM asked to flush. */
	int                   alloc_cnt;       /* The number of nodes allocated during squeeze and allococate. */
	int                   enqueue_cnt;     /* The number of nodes enqueued during squeeze and allocate. */
	capture_list_head     queue;           /* The flush queue holds allocated but not-yet-submitted jnodes that are
						* actually written when flush_empty_queue() is called. */
 	int                   queue_num;       /* The current number of queue entries. */

	flushers_list_link    flushers_link;   /* A list link of all flush_positions active for an atom. */
	txn_atom             *atom;            /* The current atom of this flush_position--maintained during atom fusion. */
	struct reiser4_io_handle * hio;        /* The handle for waiting on I/O completions. */
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
#if 0
static int           flush_scan_rapid             (flush_scan *scan);
static int           flush_scan_leftmost_dirty_unit (flush_scan *scan);
#endif

/* Main flush algorithm.  Note on abbreviation: "squeeze and allocate" == "squalloc". */
/*static*/ int       squalloc_right_neighbor      (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig          (znode *left, znode *right, flush_position *pos);
static int           squalloc_right_twig_cut      (coord_t * to, reiser4_key * to_key, znode *left);
static int           squeeze_right_non_twig       (znode *left, znode *right);
static int           shift_one_internal_unit      (znode *left, znode *right);
static int           flush_squeeze_left_edge      (flush_position *pos);
static int           flush_squalloc_right         (flush_position *pos);

/* Flush allocate, relocate, write-queueing functions: */
static int           flush_query_relocate_dirty   (jnode *node, const coord_t *parent_coord, flush_position *pos);
static int           flush_allocate_znode         (znode *node, coord_t *parent_coord, flush_position *pos);
static int           flush_rewrite_jnode          (jnode *node);
static int           flush_queue_jnode            (jnode *node, flush_position *pos);
static int           flush_empty_queue            (flush_position *pos);

/* Flush helper functions: */
static int           jnode_lock_parent_coord      (jnode *node, coord_t *coord, lock_handle *parent_lh, data_handle *parent_zh, znode_lock_mode mode);
static int           znode_get_utmost_if_dirty    (znode *node, lock_handle *right_lock, sideof side, znode_lock_mode mode);
static int           znode_same_parents           (znode *a, znode *b);

/* Flush position functions */
static int           flush_pos_init               (flush_position *pos, int *nr_to_flush);
static int           flush_pos_valid              (flush_position *pos);
static void          flush_pos_done               (flush_position *pos);
static int           flush_pos_stop               (flush_position *pos);
static int           flush_pos_unformatted        (flush_position *pos);
static int           flush_pos_to_child_and_alloc (flush_position *pos);
static int           flush_pos_to_parent          (flush_position *pos);
static int           flush_pos_set_point          (flush_position *pos, jnode *node);
static void          flush_pos_release_point      (flush_position *pos);
static int           flush_pos_lock_parent        (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, data_handle *parent_load, znode_lock_mode mode);

/* Flush debug functions */
static const char*   flush_pos_tostring           (flush_position *pos);
static const char*   flush_jnode_tostring         (jnode *node);
static const char*   flush_znode_tostring         (znode *node);
static const char*   flush_flags_tostring         (int flags);

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

/* B. There is an issue described in flush_query_relocate_check having to do with an
 * imprecise is_preceder? check having to do with partially-dirty extents.  The code that
 * sets preceder hints and computes the preceder is basically untested.  Careful testing
 * needs to be done that preceder calculations are done correction, since it doesn't
 * effect correctness we will not catch this stuff during regular testing. */

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

/* D. Atomicity of flush against deletion.  Suppose a created block is assigned a block
 * number then early-flushed to disk.  It is dirtied again and flush is called again.
 * Concurrently, that block is deleted, and the de-allocation of its block number does not
 * need to be deferred, since it is not part of the preserve set (i.e., it didn't exist
 * before the transaction).  I think there may be a race condition where flush writes the
 * dirty, created block after the non-deferred deallocated block number is re-allocated,
 * making it possible to write deleted data on top of non-deleted data.  Its just a
 * theory, but it needs to be thought out. */

/* E. Never put JNODE_WANDER blocks in the flush queue.  This is easy to implement, but it
 * is done for historical reasons related to the time when we had no log-writing and the
 * test layout.  If (WRITE_LOG == 0) then wandered blocks in the flush queue makes sense
 * (and the test layout doesn't support WRITE_LOG, I think?), but once (WRITE_LOG == 1)
 * placing wandered blocks in the flush queue can only cause more BIO objects to be
 * allocated than might otherwise be required.  But this is a minor issue. */

/* F. bio_alloc() failure is not handled gracefully. */

/* G. Unallocated children. */

/* H. Add a WANDERED_LIST to the atom to clarify the placement of wandered blocks. */


/* NEW STUFF: */
/* FIXME: Rename flush-scan to scan-point, (flush-pos to flush-point?) */
/* FIXME: Zam wants to optimize relocate block allocation for large allocations. */

/********************************************************************************
 * JNODE_FLUSH: MAIN ENTRY POINT
 ********************************************************************************/

/* This is the main entry point for flushing a jnode, called by the transaction manager
 * when an atom closes (to commit writes) and called by the VM under memory pressure (via
 * page_cache.c:page_common_writeback() to early-flush dirty blocks).



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
int jnode_flush (jnode *node, int *nr_to_flush, int flags)
{
	int ret = 0;
	flush_position flush_pos;
	flush_scan right_scan;
	flush_scan left_scan;
	struct reiser4_io_handle hio;

	assert ("jmacd-76619", lock_stack_isclean (get_current_lock_stack ()));

	if (flags & JNODE_FLUSH_COMMIT) {
		init_io_handle (&hio);
		flush_pos.hio = &hio;
	} else {
		flush_pos.hio = NULL;
	}

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
	 * again and jnode_is_allocated returns true.  At this point we simply re-submit
	 * the block to disk using the previously decided location. */
	if (jnode_is_allocated (node)) {

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

	/*trace_if (TRACE_FLUSH_VERB,*/ print_tree_rec ("parent_first", current_tree, REISER4_TREE_BRIEF); /*);*/
	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_CHECK));*/

	/* First scan left and remember the leftmost scan position.  If the leftmost
	 * position is unformatted we remember its parent_coord.  If RAPID_SCAN is
	 * enabled, the scan should be limited by FLUSH_RELOCATE_THRESHOLD since the rapid
	 * scan will continue without precisely counting nodes.  If RAPID_SCAN is
	 * disabled, then we scan up until counting FLUSH_SCAN_MAXNODES. */
	if ((ret = flush_scan_left (& left_scan, & right_scan, node,
				    USE_RAPID_SCAN ? FLUSH_RELOCATE_THRESHOLD : FLUSH_SCAN_MAXNODES))) {
		goto failed;
	}

	/* Then possibly go right to decide if we will use a policy of relocating leaves.
	 * This is only done if we did not scan past (and count) enough nodes during the
	 * leftward scan.  If we do scan right, we only care to go far enough to establish
	 * that at least FLUSH_RELOCATE_THRESHOLD number of nodes are being flushed.  The
	 * scan limit is the difference between left_scan.count and the threshold. */
	if ((left_scan.count < FLUSH_RELOCATE_THRESHOLD) &&
	    (ret = flush_scan_right (& right_scan, node, FLUSH_RELOCATE_THRESHOLD - left_scan.count))) {
		goto failed;
	}

	/* Only the right-scan count is needed, release any rightward locks right away. */
	flush_scan_done (& right_scan);

	/* ... and the answer is: we should relocate leaf nodes if at least
	 * FLUSH_RELOCATE_THRESHOLD nodes were found. */
	flush_pos.leaf_relocate = (left_scan.count + right_scan.count >= FLUSH_RELOCATE_THRESHOLD);

	/*assert ("jmacd-6218", jnode_check_dirty (left_scan.node));*/

	/* Funny business here.  We set an unformatted point at the left-end of the scan,
	 * but after that an unformatted flush position sets pos->point to NULL.  This

	 FIXME: HANS DOESNT UNDERSTAND "SETS"
	 
	 * seems lazy, but it makes the initial calls to flush_query_relocate much easier
	 * because we know the first unformatted child already.  Nothing is broken by
	 * this, but the reasoning is subtle.  Holding an extra reference on a jnode
	 * during flush can cause us to see nodes with HEARD_BANSHEE during squalloc,
	 * which is not good, but this only happens on the left-edge of flush, where nodes
	 * cannot be deleted.  So if nothing is broken, why fix it? */
	if ((ret = flush_pos_set_point (& flush_pos, left_scan.node))) {
		goto failed;
	}

	/* Now setup flush_pos using scan_left's endpoint. */
	if (jnode_is_unformatted (left_scan.node)) {
		coord_dup (& flush_pos.parent_coord, & left_scan.parent_coord);
		move_lh (& flush_pos.parent_lock, & left_scan.parent_lock);
		move_dh (& flush_pos.parent_load, & left_scan.parent_load);
	} else {
		move_lh (& flush_pos.point_lock, & left_scan.node_lock);
	}

	/* In some cases, we discover the parent-first preceder during the
	 * leftward scan.  Copy it. */
	flush_pos.preceder.blk = left_scan.preceder_blk;
	flush_scan_done (& left_scan);

	/* At this point, try squeezing at the "left edge", meaning to possibly
	 * change the parent of the left end of the scan.  NOT IMPLEMENTED FUTURE
	 * OPTIMIZATION -- see the comment in flush_squeeze_left_edge. */
	if ((ret = flush_squeeze_left_edge (& flush_pos))) {
		goto failed;
	}

	/* Do the rightward-bottom-up pass. */
	if ((ret = flush_squalloc_right (& flush_pos))) {
		goto failed;
	}

	/* Write anything left in the queue. */
	ret = flush_empty_queue (& flush_pos);

	/*trace_if (TRACE_FLUSH_VERB, print_tree_rec ("parent_first", current_tree, REISER4_TREE_CHECK));*/

	/* Any failure reaches this point. */
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

	if (ret == -EINVAL || ret == -EDEADLK || ret == -ENAVAIL || ret == -ENOENT
								 /* FIXME(C): ENOENT maybe
								  * commented out because
								  * find_child_ptr returns
								  * -ENOENT when there is
								  * no matching child ptr,
								  * corruption? */) {
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

	/* Wait for io completion.
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
	if (flags & JNODE_FLUSH_COMMIT) {
		int rc = done_io_handle(&hio);
		if (rc && ret == 0) {
			warning ("nikita-2421",
				 "Error waiting for io completion: %i", rc);
			ret = rc;
		}
	}

	ON_DEBUG (atomic_dec (& flush_cnt));

	return ret;
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

	/* First rule: If the block is less than FLUSH_RELOCATE_DISTANCE blocks away from
	 * its preceder block, do not relocate. */
	if (dist <= FLUSH_RELOCATE_DISTANCE) {
		return 0;
	}

	return 1;
}

/* This function is a predicate that tests for relocation.  Always called in the
 * reverse-parent-first context, when we are asking either:
 *
 * 1. Whether the leftmost child of a node should be relocated, using its parent as the
 * preceder.  If we decide yes (returning 1), we will dirty the parent level and
 * proceeding to flush that level.
 *
 * 2. Whether a non-leftmost child of a node should be relocated, using its preceder on
 * the leaf-level of the subtree-to-the-right.  I
 *
 * When traversing in the
 * forward-parent-first direction, relocation decisions are handled in two places, but not
 * here: flush_allocate_znode() and extent_needs_allocation(). */
static int flush_query_relocate_check (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	reiser4_block_nr pblk = 0;
	reiser4_block_nr nblk = 0;

	assert ("jmacd-8989", ! jnode_is_root (node));

	/* New nodes are treated as if they are being relocated. */
	if (jnode_created (node) || (pos->leaf_relocate && jnode_get_level (node) == LEAF_LEVEL)) {
		return 1;
	}

	/* Find the preceder.  FIXME(B): When the child is an unformatted, previously
	 * allocated node, the coord may be leftmost even though the child is not the
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

	return flush_relocate_unless_close_enough (& pblk, & nblk);
}

/* This function calls flush_query_relocate_check to make a reverse-parent-first
 * relocation decision and then, if yes, it marks the parent dirty. */
static int flush_query_relocate_dirty (jnode *node, const coord_t *parent_coord, flush_position *pos)
{
	int ret;

	if (! znode_check_dirty (parent_coord->node)) {

		if ((ret = flush_query_relocate_check (node, parent_coord, pos)) < 0) {
			return ret;
		}

		if (ret == 1) {
			assert ("jmacd-18923", znode_is_write_locked (parent_coord->node));
			znode_set_dirty (parent_coord->node);
		}
	}

	return 0;
}

/* This function either continues or stops the squeeze-and-allocate procedure when it
 * reaches the end of the twig level.  This case is special because the first child of the
 * next-rightward twig may need to be relocated even if the parent is clean.  This only
 * performs part of the task: if the right neighbor is dirty then we continue with the
 * (upward-recursive) squalloc_changed_ancestors step regardless of the first child.  If
 * the first child is clean then we stop.  If the first child is dirty, then see if it
 * should be relocated and possibly dirty the parent. */
static int flush_right_relocate_end_of_twig (flush_position *pos)
{
	int ret;
	jnode *child = NULL;
	lock_handle right_lock;
	data_handle right_load;
	coord_t right_coord;

	init_lh (& right_lock);
	init_dh (& right_load);

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

	if ((ret = load_dh_znode (& right_load, right_lock.node))) {
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
	if ((ret = flush_query_relocate_dirty (child, & right_coord, pos))) {
		goto exit;
	}

	/* If the child is relocated it will be handled by squalloc_changed_ancestor,
	 * which also handles pos_to_child. */

 exit:
	if (child != NULL) { jput (child); }
	done_lh (& right_lock);
	done_dh (& right_load);
	return ret;
}

/********************************************************************************
 * Flush Squeeze and Allocate
 ********************************************************************************/

/* flush_squeeze_left_edge -- Here the rule is: squeeze left uncle with parent if uncle is
 * dirty.  Repeat until the child's parent is stable.  If the child is a leftmost child,
 * repeat this left-edge squeezing operation at the next level up.  AS YET UNIMPLEMENTED
 * in the interest of reducing time-to-benchmark.  Also note that we cannot allocate
 * during the left-squeeze and we have to maintain coordinates throughout or else repeat a
 * tree search.  Difficult. */
static int flush_squeeze_left_edge (flush_position *pos UNUSED_ARG)
{
	return 0;
}

/* flush_set_preceder -- This is one of two places where we may set the preceding block
 * number.  It is called during flush_allocate_ancestors, the upward-recursive step the
 * calls flush_query_relocate() as long as each child is a leftmost child.  At the
 * twig-level, this routine is called to remember the rightmost child of the left twig.
 * That child (if it is found in memory), is the preceder of the highest non-leftmost
 * child that we encounter during flush_allocated_ancestors.  It helps to draw a
 * picture...
 */
static int flush_set_preceder (const coord_t *coord_in, flush_position *pos)
{
	int ret;
	coord_t coord;
	lock_handle left_lock;

	coord_dup (& coord, coord_in);

	init_lh (& left_lock);

	/* FIXME(B): Same FIXME as in "Find the preceder" in flush_query_relocate_check.
	 * coord_is_leftmost_unit is not the right test if the unformatted child is in the
	 * middle of the first extent unit. */
	if (! coord_is_leftmost_unit (& coord)) {
		coord_prev_unit (& coord);
	} else {
		if ((ret = reiser4_get_left_neighbor (& left_lock, coord.node, ZNODE_READ_LOCK, 0))) {
			/* FIXME(C): check EINVAL, EDEADLK */
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
	data_handle aload;
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
	if (znode_check_allocated (coord->node) || ! coord_is_leftmost_unit (coord)) {
		return 0;
	}

	init_lh (& alock);
	init_dh (& aload);
	coord_init_invalid (& acoord, NULL);

	/* Only ascend to the next level if it is a leftmost child, but write-lock the
	 * parent in case we will relocate the child. */
	if (! znode_is_root (coord->node)) {

		if ((ret = jnode_lock_parent_coord (ZJNODE (coord->node), & acoord, & alock, & aload, ZNODE_WRITE_LOCK))) {
			/* FIXME(C): check EINVAL, EDEADLK */
			goto exit;
		}

		if ((ret = flush_query_relocate_dirty (ZJNODE (coord->node), & acoord, pos))) {
			goto exit;
		}

		/* Recursive call. */
		if (! znode_check_allocated (acoord.node) &&
		    (ret = flush_alloc_one_ancestor (& acoord, pos))) {
			goto exit;
		}
	}

	/* Note: we call allocate with the parent write-locked (except at the root) in
	 * case we relocate the child, in which case it will modify the parent during this
	 * call. */
	ret = flush_allocate_znode (coord->node, & acoord, pos);

 exit:
	done_dh (& aload);
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
	data_handle pload;
	coord_t pcoord;

	coord_init_invalid (& pcoord, NULL);
	init_lh (& plock);
	init_dh (& pload);

	if (flush_pos_unformatted (pos) || ! znode_is_root (JZNODE (pos->point))) {
		/* Lock the parent (it may already be locked, thus the special case). */
		if ((ret = flush_pos_lock_parent (pos, & pcoord, & plock, & pload, ZNODE_WRITE_LOCK))) {
			goto exit;
		}

		/* It may not be dirty, in which case we should decide whether to relocate the
		 * child now. */
		if ((ret = flush_query_relocate_dirty (pos->point, & pcoord, pos))) {
			goto exit;
		}

		ret = flush_alloc_one_ancestor (& pcoord, pos);
	}

	/* If we are at a formatted node, allocate it now. */
	if (ret == 0 && ! flush_pos_unformatted (pos)) {
		ret = flush_allocate_znode (JZNODE (pos->point), & pcoord, pos);
	}

 exit:
	done_dh (& pload);
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
	data_handle right_load;
	data_handle parent_load;
	data_handle node_load;
	coord_t at_right, right_parent_coord;
	ON_STATS (int squeeze_counted = 1;);

	init_lh (& right_lock);
	init_lh (& parent_lock);
	init_dh (& right_load);
	init_dh (& parent_load);
	init_dh (& node_load);

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] %s\n", call_depth, flush_znode_tostring (node));

	/* Originally the idea was to assert that a node is always allocated before the
	 * upward recursion here, but its not always true.  We are allocating in the
	 * rightward direction and there is no reason the initial (leftmost) ancestors
	 * must be allocated.  They are not considered part of this parent-first traversal. */
	/*assert ("jmacd-9925", znode_check_allocated (node));*/

	if ((ret = load_dh_znode (& node_load, node))) {
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

	if ((ret = load_dh_znode (& right_load, right_lock.node))) {
		warning ("jmacd-61426", "zload failed: %d", ret);
		goto exit;
	}

	coord_init_after_last_item (& at_right, node);

	assert ("jmacd-7866", ! node_is_empty (right_lock.node));

	trace_on (TRACE_FLUSH_VERB, "sq1_ca[%u] before right neighbor %s\n", call_depth, flush_znode_tostring (right_lock.node));

	/* We found the right znode (and locked it), now squeeze from right into
	 * current node position. */
	ON_STATS (if (squeeze_counted) { squeeze_counted = 0; reiser4_stat_flush_add (flush_squeeze); );
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
		done_dh (& right_load);
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
			/* FIXME(C): check ENAVAIL, EINVAL, EDEADLK */
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

	/* No reason to hold onto the node data now, can release it early.  Okay to call
	 * done_dh twice. */
	done_dh (& node_load);

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
			/* FIXME(C): check EINVAL, EDEADLK */
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
	done_dh (& node_load);
	done_dh (& right_load);
	done_dh (& parent_load);
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
				trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (ENAVAIL, ancestors allocated): %s\n", flush_pos_tostring (pos));
				ret = flush_pos_stop (pos);
			} else {
				warning ("jmacd-61433", "znode_get_if_dirty failed: %d", ret);
			}
			goto exit;
		}

		trace_on (TRACE_FLUSH_VERB, "sq_rca no right at leaf, to parent: %s\n", flush_pos_tostring (pos));

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
			trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (at twig): %s\n", flush_pos_tostring (pos));
			ret = flush_pos_stop (pos);
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

	trace_on (TRACE_FLUSH_VERB, "sq_rca ready to move right %s\n", flush_znode_tostring (right_lock.node));

	/* We have a new right and it should have been allocated by the call to
	 * flush_squalloc_one_changed_ancestor.  However, a concurrent thread could
	 * possibly insert a new node, so just stop if ! allocated. */
	if (! jnode_check_allocated (ZJNODE (right_lock.node))) {
		trace_on (TRACE_FLUSH_VERB, "sq_rca: STOP (right not allocated): %s\n", flush_pos_tostring (pos));
		ret = flush_pos_stop (pos);
		goto exit;
	}

	if (is_unformatted) {
		done_dh (& pos->parent_load);
		done_lh (& pos->parent_lock);
		move_lh (& pos->parent_lock, & right_lock);
		if ((ret = load_dh_znode (& pos->parent_load, pos->parent_lock.node))) {
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

	if (jnode_check_allocated (pos->point)) {
		trace_on (TRACE_FLUSH_VERB, "flush concurrency: %s already allocated\n", flush_pos_tostring (pos));
		return 0;
	}

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
				ret = 0;
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
	assert ("nikita-2435", znode_is_write_locked (left));
	assert ("nikita-2436", znode_is_write_locked (right));

	if (REISER4_DEBUG) {
		spin_lock_tree (current_tree);
		assert ("nikita-2434", left->right == right);
		spin_unlock_tree (current_tree);
	}

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

/* FIXME: comment */
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

		spin_lock_tree (current_tree);
		current_tree->root_block = blk;
		spin_unlock_tree (current_tree);

		znode_set_dirty(fake);

		zput (fake);
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

	/* We have the node write-locked and should have checked for ! allocated()
	 * somewhere before reaching this point. */
	assert ("jmacd-7987", ! jnode_check_allocated (ZJNODE (node)));
	assert ("jmacd-7988", znode_is_write_locked (node));
	assert ("jmacd-7989", coord_is_invalid (parent_coord) || znode_is_write_locked (parent_coord->node));

	reiser4_stat_flush_add (flush_zalloc);

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
			pos->preceder.max_dist = min ((reiser4_block_nr) FLUSH_RELOCATE_DISTANCE, dist);
			pos->preceder.level    = znode_get_level (node);

			if ((ret = flush_allocate_znode_update (node, parent_coord, pos)) && (ret != -ENOSPC)) {
				return ret;
			}

			if (ret == 0) {
				/* Got a better allocation. */
				jnode_set_reloc (ZJNODE (node));
			} else if (dist < FLUSH_RELOCATE_DISTANCE) {
				/* The present allocation is good enough. */
				jnode_set_wander (ZJNODE (node));
			} else {
				/* Otherwise, try to relocate to the best position. */
			best_reloc:
				jnode_set_reloc (ZJNODE (node));
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

	spin_lock_znode (node);

	assert ("jmacd-4277", ! blocknr_is_fake (& pos->preceder.blk));

	trace_on (TRACE_FLUSH, "alloc: %s\n", flush_znode_tostring (node));

	/* Queue it now, releases lock. */
	return flush_queue_jnode (ZJNODE (node), pos);
}

/* FIXME: comment */
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
	assert ("jmacd-1771", jnode_is_allocated (node));

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

/* take jnode from flush queue and put it on clean_nodes list */
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
	int ret = 0;
	jnode * node;
	int max_queue_len;
	struct super_block *super;

	trace_on (TRACE_FLUSH, "flush_empty_queue with %u queued\n", pos->queue_num);

	if (pos->queue_num == 0) {
		return 0;
	}

	super = reiser4_get_current_sb (); /* cpage->mapping->host->i_sb; */

	/* We can safely traverse this flush queue without locking of atom and nodes
	 * because only this thread can add nodes to it and all already queued nodes are
	 * protected from moving out by JNODE_FLUSH_QUEUED bit */
	node = capture_list_front (&pos->queue);

	assert ("jmacd-71972", !capture_list_end (&pos->queue, node));

#if REISER4_USER_LEVEL_SIMULATION == 0	/* FIXME: Eliminate #ifdefs */
	max_queue_len = (bdev_get_queue (super->s_bdev)->max_sectors >> (super->s_blocksize_bits - 9));
#else
	max_queue_len = pos->queue_num;
#endif

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

		assert ("jmacd-71236", jnode_check_allocated (check));

		/* FIXME(E): JMACD->ZAM: I think that WANDER nodes should never be put in the
		 * queue at all, they should simply be ignored by jnode_flush_queue or
		 * something similar.  Then we don't need this special case here or below. */
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

		/* PageWriteback situation should be impossible with the current flush
		 * queue implementation. */
		assert ("jmacd-78200", ! PageWriteback (cpage));

		{
			/* Find consecutive nodes. */
			struct bio *bio;
			int nr = 1, i;
			jnode *prev = check;
			int blksz;

			super = cpage->mapping->host->i_sb;
			assert( "jmacd-2029", super != NULL );

			for (; ! capture_list_end (&pos->queue, node) && nr < max_queue_len; nr += 1, prev = node, node = capture_list_next (node)) {
				struct page *npage;

				npage = jnode_lock_page (node);
				spin_unlock_jnode (node);

				if ((WRITE_LOG && JF_ISSET (node, JNODE_WANDER)) /* FIXME(E): */ ||
				    (*jnode_get_block (node) != *jnode_get_block (prev) + 1) ||
				    PageWriteback (npage) /* FIXME: JMACD->ZAM, is this PageWriteback check needed? */) {
					unlock_page (npage);
					break;
				}
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

				trace_on (TRACE_BUG, "submitted: %li, %lu\n",
					  pg->mapping->host->i_ino, pg->index);
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

			pos->enqueue_cnt += nr;

			trace_on (TRACE_FLUSH_VERB, "\n");
			trace_on (TRACE_FLUSH, "flush_empty_queue %u consecutive blocks: BIO %p\n", nr, bio);

			io_handle_add_bio (pos->hio, bio);

			/* FIXME(B): JMACD->ZAM: 'check' is not the last written location,
			 * bio->bi_vec[i] is? */
			reiser4_update_last_written_location (super, jnode_get_block (check));

			submit_bio (WRITE, bio);
		}
		jrelse (check);
	}

	blk_run_queues ();
	trace_if (TRACE_FLUSH, if (ret == 0) { info ("flush_empty_queue length %u\n", pos->queue_num); });

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
	assert ("jmacd-53316", jnode_is_allocated (node));

	/* If this node is being wandered, just set it clean and return. */
	if ((WRITE_LOG && JF_ISSET (node, JNODE_WANDER))) {
		spin_unlock_jnode (node);
		jnode_set_clean (node);
		return 0;
	}

	/* FIXME: JMACD->NIKTA: This spinlock does very little.  Why?  Races are
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
				    data_handle *parent_zh,
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

		if ((ret = load_dh_znode (parent_zh, parent_lh->node))) {
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

			if ((ret = load_dh_znode (parent_zh, parent_lh->node))) {
				warning ("jmacd-976812", "load_dh_znode failed: %d", ret);
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

	if ((ret = reiser4_get_neighbor (lock, node, mode, side == LEFT_SIDE ? GN_GO_LEFT : 0))) {
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
	int x;

	assert ("jmacd-7011", znode_is_write_locked (a));
	assert ("jmacd-7012", znode_is_write_locked (b));

	spin_lock_tree (current_tree);
	x = (a->ptr_in_parent_hint.node == b->ptr_in_parent_hint.node);
	spin_unlock_tree (current_tree);
	return x;
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
	init_dh (& scan->parent_load);
	init_dh (& scan->node_load);
	coord_init_invalid (& scan->parent_coord, NULL);
}

/* Release any resources held by the flush scan, e.g., release locks, free memory, etc. */
static void flush_scan_done (flush_scan *scan)
{
	done_dh (& scan->node_load);
	if (scan->node != NULL) {
		jput (scan->node);
		scan->node = NULL;
	}
	done_dh (& scan->parent_load);
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
	done_dh (& scan->node_load);

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

	/* Failure may happen at the load_dh call, but the caller can assume the reference
	 * is safely taken. */
	return load_dh_jnode (& scan->node_load, node);
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

	/* Now continue with rapid scan. */
#if USE_RAPID_SCAN
	return flush_scan_rapid (scan);
#else
	return 0;
#endif
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
			copy_dh (& other->parent_load, & scan->parent_load);
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
	data_handle next_load;
	coord_t next_coord;
	jnode *child;

	init_lh (& next_lock);
	init_dh (& next_load);

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

			if ((ret = load_dh_znode (& next_load, next_lock.node))) {
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
			move_dh (& scan->parent_load, & next_load);
		}

		assert ("jmacd-1239", item_is_extent (& scan->parent_coord));
	}

	assert ("jmacd-6233", flush_scan_finished (scan) || jnode_is_znode (scan->node));
 exit:
	if (jnode_is_znode (scan->node)) {
		done_lh (& scan->parent_lock);
		done_dh (& scan->parent_load);
	}

	done_dh (& next_load);
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

		assert ("jmacd-3551", ! jnode_check_allocated (neighbor) && txn_same_atom_dirty (neighbor, scan->node, 0, 0));

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

#if 0
/* After performing the block-by-block scan, we continue skipping past the non-leftmost,
 * non-rightmost interior children of the parent.  This is only called in the left-scan
 * direction, and the code assumes that direction.  It will be easy to change if necessary
 * so that rapid scan can go in either direction, but it is currently not necessary.
 */
static int flush_scan_rapid (flush_scan *scan)
{
	data_handle lt_data;	/* lt == left twig */
	lock_handle lt_lock;
	coord_t     lt_coord;
	int         is_dirty;
	int         ret;

	/* If the current scan position is formatted, get the parent coordinate so we can
	 * test leftmost/rightmost.  Otherwise, we're already set.  Also, if we are at a
	 * formatted position we already have scan->point_lock holding a lock on
	 * scan->node.  We have to maintain that lock in addition to the parent locks as
	 * we continue with the rapid scan. */
	if (jnode_is_znode (scan->node)) {
		if ((ret = jnode_lock_parent_coord (scan->node, & scan->parent_coord, & scan->parent_lock, & scan->parent_load, ZNODE_WRITE_LOCK))) {
			return ret;
		}

		/* We release point_lock and reacquire it later. */
		done_lh (& scan->node_lock);
	}

	/* If the current node is not the leftmost child, set parent_coord to the leftmost
	 * dirty child of its parent. */
	if (! coord_is_leftmost_unit (& scan->parent_coord)) {

		/* The call to flush_scan_leftmost_dirty_unit re-initializes
		 * scan->parent_coord to the leftmost _unit_ that has dirty children.
		 * This also resets the scan->node properly. */
		if ((ret = flush_scan_leftmost_dirty_unit (scan))) {
			return ret;
		}
	}

	init_lh (& lt_lock);
	init_dh (& lt_data);

	/* Now we loop as long as we are positioned at the leftmost child of the parent.
	 * In this case, we check the left neighbor to see if it is dirty.  If the left
	 * neighbor is dirty then we repeat: set parent_coord to the leftmost unit that
	 * has dirty children... */
	while (coord_is_leftmost_unit (& scan->parent_coord)) {

		/* There is an optimization possible here because there are two ways to
		 * obtain the left neighbor.  If scan->node is a znode we can go left by
		 * checking its ->left sibling pointer, but this is not general in case of
		 * unformatted nodes.  Only the general case is implemented: get the left
		 * twig and access its rightmost child.  Check if the rightmost child is
		 * dirty.  If not, stop here.
		 *
		 * If the rightmost child of the left twig is dirty, then set
		 * scan->parent_coord to the left twig and again find the leftmost unit
		 * with a dirty child.
		 *
		 * Finally, repeat this loop.
		 */
		if ((ret = reiser4_get_left_neighbor (& lt_lock, scan->parent_coord.node, ZNODE_WRITE_LOCK, 0))) {
			/* We get NAVAIL or NOENT if the left twig is not in memory, in
			 * which case stop the rapid scan. */
			if (ret == -ENAVAIL || ret == -ENOENT) {
				ret = 0;
			}
			goto fail;
		}

		/* Load the left twig, check if its rightmost child is dirty. */
		if ((ret = load_dh_znode (& lt_data, lt_lock.node))) {
			goto fail;
		}

		coord_init_last_unit (& lt_coord, lt_lock.node);

		if ((ret = item_utmost_child_dirty (& lt_coord, RIGHT_SIDE, & is_dirty))) {
			goto fail;
		}

		/* If the rightmost child is not dirty, stop where we are. */
		if (! is_dirty) {
			break;
		}

		/* Rightmost child of left twig is dirty, now continue at that twig. */
		move_dh (& scan->parent_load, & lt_data);
		move_lh (& scan->parent_lock, & lt_lock);

		/* Set coordinate to the leftmost dirty unit.  This also resets scan->node
		 * properly. */
		if ((ret = flush_scan_leftmost_dirty_unit (scan))) {
			goto fail;
		}
	}

	ret = 0;
 fail:
	done_dh (& lt_data);
	done_lh (& lt_lock);

	/* Finally, if we are returning success and the position is formatted then we
	 * should reacquire the scan->point_lock, which gets moved into
	 * flush_pos.point_lock. */
	if (ret == 0 && jnode_is_znode (scan->node)) {

		assert ("jmacd-76620", lock_stack_isclean (get_current_lock_stack ()));

		ret = longterm_lock_znode (& scan->node_lock, JZNODE (scan->node), ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI);
	}

	return ret;
}

/* During flush_scan_rapid we need to find the leftmost dirty child/unit of a parent.
 * This routine finds that node and sets scan->node, scan->parent_coord appropriately.
 * The structure of this code resembles the flush_scan_formatted and
 * flush_scan_extent_coord functions, except the logic is inverted: we want to scan past
 * clean nodes and stop at the first dirty node in this pass.  We disregard the atom here
 * and allow children in different atoms to fuse later during flush. */
static int flush_scan_leftmost_dirty_unit (flush_scan *scan)
{
	int ret;

	/* FIXME(G): UNIMPLEMENTED: The whole idea of leftmost_dirty_unit may be wrong, and
	 * this leads to a possible simplification which is not being pursued since
	 * rapid_scan is not going to be used right away.  The idea here was to find the
	 * first dirty node, but why do that?  If flush can support twig-positions for
	 * clean nodes then calling coord_init_first_item should be sufficient.  Would
	 * require simple changes to flush_scan_rapid, but the trick is fixing
	 * jnode_flush() to handle it. */

	/* Starting from the leftmost unit... */
	coord_init_before_first_item (& scan->parent_coord, scan->parent_coord.node);

	while (coord_next_unit (& scan->parent_coord) == 0) {

		/* Handle items by type: */
		if (item_is_internal (& scan->parent_coord)) {

			/* The internal item case is simple, check the only child and do
			 * stop if it is dirty. */
			jnode *child;

			if ((ret = item_utmost_child (& scan->parent_coord, LEFT_SIDE, & child))) {
				return ret;
			}

			/* Just check if it is dirty, not the atom. */
			if (! jnode_check_dirty (child)) {
				jput (child);
				continue;
			}

			ret = flush_scan_set_current (scan, child, 1 /* add_count is irrelevant at this point */, NULL);
			break;

		} else {
			assert ("jmacd-81900", item_is_extent (& scan->parent_coord));

			/* ... */
		}
	} while (1);

	return ret;
}
#endif

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
	pos->enqueue_cnt = 0;
	pos->nr_to_flush = nr_to_flush;

	coord_init_invalid (& pos->parent_coord, NULL);

	blocknr_hint_init (& pos->preceder);
	init_lh (& pos->point_lock);
	init_lh (& pos->parent_lock);
	init_dh (& pos->parent_load);
	init_dh (& pos->point_load);

	return 0;
}

/* The flush loop inside flush_squalloc_right periodically checks flush_pos_valid to
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
	if (pos->nr_to_flush != NULL && pos->enqueue_cnt >= *pos->nr_to_flush) {
		return 0;
	}
	return pos->point != NULL || lock_mode (& pos->parent_lock) != ZNODE_NO_LOCK;
}

/* return jnode back to atom's lists */
static void invalidate_flush_queue (struct flush_position * pos)
{
	if (capture_list_empty(&pos->queue)) return;


	while (1) {
		jnode * cur = capture_list_pop_front (&pos->queue);
		txn_atom * atom;

		spin_lock_jnode (cur);
		atom = atom_get_locked_by_jnode (cur);

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

/* Release any resources of a flush_position. */
static void flush_pos_done (flush_position *pos)
{
	invalidate_flush_queue (pos);
	flush_pos_stop (pos);
	blocknr_hint_done (& pos->preceder);
}

/* Reset the point and parent. */
static int flush_pos_stop (flush_position *pos)
{
	done_dh (& pos->parent_load);
	done_dh (& pos->point_load);
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

	if (! jnode_check_dirty (child)) {
		trace_on (TRACE_FLUSH_VERB, "fpos_to_child_alloc: STOP (not dirty): %s\n", flush_pos_tostring (pos));
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

	if (! jnode_check_allocated (child) && (ret = flush_allocate_znode (JZNODE (child), & pos->parent_coord, pos))) {
		return ret;
	}

	if (0) {
	stop:
		ret = flush_pos_stop (pos);
		return ret;
	}

	/* And keep going... */
	done_dh (& pos->parent_load);
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
		/* FIXME(C): check EINVAL, EDEADLK */
		return ret;
	}

	/* When this is called, we have already tried the sibling link of the znode in
	 * question, therefore we are not interested in saving ->point. */
	done_dh (& pos->point_load);
	done_lh (& pos->point_lock);

	/* Note: we leave the point set, but unlocked/unloaded. */
	/* This is a bad idea if the child can be deleted.... but it helps the call to
	 * left_relocate.  Needs a better solution.  Related to "Funny business" comment
	 * above. */
	return 0;
}

static int flush_pos_unformatted (flush_position *pos)
{
	return pos->parent_lock.node != NULL;
}

static void flush_pos_release_point (flush_position *pos)
{
	if (pos->point != NULL) {
		jput (pos->point);
		pos->point = NULL;
	}
	done_dh (& pos->point_load);
	done_lh (& pos->point_lock);
}

static int flush_pos_set_point (flush_position *pos, jnode *node)
{
	flush_pos_release_point (pos);
	pos->point = jref (node);
	return load_dh_jnode (& pos->point_load, node);
}

static int flush_pos_lock_parent (flush_position *pos, coord_t *parent_coord, lock_handle *parent_lock, data_handle *parent_load, znode_lock_mode mode)
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
		if ((ret = load_dh_znode (parent_load, parent_lock->node))) {
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

reiser4_blocknr_hint* flush_pos_hint (flush_position *pos)
{
	return & pos->preceder;
}

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
			spin_lock_jnode (scan);
			scan->atom = large;
			spin_unlock_jnode (scan);
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
	data_handle load;
	fmtbuf[0] = 0;

	init_dh (& load);

	if (pos->parent_lock.node != NULL) {

		assert ("jmacd-79123", pos->parent_lock.node == pos->parent_load.node);

		strcat (fmtbuf, "par:");
		flush_jnode_tostring_internal (ZJNODE (pos->parent_lock.node), fmtbuf);

		if (load_dh_znode (& load, pos->parent_lock.node)) {
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

	done_dh (& load);
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
