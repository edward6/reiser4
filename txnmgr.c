/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* The txnmgr is a set of interfaces that keep track of atoms and transcrash handles.  The
   txnmgr processes capture_block requests and manages the relationship between jnodes and
   atoms through the various stages of a transcrash, and it also oversees the fusion and
   capture-on-copy processes.  The main difficulty with this task is maintaining a
   deadlock-free lock ordering between atoms and jnodes/handles.  The reason for the
   difficulty is that jnodes, handles, and atoms contain pointer circles, and the cycle
   must be broken.  The main requirement is that atom-fusion be deadlock free, so once you
   hold the atom_lock you may then wait to acquire any jnode or handle lock.  This implies
   that any time you check the atom-pointer of a jnode or handle and then try to lock that
   atom, you must use trylock() and possibly reverse the order.
  
   This code implements the design documented at:
  
     http://namesys.com/txn-doc.html
*/

/* Thoughts on the external transaction interface:
  
   In the current code, a TRANSCRASH handle is created implicitely by REISER4_ENTRY and
   closed by REISER4_EXIT, occupying the scope of a single system call.  We wish to give
   certain applications an interface to begin and close (commit) transactions.  Since our
   implementation of transactions does not yet support isolation, allowing an application
   to open a transaction implies trusting it to later close the transaction.  Part of the
   transaction interface will be aimed at enabling that trust, but the interface for
   actually using transactions is fairly narrow.
  
   BEGIN_TRANSCRASH: Returns a transcrash identifier.  It should be possible to translate
   this identifier into a string that a shell-script could use, allowing you to start a
   transaction by issuing a command.  Once open, the transcrash should be set in the task
   structure, and there should be options (I suppose) to allow it to be carried across
   fork/exec.  A transcrash has several options:
  
     - READ_FUSING or WRITE_FUSING: The default policy is for txn-capture to capture only
     on writes (WRITE_FUSING) and allow "dirty reads".  If the application wishes to
     capture on reads as well, it should set READ_FUSING.
  
     - TIMEOUT: Since a non-isolated transcrash cannot be undone, every transcrash must
     eventually close (or else the machine must crash).  If the application dies an
     unexpected death with an open transcrash, for example, or if it hangs for a long
     duration, one solution (to avoid crashing the machine) is to simply close it anyway.
     This is a dangerous option, but it is one way to solve the problem until isolated
     transcrashes are available for untrusted applications.
  
   RESERVE_BLOCKS: A running transcrash should indicate to the transaction manager how
   many dirty blocks it expects.  The reserve_blocks interface should be called at a point
   where it is safe for the application to fail, because the system may not be able to
   grant the allocation and the application must be able to back-out.  For this reason,
   the number of reserve-blocks can also be passed as an argument to BEGIN_TRANSCRASH, but
   the application may also wish to extend the allocation after beginning its transcrash.
  
   CLOSE_TRANSCRASH: The application closes the transcrash when it is finished making
   modifications that require transaction protection.  When isolated transactions are
   supported the CLOSE operation is replaced by either COMMIT or ABORT.  For example, if a
   RESERVE_BLOCKS call fails for the application, it should "abort" by calling
   CLOSE_TRANSCRASH, even though it really commits any changes that were made (which is
   why, for safety, the application should call RESERVE_BLOCKS before making any changes).
  
   For actually implementing these out-of-system-call-scopped transcrashes, the
   reiser4_context has a "txn_handle *trans" pointer that may be set to an open
   transcrash.  Currently there are no dynamically-allocated transcrashes, but there is a
   "kmem_cache_t *_txnh_slab" created for that purpose in this file.
*/

/* Extending the other system call interfaces for future transaction features:
  
   Specialized applications may benefit from passing flags to the ordinary system call
   interface such as read(), write(), or stat().  For example, the application specifies
   WRITE_FUSING by default but wishes to add that a certain read() command should be
   treated as READ_FUSING.  But which read?  Is it the directory-entry read, the stat-data
   read, or the file-data read?  These issues are straight-forward, but there are a lot of
   them and adding the necessary flags-passing code will be tedious.
  
   When supporting isolated transactions, there is a corresponding READ_MODIFY_WRITE (RMW)
   flag, which specifies that although it is a read operation being requested, a
   write-lock should be taken.  The reason is that read-locks are shared while write-locks
   are exclusive, so taking a read-lock when a later-write is known in advance will often
   leads to deadlock.  If a reader knows it will write later, it should issue read
   requests with the RMW flag set.
*/

/* Special disk space reservation:
  
   The space reserved for a transcrash by calling RESERVE_BLOCKS does not cover all
   possible space requirements that a transaction may encounter trying to flush.  The
   prime example of this is extent-allocation, which can consume an unpredictable amount
   of space during flush, due to fragmentation.  We have discussed two ways to reserve for
   any "extra allocation":
  
   - Reserve a fixed percentage of disk space for use (e.g., 5%), and if that approach
   doesn't work (because Nikita Was Right), then...
  
   - Reserve an amount of disk space proportional to the number of unallocated extent
   blocks, or something like that.
*/

/* CURRENT DEADLOCK BETWEEN LOCK_MANAGER & ATOM_CAPTURE_WAIT: SOLUTION.  Its not a true
   deadlock, its a bug.  The bug occurs when one thread waits on a lock held by another
   thread waiting in capture_wait.  The thread waiting for capture is blocked because the
   atom is expired, trying to commit, but it holds a lock.  This problem has the symptoms
   of PRIORITY INVERSION because the lock-waiter should be unblocked so that it can
   eventually release the lock needed to commit the atom.
  
   Currently the transaction manager code is properly signalled by the lock manager when a
   thread is waiting for capture but the lock manager determines that it must yield one of
   its locks.  The problem is that the transaction manager does not wake threads that are
   blocked in capture_wait when they hold a lock needed to commit the atom.
  
   The problem seems impossible--it seems to be already solved.  As described in comments
   at the top of lock.c, we try to capture a block before we try to lock it.  First we
   take the znode spinlock, then we try to capture.  When capture succeeds it returns with
   the spinlock still held.  This is important because if the lock is available (i.e., it
   is unlocked or read-locked) the lock must be granted before any other thread "skips
   ahead" and takes the lock first.  This is done because sometimes a capture request can
   return without actually requiring capture.  For example, a read-request with a
   WRITE_FUSING transcrash does nothing, or a read-request with READ_FUSING and a clean
   node does nothing.  But in order for this to work, the read-request must be granted
   before any writers can lock the node.  For this reason, we capture before we lock.
  
   At first glance, capture-before-lock would seem to address the problem, but it does not
   always.  The explanation is not difficult, but it requires carefully labeling the
   objects involved.
  
   - There are two threads, BLOCKED_IN_LOCK and BLOCKED_IN_CAPTURE, and there are two
   nodes, CLEAN_NODE and DIRTY_NODE.
  
   - BLOCKED_IN_LOCK and DIRTY_NODE belong to an atom that is trying to commit.
   BLOCKED_IN_LOCK has already made modifications.
  
   - BLOCKED_IN_CAPTURE and CLEAN_NODE have no atom.  BLOCKED_IN_CAPTURE is likely a
   coord_by_key tree traversal and it has made no modifications.
  
   The chain of events goes like this:
  
                   BLOCKED_IN_CAPTURE                  BLOCKED_IN_LOCK
  
   1.           read-captures CLEAN_NODE
                  (no capture needed)
  
   2.            read-locks CLEAN_NODE
                      (succeeds)
  
   3.                                              write-captures CLEAN_NODE
                                                    (capture succeeds, now
                                                     CLEAN_NODE is part of
                                                     the atom)
  
   4.                                               write-locks CLEAN_NODE
                                                     (!-- blocked waiting for
                                                      the CLEAN_NODE lock --!)
  
   5.          write-captures DIRTY_NODE
                 (!-- blocked waiting for
                  atom to commit --!)
  
   There we have both processes blocked and capture-before-lock did not help.  The reason
   capture-before-lock did not help is that neither BLOCKED_IN_CAPTURE nor CLEAN_NODE have
   an atom, thus the capture step in #3 causes no fusion.  The above order of events is
   fine the way it is, except in step #3.  At the moment we capture CLEAN_NODE (#3), the
   BLOCKED_IN_CAPTURE thread (which is not actually blocked until #5) should be "taken in"
   to the atom because it is trying to commit.  Then BLOCKED_IN_CAPTURE will never
   actually be blocked in capture when it reaches #5, it will get the lock it wants and
   eventually release CLEAN_NODE so that BLOCKED_IN_LOCK can get it.  In general, the
   following steps will solve this problem:
  
   - When an atom that is trying to commit will succeed at capturing a block, the to-be
   captured block's list of lock owners should be inspected.
  
   - If the thread (transcrash) that holds the lock belongs to another atom: fuse the
   lock-holder's atom with the capturer's atom.
  
   - If the thread (transcrash) that holds the lock does NOT belong to another atom:
   assign that thread to the capturer's atom.
  
   In our previous discussion on this matter, we included the following step "wake the
   thread", but now I believe that is not necessary.  In the example above, taking these
   steps will prevent the BLOCKED_IN_CAPTURE thread from ever blocking.
*/

#include "debug.h"
#include "tslist.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "wander.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "page_cache.h"
#include "reiser4.h"
#include "vfs_ops.h"

#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

static void atom_free(txn_atom * atom);

static long commit_txnh(txn_handle * txnh);

static void wakeup_atom_waitfor_list(txn_atom * atom);
static void wakeup_atom_waiting_list(txn_atom * atom);

static void capture_assign_txnh_nolock(txn_atom * atom, txn_handle * txnh);

static void capture_assign_block_nolock(txn_atom * atom, jnode * node);

static int capture_assign_block(txn_handle * txnh, jnode * node);

static int capture_assign_txnh(jnode * node, txn_handle * txnh, txn_capture mode);

static int check_not_fused_lock_owners(txn_handle * txnh, znode * node);

static int capture_init_fusion(jnode * node, txn_handle * txnh, txn_capture mode);

static int capture_fuse_wait(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode);

static void capture_fuse_into(txn_atom * small, txn_atom * large);

static int capture_copy(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode);

static void invalidate_list(capture_list_head *);

/* GENERIC STRUCTURES */

typedef struct _txn_wait_links txn_wait_links;

struct _txn_wait_links {
	lock_stack *_lock_stack;
	fwaitfor_list_link _fwaitfor_link;
	fwaiting_list_link _fwaiting_link;
};

TS_LIST_DEFINE(atom, txn_atom, atom_link);
TS_LIST_DEFINE(txnh, txn_handle, txnh_link);

TS_LIST_DEFINE(fwaitfor, txn_wait_links, _fwaitfor_link);
TS_LIST_DEFINE(fwaiting, txn_wait_links, _fwaiting_link);

/* FIXME: In theory, we should be using the slab cache init & destructor
   methods instead of, e.g., jnode_init, etc. */
static kmem_cache_t *_atom_slab = NULL;
static kmem_cache_t *_txnh_slab = NULL;	/* FIXME_LATER_JMACD Will it be used? */

ON_DEBUG(extern atomic_t flush_cnt;)

/* TXN_INIT */
ktxnmgrd_context kdaemon;

/* Initialize static variables in this file. */
int
txnmgr_init_static(void)
{
	assert("jmacd-600", _atom_slab == NULL);
	assert("jmacd-601", _txnh_slab == NULL);

	ON_DEBUG(atomic_set(&flush_cnt, 0));

	_atom_slab = kmem_cache_create("txn_atom", sizeof (txn_atom), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (_atom_slab == NULL) {
		goto error;
	}

	_txnh_slab = kmem_cache_create("txn_handle", sizeof (txn_handle), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (_txnh_slab == NULL) {
		goto error;
	}

	init_ktxnmgrd_context(&kdaemon);

	return 0;

error:

	if (_atom_slab != NULL) {
		kmem_cache_destroy(_atom_slab);
	}
	if (_txnh_slab != NULL) {
		kmem_cache_destroy(_txnh_slab);
	}
	return -ENOMEM;
}

/* Un-initialize static variables in this file. */
int
txnmgr_done_static(void)
{
	int ret1, ret2, ret3;

	ret1 = ret2 = ret3 = 0;

	if (_atom_slab != NULL) {
		ret1 = kmem_cache_destroy(_atom_slab);
		_atom_slab = NULL;
	}

	if (_txnh_slab != NULL) {
		ret2 = kmem_cache_destroy(_txnh_slab);
		_txnh_slab = NULL;
	}

	return ret1 ? : ret2;
}

/* Initialize a new transaction manager.  Called when the super_block is initialized. */
void
txnmgr_init(txn_mgr * mgr)
{
	assert("umka-169", mgr != NULL);

	mgr->atom_count = 0;
	mgr->id_count = 1;

	atom_list_init(&mgr->atoms_list);
	spin_txnmgr_init(mgr);

	sema_init(&mgr->commit_semaphore, 1);
}

/* Free transaction manager. */
int
txnmgr_done(txn_mgr * mgr UNUSED_ARG)
{
	assert("umka-170", mgr != NULL);

	return 0;
}

/* Initialize a transaction handle. */
/* Audited by: umka (2002.06.13) */
static void
txnh_init(txn_handle * txnh, txn_mode mode)
{
	assert("umka-171", txnh != NULL);

	txnh->mode = mode;
	txnh->atom = NULL;

	spin_txnh_init(txnh);

	txnh_list_clean(txnh);
}

#if REISER4_DEBUG
/* Check if a transaction handle is clean. */
static int
txnh_isclean(txn_handle * txnh)
{
	assert("umka-172", txnh != NULL);
	return ((txnh->atom == NULL) && spin_txnh_is_not_locked(txnh));
}
#endif

/* Initialize an atom. */
static void
atom_init(txn_atom * atom)
{
	int level;

	assert("umka-173", atom != NULL);

	xmemset(atom, 0, sizeof (txn_atom));

	atom->stage = ASTAGE_FREE;
	atom->start_time = jiffies;

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		capture_list_init(&atom->dirty_nodes[level]);
	}

	capture_list_init(&atom->clean_nodes);
	capture_list_init(&atom->ovrwr_nodes);
	spin_atom_init(atom);
	txnh_list_init(&atom->txnh_list);
	atom_list_clean(atom);
	fwaitfor_list_init(&atom->fwaitfor_list);
	fwaiting_list_init(&atom->fwaiting_list);
	blocknr_set_init(&atom->delete_set);
	blocknr_set_init(&atom->wandered_map);

	init_atom_fq_parts(atom);
}

#if REISER4_DEBUG
/* Check if an atom is clean. */
static int
atom_isclean(txn_atom * atom)
{
	int level;

	assert("umka-174", atom != NULL);

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		if (!capture_list_empty(&atom->dirty_nodes[level])) {
			return 0;
		}
	}

	return ((atom->stage == ASTAGE_FREE) &&
		(atom->txnh_count == 0) &&
		(atom->capture_count == 0) &&
		(atomic_read(&atom->refcount) == 0) &&
		atom_list_is_clean(atom) &&
		txnh_list_empty(&atom->txnh_list) &&
		capture_list_empty(&atom->clean_nodes) &&
		fwaitfor_list_empty(&atom->fwaitfor_list) && fwaiting_list_empty(&atom->fwaiting_list));
}
#endif

/* FIXME_LATER_JMACD Not sure how this is used yet.  The idea is to reserve a number of
   blocks for use by the current transaction handle. */
/* Audited by: umka (2002.06.13) */
int
txn_reserve(int reserved UNUSED_ARG)
{
	return 0;
}

/* Begin a transaction in this context.  Currently this uses the reiser4_context's
   trans_in_ctx, which means that transaction handles are stack-allocated.  Eventually
   this will be extended to allow transaction handles to span several contexts. */
/* Audited by: umka (2002.06.13) */
void
txn_begin(reiser4_context * context)
{
	assert("jmacd-544", context->trans == NULL);

	context->trans = &context->trans_in_ctx;

	/* FIXME_LATER_JMACD Currently there's no way to begin a TXN_READ_FUSING
	   transcrash.  Default should be TXN_WRITE_FUSING.  Also, the _trans variable is
	   stack allocated right now, but we would like to allow for dynamically allocated
	   transcrashes that span multiple system calls.
	*/
	txnh_init(context->trans, TXN_WRITE_FUSING);
}

/* Finish a transaction handle context. */
long
txn_end(reiser4_context * context)
{
	int ret = 0;
	txn_handle *txnh;

	assert("umka-283", context != NULL);
	assert("nikita-3012", schedulable());

	/* closing non top-level context---nothing to do */
	if (context != context->parent)
		return 0;

	assert("nikita-2967", lock_stack_isclean(get_current_lock_stack()));

	txnh = context->trans;

	if (txnh != NULL) {
		/* The txnh's field "atom" can be checked for NULL w/o holding a
		   lock because txnh->atom could be set by this thread's call to
		   try_capture or the deadlock prevention code in
		   check_not_fused_lock_owners().  But that code may assign an
		   atom to this transaction handle only if there are locked and
		   not yet fused nodes.  It cannot happen because lock stack
		   should be clean at this moment. */
		if (txnh->atom != NULL) 
			ret = commit_txnh(txnh);

		assert("jmacd-633", txnh_isclean(txnh));

		context->trans = NULL;
	}

	return ret;
}

/* TXN_ATOM */

/* Get the atom belonging to a txnh, which is not locked.  Return txnh locked. Locks atom, if atom
   is not NULL.  This performs the necessary spin_trylock to break the lock-ordering cycle.  May
   return NULL. */
txn_atom *
atom_get_locked_with_txnh_locked_nocheck(txn_handle * txnh)
{
	txn_atom *atom;

	assert("umka-180", txnh != NULL);
	assert("jmacd-5108", spin_txnh_is_not_locked(txnh));

	while (1) {
		LOCK_TXNH(txnh);
		atom = txnh->atom;

		if (atom == NULL)
			break;

		if (spin_trylock_atom(atom))
			break;

		atomic_inc(&atom->refcount);

		UNLOCK_TXNH(txnh);
		LOCK_ATOM(atom);
		LOCK_TXNH(txnh);

		if (txnh->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		UNLOCK_TXNH(txnh);
		atom_dec_and_unlock(atom);
	}

	return atom;
}

/* Get the current atom and spinlock it if current atom present. May return NULL  */
txn_atom *
get_current_atom_locked_nocheck(void)
{
	reiser4_context *cx;
	txn_atom *atom;
	txn_handle *txnh;

	cx = get_current_context();
	assert("zam-437", cx != NULL);

	txnh = cx->trans;
	assert("zam-435", txnh != NULL);

	atom = atom_get_locked_with_txnh_locked_nocheck(txnh);

	UNLOCK_TXNH(txnh);
	return atom;
}

/* Get the atom belonging to a jnode, which is initially locked.  Return with
   both jnode and atom locked.  This performs the necessary spin_trylock to
   break the lock-ordering cycle.  Assumes the jnode is already locked, and
   returns NULL if atom is not set. */
txn_atom *
atom_locked_by_jnode(jnode * node)
{
	txn_atom *atom;

	assert("umka-181", node != NULL);

	while (1) {
		assert("jmacd-5108", spin_jnode_is_locked(node));

		atom = node->atom;
		/* node is not in any atom */
		if (atom == NULL)
			break;

		/* there could be not contention for the atom spin lock at this
		 * moment and trylock can succeed. */
		if (spin_trylock_atom(atom))
			break;

		/* At leat one jnode belongs to this atom it guarantees that
		 * atom->refcount > 0, we can safely increment refcount. */
		atomic_inc(&atom->refcount);
		UNLOCK_JNODE(node);

		/* re-aquire spin locks in right order */
		LOCK_ATOM(atom);
		LOCK_JNODE(node);

		/* check if node still points to the same atom. */
		if (node->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		/* releasing of atom lock and reference requires not holding
		 * locks on jnodes.  */
		UNLOCK_JNODE(node);

		/* We do not sure that this atom has extra references except our
		 * one, so we should call proper function which may free atom if
		 * last reference is released. */
		atom_dec_and_unlock(atom);

		/* lock jnode again for getting valid node->atom pointer
		 * value. */
		LOCK_JNODE(node);
	}

	return atom;
}

/* Returns true if @node is dirty and part of the same atom as one of its neighbors.  Used
   by flush code to indicate whether the next node (in some direction) is suitable for
   flushing. */
int
same_atom_dirty(jnode * node, jnode * check, int alloc_check, int alloc_value)
{
	int compat;
	txn_atom *atom;

	assert("umka-182", node != NULL);
	assert("umka-183", check != NULL);

	/* Not sure what this function is supposed to do if supplied with @check that is
	   neither formatted nor unformatted (bitmap or so). */
	assert("nikita-2373", jnode_is_znode(check) || jnode_is_unformatted(check));

	/* Need a lock on CHECK to get its atom and to check various state
	   bits.  Don't need a lock on NODE once we get the atom lock. */
	LOCK_JNODE(check);

	atom = atom_locked_by_jnode(check);

	if (atom == NULL) {
		compat = 0;
	} else {
		compat = (node->atom == atom && jnode_is_dirty(check));

		if (compat && jnode_is_znode(check)) {
			compat &= znode_is_connected(JZNODE(check));
		}

		if (compat && alloc_check) {
			compat &= (alloc_value == jnode_is_flushprepped(check));
		}

		UNLOCK_ATOM(atom);
	}

	UNLOCK_JNODE(check);

	return compat;
}

#if REISER4_DEBUG
/* Return true if an atom is currently "open". */
static int
atom_isopen(const txn_atom * atom)
{
	assert("umka-185", atom != NULL);

	return atom->stage > 0 && atom->stage < ASTAGE_PRE_COMMIT;
}
#endif

/* Decrement the atom's reference count and if it falls to zero, free it. */
void atom_dec_and_unlock(txn_atom * atom)
{
	txn_mgr *mgr = &get_super_private(reiser4_get_current_sb())->tmgr;

	assert("umka-186", atom != NULL);
	assert("jmacd-1071", spin_atom_is_locked(atom));

	if (atomic_dec_and_test(&atom->refcount)) {
		/* take txnmgr lock and atom lock in proper order. */
		if (!spin_trylock_txnmgr(mgr)) {
			/* This atom should exist after we re-acquire its
			 * spinlock, so we increment its reference counter. */
			atomic_inc(&atom->refcount);
			UNLOCK_ATOM(atom);
			spin_lock_txnmgr(mgr);
			LOCK_ATOM(atom);

			if (!atomic_dec_and_test(&atom->refcount)) {
				UNLOCK_ATOM(atom);
				spin_unlock_txnmgr(mgr);
				return;
			}
		}
		assert("nikita-2656", spin_txnmgr_is_locked(mgr));
		atom_free(atom);
		spin_unlock_txnmgr(mgr);
	} else {
		UNLOCK_ATOM(atom);
	}
}

/* Return an new atom, locked.  This adds the atom to the transaction manager's list and
   sets its reference count to 1, an artificial reference which is kept until it
   commits.  We play strange games to avoid allocation under jnode & txnh spinlocks. */
static txn_atom *
atom_begin_andlock(txn_atom ** atom_alloc, jnode * node, txn_handle * txnh)
{
	txn_atom *atom;
	txn_mgr *mgr;

	assert("jmacd-43228", spin_jnode_is_locked(node));
	assert("jmacd-43227", spin_txnh_is_locked(txnh));
	assert("jmacd-43226", node->atom == NULL);
	assert("jmacd-43225", txnh->atom == NULL);

	/* Cannot allocate under those spinlocks. */
	UNLOCK_JNODE(node);
	UNLOCK_TXNH(txnh);

	if (*atom_alloc == NULL) {
		(*atom_alloc) = kmem_cache_alloc(_atom_slab, GFP_KERNEL);

		if (*atom_alloc == NULL)
			return ERR_PTR(-ENOMEM);
	}

	/* and, also, txnmgr spin lock should be taken before jnode and txnh
	   locks. */
	mgr = &get_super_private(reiser4_get_current_sb())->tmgr;
	spin_lock_txnmgr(mgr);

	LOCK_JNODE(node);
	LOCK_TXNH(txnh);

	/* Check if both atom pointers are still NULL... */
	if (node->atom != NULL || txnh->atom != NULL) {
		trace_on(TRACE_TXN, "alloc atom race\n");
		/* NOTE-NIKITA probably it is rather better to free
		 * atom_alloc here than thread it up to try_capture(). */

		UNLOCK_TXNH(txnh);
		UNLOCK_JNODE(node);
		spin_unlock_txnmgr(mgr);

		return ERR_PTR(-EAGAIN);
	}

	atom = *atom_alloc;
	*atom_alloc = NULL;

	atom_init(atom);

	assert("jmacd-17", atom_isclean(atom));

	/* Take the atom and txnmgr lock. No checks for lock ordering, because
	   @atom is new and inaccessible for others. */
	spin_lock_atom_no_ord(atom, 0, 0);

	atom_list_push_back(&mgr->atoms_list, atom);

	atom->atom_id = mgr->id_count++;
	mgr->atom_count += 1;

	/* Release txnmgr lock */
	spin_unlock_txnmgr(mgr);

	/* One reference until it commits. */
	atomic_inc(&atom->refcount);

	atom->stage = ASTAGE_CAPTURE_FUSE;

	trace_on(TRACE_TXN, "begin atom %u\n", atom->atom_id);

	return atom;
}

/* Return the number of pointers to this atom that must be updated during fusion.  This
   approximates the amount of work to be done.  Fusion chooses the atom with fewer
   pointers to fuse into the atom with more pointers. */
/* Audited by: umka (2002.06.13), umka (2002.16.15) */
static int
atom_pointer_count(const txn_atom * atom)
{
	assert("umka-187", atom != NULL);

	/* This is a measure of the amount of work needed to fuse this atom into another. */
	assert("jmacd-28", atom_isopen(atom));

	return atom->txnh_count + atom->capture_count;
}

/* Called holding the atom lock, this removes the atom from the transaction manager list
   and frees it. */
static void
atom_free(txn_atom * atom)
{
	txn_mgr *mgr = &get_super_private(reiser4_get_current_sb())->tmgr;

	assert("umka-188", atom != NULL);

	trace_on(TRACE_TXN, "free atom %u\n", atom->atom_id);

	assert("jmacd-18", spin_atom_is_locked(atom));

	/* Remove from the txn_mgr's atom list */
	assert("nikita-2657", spin_txnmgr_is_locked(mgr));
	mgr->atom_count -= 1;
	atom_list_remove_clean(atom);

	/* Clean the atom */
	assert("jmacd-16", (atom->stage == ASTAGE_FUSED || atom->stage == ASTAGE_DONE));
	atom->stage = ASTAGE_FREE;

	blocknr_set_destroy(&atom->delete_set);
	blocknr_set_destroy(&atom->wandered_map);

	assert("jmacd-16", atom_isclean(atom));

	UNLOCK_ATOM(atom);

	kmem_cache_free(_atom_slab, atom);
}

static int
atom_is_dotard(const txn_atom * atom)
{
	return time_after(jiffies, atom->start_time + get_current_super_private()->txnmgr.atom_max_age);
}

static int atom_can_be_committed (txn_atom * atom)
{
	assert ("zam-884", spin_atom_is_locked(atom));
	assert ("zam-885", atom->txnh_count > atom->nr_waiters);
	return atom->txnh_count == atom->nr_waiters + 1;
}

/* Return true if an atom should commit now.  This will be determined by aging.  For now
   this says to commit after the atom has 20 captured nodes.  The routine is only called
   when the txnh_count drops to 0. */
static int
atom_should_commit(const txn_atom * atom)
{
	assert("umka-189", atom != NULL);
	return (atom->flags & ATOM_FORCE_COMMIT)
		|| ((unsigned) atom_pointer_count(atom) > get_current_super_private()->txnmgr.atom_max_size)
		|| atom_is_dotard(atom);
}

#if 0
/* FIXME: JMACD->ZAM: This should be removed after a transaction can wait on all its
   active io_handles here. */
static void
txn_wait_on_io(txn_atom * atom)
{
	jnode *scan;

	for (scan = capture_list_front(&atom->clean_nodes);
	     /**/ !capture_list_end(&atom->clean_nodes, scan); scan = capture_list_next(scan)) {

		if (scan->pg && PageWriteback(scan->pg)) {
			wait_on_page_writeback(scan->pg);
		}
	}
}
#endif

/* Get first dirty node from the atom's dirty_nodes[n] lists; return NULL if atom has no dirty
   nodes on atom's lists */
jnode * find_first_dirty_jnode (txn_atom * atom)
{
	jnode *first_dirty;
	tree_level level;

	assert("zam-753", spin_atom_is_locked(atom));

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		capture_list_head *head;

		if (capture_list_empty(&atom->dirty_nodes[level])) {
			continue;
		}

		/* skip jnodes which have "heard banshee" */
		head = &atom->dirty_nodes[level];
		for (first_dirty = capture_list_front(head); !capture_list_end(head, first_dirty);
		     first_dirty = capture_list_next(first_dirty)) {
			if (!JF_ISSET(first_dirty, JNODE_HEARD_BANSHEE))
				return first_dirty;
		}
	}
	return NULL;
}

/* Called with the atom locked and no open "active" transaction handlers except
   ours, this function calls flush_current_atom() until all dirty nodes are
   processed.  Then it initiates commit processing.
  
   Called by the single remaining open "active" txnh, which is closing. Other
   open txnhs belong to processes which wait atom commit in commit_txnh()
   routine. They are counted as "waiters" in atom->nr_waiters.  Therefore as
   long as we hold the atom lock none of the jnodes can be captured and/or
   locked.
   
   Return value is an error code if commit fails.
*/
static int commit_current_atom (long *nr_submitted, txn_atom ** atom)
{
	reiser4_super_info_data * sbinfo = get_current_super_private ();
	long ret;

	assert ("zam-888", atom != NULL && *atom != NULL);
	assert ("zam-886", spin_atom_is_locked(*atom));
	assert ("zam-887", get_current_context()->trans->atom == *atom);
	assert("jmacd-151", atom_isopen(*atom));

	trace_on(TRACE_TXN, "atom %u trying to commit %u: CAPTURE_WAIT\n", 
		 (*atom)->atom_id, current->pid);

	while (1) {
		ret = flush_current_atom(JNODE_FLUSH_WRITE_BLOCKS, nr_submitted, atom);
		if (ret != -EAGAIN)
			break;

		*atom = get_current_atom_locked();
	}

	if (ret)
		return ret;

	assert ("zam-882", spin_atom_is_locked(*atom));

	if (!atom_can_be_committed(*atom)) {
		UNLOCK_ATOM(*atom);
		return -EAGAIN;
	}

	/* Up to this point we have been flushing and after flush is called we
	   return -EAGAIN.  Now we can commit.  We cannot return -EAGAIN at this
	   point, commit should be successful. */
	(*atom)->stage = ASTAGE_PRE_COMMIT;

	trace_on(TRACE_TXN, "commit atom %u: PRE_COMMIT\n", (*atom)->atom_id);
	trace_on(TRACE_FLUSH, "everything flushed atom %u: PRE_COMMIT\n", (*atom)->atom_id);

	UNLOCK_ATOM(*atom);

	ret = current_atom_finish_all_fq();
	if (ret)
		return ret;

	trace_on(TRACE_FLUSH, "everything written back atom %u\n", 
		 (*atom)->atom_id);

	/* isolate critical code path which should be executed by only one
	 * thread using tmgr semaphore */
	down(&sbinfo->tmgr.commit_semaphore);

	ret = reiser4_write_logs();
	if (ret < 0)
		reiser4_panic("zam-597", "write log failed (%ld)\n", ret);

	LOCK_ATOM(*atom);

	invalidate_list(&(*atom)->clean_nodes);
	invalidate_list(&(*atom)->ovrwr_nodes);

	up(&sbinfo->tmgr.commit_semaphore);

	(*atom)->stage = ASTAGE_DONE;

	/* Atom's state changes, so wake up everybody waiting for this
	   event. */
	wakeup_atom_waitfor_list(*atom);
	wakeup_atom_waiting_list(*atom);

	/* Decrement the "until commit" reference, at least one txnh (the caller) is
	   still open. */
	atomic_dec(&(*atom)->refcount);

	assert("jmacd-1070", atomic_read(&(*atom)->refcount) > 0);
	assert("jmacd-1062", (*atom)->capture_count == 0);
	assert("jmacd-1071", spin_atom_is_locked(*atom));

	trace_on(TRACE_TXN, "commit atom finished %u refcount %d\n",
		 (*atom)->atom_id, atomic_read(&(*atom)->refcount));
	return ret;
}

/* TXN_TXNH */

/* commit current atom and wait commit completion; atom and txn_handle should be
 * locked before call, this function unlocks them on exit. */
static int force_commit_atom_nolock (txn_handle * txnh)
{
	int ret;
	txn_atom * atom;
	reiser4_context * ctx = get_current_context();

	assert ("zam-837", txnh != NULL);
	assert ("zam-835", spin_txnh_is_locked(txnh));
	assert ("nikita-2966", lock_stack_isclean(get_current_lock_stack()));

	atom = txnh->atom;

	assert ("zam-834", atom != NULL);
	assert ("zam-836", spin_atom_is_locked(atom));

	/* Set flags for atom and txnh: forcing atom commit and waiting for
	 * commit completion */
	txnh->flags |= TXNH_WAIT_COMMIT;
	atom->flags |= ATOM_FORCE_COMMIT;

	UNLOCK_TXNH(txnh);
	UNLOCK_ATOM(atom);

	if ((ret = txn_end(ctx)) < 0) {
		return ret;
	}

	preempt_point();

	txn_begin(ctx);

	return 0;
}

/* externally visible function which takes all necessary locks and commits
 * current atom */
int txnmgr_force_commit_current_atom (void)
{
	txn_handle * txnh = get_current_context()->trans;
	txn_atom * atom;

	atom = atom_get_locked_with_txnh_locked_nocheck(txnh);

	if (atom == NULL) {
		UNLOCK_TXNH(txnh);
		return 0;
	}
	
	return force_commit_atom_nolock(txnh);
} 

/* Called to force commit of any outstanding atoms.  Later this should be
   improved to not to wait indefinitely if new atoms are created. */
int txnmgr_force_commit_all (struct super_block *super)
{
	int ret;
	txn_atom *atom;
	txn_mgr *mgr;
	txn_handle *txnh;
	reiser4_context * ctx = get_current_context();

	assert("nikita-2965", lock_stack_isclean(get_current_lock_stack()));
	assert("nikita-3058", no_counters_are_held());

	ret = txn_end(ctx);
	if (ret)
		return ret;

	txn_begin(ctx);

	mgr = &get_super_private(super)->tmgr;

	txnh = get_current_context()->trans;

again:

	spin_lock_txnmgr(mgr);

	for (atom = atom_list_front(&mgr->atoms_list);
	     /**/ !atom_list_end(&mgr->atoms_list, atom); atom = atom_list_next(atom)) {

		LOCK_ATOM(atom);

		if (atom->stage < ASTAGE_PRE_COMMIT) {
			spin_unlock_txnmgr(mgr);
			LOCK_TXNH(txnh);

			/* Add force-context txnh */
			capture_assign_txnh_nolock(atom, txnh);

			ret = force_commit_atom_nolock(txnh);
			if(ret)
				return ret;

			goto again;
		}

		UNLOCK_ATOM(atom);
	}

#if REISER4_DEBUG
	{
		reiser4_super_info_data * sbinfo = get_super_private(super);
		reiser4_spin_lock_sb(super);
		assert("zam-813", sbinfo->blocks_fake_allocated_unformatted == 0);
		assert("zam-812", sbinfo->blocks_fake_allocated == 0);
		reiser4_spin_unlock_sb(super);
	}
#endif

	spin_unlock_txnmgr(mgr);

	return 0;
}

/* called periodically from ktxnmgrd to commit old atoms. Releases ktxnmgrd spin
 * lock at exit */
int
commit_some_atoms(txn_mgr * mgr)
{
	int ret = 0;
	txn_atom *atom;
	txn_handle *txnh;
	reiser4_context *ctx;

	ctx = get_current_context();
	assert("nikita-2444", ctx != NULL);
	assert("nikita-2445", spin_ktxnmgrd_is_locked(mgr->daemon));

	txnh = ctx->trans;
	spin_lock_txnmgr(mgr);

	/* look for atom to commit */
	for (atom = atom_list_front(&mgr->atoms_list);
	     /**/ !atom_list_end(&mgr->atoms_list, atom); atom = atom_list_next(atom)) {

		LOCK_ATOM(atom);

		if ((atom->stage < ASTAGE_PRE_COMMIT) && (atom->txnh_count == 0) && atom_should_commit(atom))
			break;

		UNLOCK_ATOM(atom);
	}

	ret = atom_list_end(&mgr->atoms_list, atom);
	spin_unlock_txnmgr(mgr);

	if (ret) {
		/* nothing found */
		spin_unlock_ktxnmgrd(mgr->daemon);
		return 0;
	}

	LOCK_TXNH(txnh);

	/* Set the atom to force committing */
	atom->flags |= ATOM_FORCE_COMMIT;

	/* Add force-context txnh */
	capture_assign_txnh_nolock(atom, txnh);

	UNLOCK_TXNH(txnh);
	UNLOCK_ATOM(atom);

	/* we are about to release daemon spin lock, notify daemon it
	   has to rescan atoms */
	mgr->daemon->rescan = 1;
	spin_unlock_ktxnmgrd(mgr->daemon);
	ret = txn_end(ctx);

	if (ret >= 0) {
		txn_begin(ctx);
		ret = 0;
	}

	return ret;
}

/* Calls jnode_flush for current atom if it exists; if not, just take another
   atom and call jnode_flush() for him.  If current transaction handle has
   already assigned atom (current atom) we have to close current transaction
   prior to switch to another atom or do something with current atom. This
   code tries to flush current atom. */
int flush_some_atom(long *nr_submitted, int flags)
{
	reiser4_context *ctx = get_current_context();
	txn_handle *txnh = ctx->trans;
	txn_atom *atom;
	int ret;

	if (txnh->atom == NULL) {
		int found = 0;
		/* current atom is available, take first from txnmgr */
		txn_mgr *tmgr = &get_super_private(ctx->super)->tmgr;

		spin_lock_txnmgr(tmgr);

		/* traverse the list of all atoms */
		for (atom = atom_list_front(&tmgr->atoms_list);
		     !found && !atom_list_end(&tmgr->atoms_list, atom); 
		     atom = atom_list_next(atom)) 
		{
			/* lock atom before checking its state */
			LOCK_ATOM(atom);

			/* we need an atom which is not being committed and which has no
			 * flushers (jnode_flush() add one flusher at the beginning and
			 * subtract one at the end). */
			if (atom->stage < ASTAGE_PRE_COMMIT && atom->nr_flushers == 0) {
				LOCK_TXNH(txnh);
				capture_assign_txnh_nolock(atom, txnh);
				UNLOCK_TXNH(txnh);

				found = 1;
				break;
			}

			UNLOCK_ATOM(atom);
		}

		spin_unlock_txnmgr(tmgr);

		/* no suitable atoms found, return */
		if (!found)
			return 0;
	} else 
		atom = get_current_atom_locked();

	ret = flush_current_atom(flags, nr_submitted, &atom);
	if (ret == 0) {
		if (*nr_submitted == 0) {
			/* if early flushing could not make more nodes clean, we
			 * force current atom to commit. */
			txnh->flags |= TXNH_WAIT_COMMIT;
			atom->flags |= ATOM_FORCE_COMMIT;
		}
		UNLOCK_ATOM(atom);
	}

	if (ret == -EAGAIN)
		ret = 0;

	{
		int ret1;

		ret1 = txn_end(ctx);
		if (ret1 > 0)
			*nr_submitted += ret1;
		txn_begin(ctx);
	}

	return ret;
}

/* Remove processed nodes from atom's clean list (thereby remove them from transaction). */
static void
invalidate_list(capture_list_head * head)
{
	while (!capture_list_empty(head)) {
		jnode *pos_in_atom;

		pos_in_atom = capture_list_front(head);
		uncapture_block(pos_in_atom->atom, pos_in_atom);
	}
}

static void
init_wlinks(txn_wait_links * wlinks)
{
	wlinks->_lock_stack = get_current_lock_stack();
	fwaitfor_list_clean(wlinks);
	fwaiting_list_clean(wlinks);
}

/* Add and atom to the atom's waitfor list and wait somebody who is pleased to wake us up; */
void atom_wait_event(txn_atom * atom)
{
	txn_wait_links _wlinks;
	PROF_BEGIN(atom_wait_event);

	assert("zam-744", spin_atom_is_locked(atom));
	init_wlinks(&_wlinks);
	fwaitfor_list_push_back(&atom->fwaitfor_list, &_wlinks);
	atomic_inc(&atom->refcount);
	UNLOCK_ATOM(atom);

	/*assert("nikita-3056", no_counters_are_held());*/
	prepare_to_sleep(_wlinks._lock_stack);
	go_to_sleep(_wlinks._lock_stack, ADD_TO_SLEPT_IN_WAIT_EVENT);

	LOCK_ATOM (atom);
	fwaitfor_list_remove(&_wlinks);
	atom_dec_and_unlock (atom);
	PROF_END(atom_wait_event, atom_wait_event);
}

/* wake all threads which wait for an event */
void
atom_send_event(txn_atom * atom)
{
	assert("zam-745", spin_atom_is_locked(atom));
	wakeup_atom_waitfor_list(atom);
}

/* Informs txn manager code that owner of this txn_handle should wait atom commit completion (for
   example, because it does fsync(2)) */
static int
should_wait_commit(txn_handle * h)
{
	return h->flags & TXNH_WAIT_COMMIT;
}

/* Called to commit a transaction handle.  This decrements the atom's number of open
   handles and if it is the last handle to commit and the atom should commit, initiates
   atom commit. if commit does not fail, return number of written blocks */
static long
commit_txnh(txn_handle * txnh)
{
	int ret = 0;
	txn_atom *atom;
	int failed = 0;
	int wait = 0;
	long nr_written = 0;

	assert("umka-192", txnh != NULL);

again:
	assert("nikita-2968", lock_stack_isclean(get_current_lock_stack()));

	/* Get the atom and txnh locked. */
	atom = get_current_atom_locked();

	if (wait) {
		atom->nr_waiters --;
		wait = 0;
	}

	if (atom->stage == ASTAGE_DONE)
		goto done;

	trace_on(TRACE_TXN,
		 "commit_txnh: atom %u failed %u; txnh_count %u; should_commit %u\n",
		 atom->atom_id, failed, atom->txnh_count, atom_should_commit(atom));

	if (!failed && atom_should_commit(atom)) {
		if (!atom_can_be_committed(atom)) {
			if (should_wait_commit(txnh)) {
				atom->nr_waiters++;
				wait = 1;
				atom_wait_event(atom);
				goto again;
			}

			goto done;
		}

		/* We change atom state to ASTAGE_CAPTURE_WAIT to prevent atom fusion and count
		   ourself as an active flusher */
		atom->stage = ASTAGE_CAPTURE_WAIT;
		atom->flags |= ATOM_FORCE_COMMIT;

		ret = commit_current_atom(&nr_written, &atom);
		if (ret) {
			if (ret != -EAGAIN)
				failed = 1;
			ret = 0;
			preempt_point();
			goto again;
		}
	}

	assert("jmacd-1027", spin_atom_is_locked(atom));
done:
	LOCK_TXNH(txnh);

	atom->txnh_count -= 1;
	txnh->atom = NULL;

	txnh_list_remove(txnh);

	trace_on(TRACE_TXN, "close txnh atom %u refcount %d\n", atom->atom_id, atomic_read(&atom->refcount));

	UNLOCK_TXNH(txnh);
	atom_dec_and_unlock(atom);

	/* Note: We are ignoring the failure code.  Can't change the result of the caller.
	   E.g., in write():
	  
	     result = 512;
	     REISER4_EXIT (result);
	  
	   It cannot "forget" that 512 bytes were written, even if commit fails.  This
	   means force_txn_commit will retry forever.  Is there a better solution?
	*/
	return nr_written;
}

/* TRY_CAPTURE */

/* This routine attempts a single block-capture request.  It may return -EAGAIN if some
   condition indicates that the request should be retried, and it may block if the
   txn_capture mode does not include the TXN_CAPTURE_NONBLOCKING request flag.
  
   The try_capture() function (below) is the external interface, which calls this
   function repeatedly as long as -EAGAIN is returned.
  
   This routine encodes the basic logic of block capturing described by:
  
     http://namesys.com/txn-doc.html
  
   Our goal here is to ensure that any two blocks that contain dependent modifications
   should commit at the same time.  This function enforces this discipline by initiating
   fusion whenever a transaction handle belonging to one atom requests to read or write a
   block belonging to another atom (TXN_CAPTURE_WRITE or TXN_CAPTURE_READ_ATOMIC).
  
   In addition, this routine handles the initial assignment of atoms to blocks and
   transaction handles.  These are possible outcomes of this function:
  
   1. The block and handle are already part of the same atom: return immediate success
  
   2. The block is assigned but the handle is not: call capture_assign_txnh to assign
      the handle to the block's atom.
  
   3. The handle is assigned but the block is not: call capture_assign_block to assign
      the block to the handle's atom.
  
   4. Both handle and block are assigned, but to different atoms: call capture_init_fusion
      to fuse atoms.
  
   5. Neither block nor handle are assigned: create a new atom and assign them both.
  
   6. A read request for a non-captured block: return immediate success.
  
   This function acquires and releases the handle's spinlock.  This function is called
   under the jnode lock and if the return value is 0, it returns with the jnode lock still
   held.  If the return is -EAGAIN or some other error condition, the jnode lock is
   released.  The external interface (try_capture) manages re-aquiring the jnode lock
   in the failure case.
*/
static int
try_capture_block(txn_handle * txnh, jnode * node, txn_capture mode, txn_atom ** atom_alloc)
{
	int ret;
	txn_atom *block_atom;
	txn_atom *txnh_atom;

	/* Should not call capture for READ_NONCOM requests, handled in try_capture. */
	assert("jmacd-567", CAPTURE_TYPE(mode) != TXN_CAPTURE_READ_NONCOM);

	/* FIXME_LATER_JMACD Should assert that atom->tree == node->tree somewhere. */

	assert("umka-194", txnh != NULL);
	assert("umka-195", node != NULL);

	/* The jnode is already locked!  Being called from try_capture(). */
	assert("jmacd-567", spin_jnode_is_locked(node));

	/* Get txnh spinlock, this allows us to compare txn_atom pointers but it doesn't
	   let us touch the atoms themselves. */
	LOCK_TXNH(txnh);

	block_atom = node->atom;
	txnh_atom = txnh->atom;

	if (txnh_atom != NULL) {
		/* It is time to perform deadlock prevention check over the node we want to capture.
		   It is possible this node was locked for read without capturing it. The
		   optimization which allows to do it helps us in keeping atoms independent as long
		   as possible but it may cause lock/fuse deadlock problems. 

		   The number of similar deadlock situations with locked but not captured were
		   found.  In each situation there are two or more threads one of them does flushing
		   another one does routine balancing or tree lookup.  The flushing thread (F)
		   sleeps in long term locking request for node (N), another thread (A) sleeps in
		   trying to capture some node already belonging the atom F, F has a state which
		   prevents immediately fusion .

		   Deadlocks of this kind cannot happen if node N was properly captured by thread
		   A. The F thread fuse atoms before locking therefore current atom of thread F and
		   current atom of thread A became the same atom and thread A may proceed.  This
		   does not work if node N was not captured because the fusion of atom does not
		   happens.

		   The following scheme solves the deadlock: If longterm_lock_znode locks and does
		   not capture a znode, that znode is marked as MISSED_IN_CAPTURE.  A node marked
		   this way is processed by the code below which restores the missed capture and
		   fuses current atoms of all the node lock owners by calling the
		   check_not_fused_lock_owners() function.
		*/

		if (		// txnh_atom->stage >= ASTAGE_CAPTURE_WAIT &&
			   jnode_is_znode(node) && znode_is_locked(JZNODE(node))
			   && JF_ISSET(node, JNODE_MISSED_IN_CAPTURE)) {
			JF_CLR(node, JNODE_MISSED_IN_CAPTURE);

			ret = check_not_fused_lock_owners(txnh, JZNODE(node));

			if (ret) {
				JF_SET(node, JNODE_MISSED_IN_CAPTURE);

				assert("zam-687", spin_txnh_is_not_locked(txnh));
				assert("zam-688", spin_jnode_is_not_locked(node));

				return ret;
			}

			assert("zam-701", spin_txnh_is_locked(txnh));
			assert("zam-702", spin_jnode_is_locked(node));
		}
	}

	if (block_atom != NULL) {
		/* The block has already been assigned to an atom. */

		if (block_atom == txnh_atom) {
			/* No extra capturing work required. */
		} else if (txnh_atom == NULL) {

			/* The txnh is unassigned, try to assign it. */
			if ((ret = capture_assign_txnh(node, txnh, mode)) != 0) {
				/* EAGAIN or otherwise */
				assert("jmacd-6129", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6130", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* Either the txnh is now assigned to the block's atom or the read-request was
			   granted because the block is committing.  Locks still held. */
		} else {
			if (mode & TXN_CAPTURE_DONT_FUSE) {
				UNLOCK_TXNH(txnh);
				UNLOCK_JNODE(node);
				return -E_NO_NEIGHBOR;
			}

			/* In this case, both txnh and node belong to different atoms.  This function
			   returns -EAGAIN on successful fusion, 0 on the fall-through case. */
			if ((ret = capture_init_fusion(node, txnh, mode)) != 0) {
				assert("jmacd-6131", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6132", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* The fall-through case is read request for committing block.  Locks still
			   held. */
		}

	} else if ((mode & TXN_CAPTURE_WTYPES) != 0) {

		/* In this case, the page is unlocked and the txnh wishes exclusive access. */

		if (txnh_atom != NULL) {
			/* The txnh is already assigned: add the page to its atom. */
			if ((ret = capture_assign_block(txnh, node)) != 0) {
				/* EAGAIN or otherwise */
				assert("jmacd-6133", spin_txnh_is_not_locked(txnh));
				assert("jmacd-6134", spin_jnode_is_not_locked(node));
				return ret;
			}

			/* Success: Locks are still held. */

		} else {

			/* In this case, neither txnh nor page are assigned to an atom. */
			block_atom = atom_begin_andlock(atom_alloc, node, txnh);

			if (!IS_ERR(block_atom)) {
				/* Assign both, release atom lock. */
				assert("jmacd-125", block_atom->stage == ASTAGE_CAPTURE_FUSE);

				capture_assign_txnh_nolock(block_atom, txnh);
				capture_assign_block_nolock(block_atom, node);

				UNLOCK_ATOM(block_atom);
			} else {
				/* all locks are released already */
				return PTR_ERR(block_atom);
			}

			/* Success: Locks are still held. */
		}

	} else {
		/* The jnode is uncaptured and its a read request -- fine. */
		assert("jmacd-411", CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC);
	}

	/* Successful case: both jnode and txnh are still locked. */
	assert("jmacd-740", spin_txnh_is_locked(txnh));
	assert("jmacd-741", spin_jnode_is_locked(node));

	/* Release txnh lock, return with the jnode still locked. */
	UNLOCK_TXNH(txnh);

	return 0;
}

/* This function sets up a call to try_capture_block and repeats as long as -EAGAIN is
   returned by that routine.  The txn_capture request mode is computed here depending on
   the transaction handle's type and the lock request.  This is called from the depths of
   the lock manager with the jnode lock held and it always returns with the jnode lock
   held.
*/
int
try_capture(jnode * node, znode_lock_mode lock_mode, txn_capture flags
	    /* ...NONBLOCKING and ...DONT_FUSE are allowed here */ )
{
	int ret;
	txn_handle *txnh;
	txn_capture cap_mode;
	txn_atom *atom_alloc = NULL;

	int non_blocking = flags & TXN_CAPTURE_NONBLOCKING;

	txnh = get_current_context()->trans;
	assert("jmacd-492", txnh);

	/* FIXME_JMACD No way to set TXN_CAPTURE_READ_MODIFY yet. */

	if (lock_mode == ZNODE_WRITE_LOCK) {
		cap_mode = TXN_CAPTURE_WRITE;
	} else if (node->atom != NULL) {
		cap_mode = TXN_CAPTURE_WRITE;
	} else if ((txnh->mode == TXN_READ_FUSING)
		   && (jnode_get_level(node) == LEAF_LEVEL)) {
		/* NOTE-NIKITA TXN_READ_FUSING is not currently used */
		/* We only need a READ_FUSING capture at the leaf level.  This is because
		   the internal levels of the tree (twigs included) are redundant from the
		   point of the user that asked for a read-fusing transcrash.  The user
		   only wants to read-fuse atoms due to reading uncommitted data that
		   another user has written.  It is the file system that reads/writes the
		   internal tree levels, the user only reads/writes leaves. */
		cap_mode = TXN_CAPTURE_READ_ATOMIC;
	} else {
		/* In this case (read lock at a non-leaf) there's no reason to capture. */
		/* cap_mode = TXN_CAPTURE_READ_NONCOM; */

		/* Mark this node as "MISSED".  It helps in further deadlock analysis */
		JF_SET(node, JNODE_MISSED_IN_CAPTURE);
		return 0;
	}

	cap_mode |= (flags & (TXN_CAPTURE_NONBLOCKING | TXN_CAPTURE_DONT_FUSE));

	assert("jmacd-604", spin_jnode_is_locked(node));

repeat:
	/* Repeat try_capture as long as -EAGAIN is returned. */
	ret = try_capture_block(txnh, node, cap_mode, &atom_alloc);

	/* Regardless of non_blocking:

	   If ret == 0 then jnode is still locked.
	   If ret != 0 then jnode is unlocked.
	*/
	assert("nikita-2674", ergo(ret == 0, spin_jnode_is_locked(node)));
	assert("nikita-2675", ergo(ret != 0, spin_jnode_is_not_locked(node)));

	assert("nikita-2974", spin_txnh_is_not_locked(txnh));

	if (ret == -EAGAIN && !non_blocking) {
		/* EAGAIN implies all locks were released, therefore we need to take the
		   jnode's lock again. */
		LOCK_JNODE(node);

		/* Although this may appear to be a busy loop, it is not.  There are
		   several conditions that cause EAGAIN to be returned by the call to
		   txn_block_try_capture, all cases indicating some kind of state
		   change that means you should retry the request and will get a different
		   result.  In some cases this could be avoided with some extra code, but
		   generally it is done because the necessary locks were released as a
		   result of the operation and repeating is the simplest thing to do (less
		   bug potential).  The cases are: atom fusion returns EAGAIN after it
		   completes (jnode and txnh were unlocked); race conditions in
		   assign_block, assign_txnh, and init_fusion return EAGAIN (trylock
		   failure); after going to sleep in capture_fuse_wait (request was
		   blocked but may now succeed).  I'm not quite sure how capture_copy
		   works yet, but it may also return EAGAIN.  When the request is
		   legitimately blocked, the requestor goes to sleep in fuse_wait, so this
		   is not a busy loop. */
		/* NOTE-NIKITA: still don't understand:
		  
		   try_capture_block->capture_assign_txnh->spin_trylock_atom->EAGAIN
		  
		   looks like busy loop?
		*/
		goto repeat;
	}

	/* free extra atom object that was possibly allocated by
	   try_capture_block().
	  
	   Do this before acquiring jnode spin lock to
	   minimize time spent under lock. --nikita */
	if (atom_alloc != NULL) {
		kmem_cache_free(_atom_slab, atom_alloc);
	}

	if (ret != 0) {
		/* Failure means jnode is not locked.  FIXME_LATER_JMACD May want to fix
		   the above code to avoid releasing the lock and re-acquiring it, but there
		   are cases were failure occurs when the lock is not held, and those
		   cases would need to be modified to re-take the lock. */
		LOCK_JNODE(node);
	}

	/* Jnode is still locked. */
	assert("jmacd-760", spin_jnode_is_locked(node));

	return ret;
}

/* fuse all 'active' atoms of lock owners of given node. */
static int
check_not_fused_lock_owners(txn_handle * txnh, znode * node)
{
	lock_handle *lh;
	int repeat = 0;
	txn_atom *atomh = txnh->atom;

/*	assert ("zam-689", znode_is_rlocked (node));*/
	assert("zam-690", spin_znode_is_locked(node));
	assert("zam-691", spin_txnh_is_locked(txnh));
	assert("zam-692", atomh != NULL);

	if (!spin_trylock_atom(atomh)) {
		repeat = 1;
		goto fail;
	}

	/* inspect list of lock owners */
	for (lh = owners_list_front(&node->lock.owners);
	     !owners_list_end(&node->lock.owners, lh); lh = owners_list_next(lh)) {
		reiser4_context *ctx;
		txn_atom *atomf;

		ctx = get_context_by_lock_stack(lh->owner);

		if (ctx == get_current_context())
			continue;

		if (!spin_trylock_txnh(ctx->trans)) {
			repeat = 1;
			continue;
		}

		atomf = ctx->trans->atom;

		if (atomf == NULL) {
			capture_assign_txnh_nolock(atomh, ctx->trans);
			UNLOCK_TXNH(ctx->trans);

			reiser4_wake_up(lh->owner);
			continue;
		}

		if (atomf == atomh) {
			UNLOCK_TXNH(ctx->trans);
			continue;
		}

		if (!spin_trylock_atom(atomf)) {
			UNLOCK_TXNH(ctx->trans);
			repeat = 1;
			continue;
		}

		UNLOCK_TXNH(ctx->trans);

		if (atomf == atomh || atomf->stage > ASTAGE_CAPTURE_WAIT) {
			UNLOCK_ATOM(atomf);
			continue;
		}
		// repeat = 1;

		reiser4_wake_up(lh->owner);

		UNLOCK_TXNH(txnh);
		spin_unlock_znode(node);

		/* @atomf is "small" and @atomh is "large", by
		   definition. Small atom is destroyed and large is unlocked
		   inside capture_fuse_into()
		*/
		capture_fuse_into(atomf, atomh);

		return -EAGAIN;
	}

	UNLOCK_ATOM(atomh);

	if (repeat) {
fail:
		UNLOCK_TXNH(txnh);
		spin_unlock_znode(node);
		return -EAGAIN;
	}

	return 0;
}

/* This is the interface to capture unformatted nodes via their struct page
   reference. */
int
try_capture_page(struct page *pg, znode_lock_mode lock_mode, int non_blocking)
{
	int ret;
	jnode *node;

	assert("umka-292", pg != NULL);
	assert("nikita-2597", PageLocked(pg));

	if (IS_ERR(node = jnode_of_page(pg))) {
		return PTR_ERR(node);
	}

	LOCK_JNODE(node);
	reiser4_unlock_page(pg);

	ret = try_capture(node, lock_mode, non_blocking ? TXN_CAPTURE_NONBLOCKING : 0);
	if (ret == 0) {
		UNLOCK_JNODE(node);
	}
	jput(node);
	reiser4_lock_page(pg);
	return ret;
}

/* This interface is used by flush routines when they need to prevent an atom from
   committing while they perform early flushing.  The node is already captured but the
   txnh is not. */
int
attach_txnh_to_node(txn_handle * txnh, jnode * node, txn_flags flags)
{
	txn_atom *atom;
	int ret = 0;

	assert("jmacd-77917", spin_txnh_is_not_locked(txnh));
	assert("jmacd-7791724897", spin_jnode_is_not_locked(node));
	assert("jmacd-77918", txnh->atom == NULL);

	LOCK_JNODE(node);
	LOCK_TXNH(txnh);

	atom = atom_locked_by_jnode(node);

	/* Atom can commit at this point. */
	if (atom == NULL) {
		ret = -ENOENT;
		goto fail_unlock;
	}

	atom->flags |= flags;

	capture_assign_txnh_nolock(atom, txnh);

	UNLOCK_ATOM(atom);
fail_unlock:
	UNLOCK_TXNH(txnh);
	UNLOCK_JNODE(node);
	return ret;
}

/* This informs the transaction manager when a node is deleted.  Add the block to the
   atom's delete set and uncapture the block.  Handles the EAGAIN result from
   blocknr_set_add_block, which is returned by blocknr_set_add when it releases the atom
   lock to perform an allocation.  The atom could fuse while this lock is held, which is
   why the EAGAIN must be handled by repeating the call to atom_locked_by_jnode.  The
   second call is guaranteed to provide a pre-allocated blocknr_entry so it can only
   "repeat" once.  */
void
uncapture_page(struct page *pg)
{
	int ret;
	jnode *node;
	txn_atom *atom;
	blocknr_set_entry *blocknr_entry = NULL;

	assert("umka-199", pg != NULL);

	write_lock(&pg->mapping->page_lock);
	test_clear_page_dirty(pg);

	list_del(&pg->list);
	list_add(&pg->list, &pg->mapping->clean_pages);

	write_unlock(&pg->mapping->page_lock);

	node = (jnode *) (pg->private);

	if (node == NULL)
		return;

repeat:
	LOCK_JNODE(node);

	eflush_del(node, 1/* page is locked */);
	/*assert ("zam-815", !JF_ISSET(node, JNODE_EFLUSH));*/

	atom = atom_locked_by_jnode(node);
	UNLOCK_JNODE (node);

	if (atom == NULL) {
		assert("jmacd-7111", !jnode_check_dirty(node));
		return;
	}

	if (jnode_is_znode(node)) {
		if (!blocknr_is_fake(jnode_get_block(node))) {
			/* jnode has assigned real disk block. Put it into
			   atom's delete set */
			if (REISER4_DEBUG) {
				struct super_block *s = reiser4_get_current_sb();

				reiser4_spin_lock_sb(s);
				assert("zam-561", *jnode_get_block(node) < reiser4_block_count(s));
				reiser4_spin_unlock_sb(s);
			}
			ret = blocknr_set_add_block(atom, &atom->delete_set, &blocknr_entry, jnode_get_block(node));

			if (ret == -EAGAIN)
				goto repeat;
		} else {
			/* jnode has assigned block which is counted as "fake
			   allocated". Return it back to "free blocks") */
			fake_allocated2free((__u64) 1, BA_FORMATTED, "uncapture_page: formatted fake allocated node");
		}
	}

	assert("jmacd-5177", blocknr_entry == NULL);

	uncapture_block(atom, node);

	UNLOCK_ATOM(atom);
}

/* this is similar to the above uncapture_page, except that it is always called for unformatted jnode which was just emergency
   flushed and therefore does not have a page */
void
uncapture_jnode(jnode *node)
{
	txn_atom *atom;

	jnode_set_clean(node);

	LOCK_JNODE(node);

	atom = atom_locked_by_jnode(node);
	UNLOCK_JNODE (node);

	if (atom == NULL) {
		assert("jmacd-7111", !jnode_check_dirty(node));
		return;
	}

	uncapture_block(atom, node);

	UNLOCK_ATOM(atom);
}

/* No-locking version of assign_txnh.  Sets the transaction handle's atom pointer,
   increases atom refcount, adds to txnh_list. */
static void
capture_assign_txnh_nolock(txn_atom * atom, txn_handle * txnh)
{
	assert("umka-200", atom != NULL);
	assert("umka-201", txnh != NULL);

	assert("jmacd-822", spin_txnh_is_locked(txnh));
	assert("jmacd-823", spin_atom_is_locked(atom));
	assert("jmacd-824", txnh->atom == NULL);

	atomic_inc(&atom->refcount);

	trace_on(TRACE_TXN, "assign txnh atom %u refcount %d\n", atom->atom_id, atomic_read(&atom->refcount));

	txnh->atom = atom;
	txnh_list_push_back(&atom->txnh_list, txnh);
	atom->txnh_count += 1;
}

/* No-locking version of assign_block.  Sets the block's atom pointer, references the
   block, adds it to the clean or dirty capture_jnode list, increments capture_count. */
static void
capture_assign_block_nolock(txn_atom * atom, jnode * node)
{
	assert("umka-202", atom != NULL);
	assert("umka-203", node != NULL);
	assert("jmacd-321", spin_jnode_is_locked(node));
	assert("umka-295", spin_atom_is_locked(atom));
	assert("jmacd-323", node->atom == NULL);

	/* Pointer from jnode to atom is not counted in atom->refcount. */
	node->atom = atom;

	if (jnode_is_dirty(node)) {
		capture_list_push_back(&atom->dirty_nodes[jnode_get_level(node)], node);
	} else {
		capture_list_push_back(&atom->clean_nodes, node);
	}

	atom->capture_count += 1;
	/* reference to jnode is acquired by atom. */
	jref(node);
	ON_DEBUG_CONTEXT(++lock_counters()->t_refs);

	trace_on(TRACE_TXN, "capture %p for atom %u (captured %u)\n", node, atom->atom_id, atom->capture_count);
}

/* Set the dirty status for this jnode.  If the jnode is not already dirty, this involves locking the atom (for its
   capture lists), removing from the clean list and pushing in to the dirty list of the appropriate level. */
void
jnode_set_dirty(jnode * node)
{
	txn_atom * atom;
	struct page *page;

	assert("umka-204", node != NULL);

	/* We get both locks (atom, jnode) before jnode state check because
	   atom_locked_by_jnode may unlock jnode in a process of getting
	   atom spin lock */
	LOCK_JNODE(node);
	atom = atom_locked_by_jnode (node);

	assert("vs-1094", atom);

	if (!jnode_is_dirty(node)) {
		JF_SET(node, JNODE_DIRTY);

		assert("jmacd-3981", jnode_is_dirty(node));

		get_current_context()->nr_marked_dirty ++;

		/* We grab2flush_reserve one additional block only if node was
		   not CREATED and jnode_flush did not sort it into neither
		   relocate set nor overwrite one. If node is in overwrite or
		   relocate set we assume that atom's flush reserved counter was
		   already adjusted. */
		if (!JF_ISSET(node, JNODE_CREATED)
		    && !JF_ISSET(node, JNODE_RELOC) 
		    && !JF_ISSET(node, JNODE_OVRWR))
		{
			assert("vs-1093", !blocknr_is_fake(&node->blocknr));
			grabbed2flush_reserved_nolock(atom, (__u64)1, "jnode_set_dirty: for clean, !created, !reloc and !ovrwr");
		}

		if (!JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
			/* If the atom is not set yet, it will be added to the appropriate list in
			   capture_assign_block_nolock. */
			/* Sometimes a node is set dirty before being captured -- the case for new
			   jnodes.  In that case the jnode will be added to the appropriate list
			   in capture_assign_block_nolock. Another reason not to re-link jnode is
			   that jnode is on a flush queue (see flush.c for details) */

			int level = jnode_get_level(node);

			assert("zam-654", !(JF_ISSET(node, JNODE_OVRWR)
						    && atom->stage >= ASTAGE_PRE_COMMIT));
			assert("nikita-2607", 0 <= level);
			assert("nikita-2606", level <= REAL_MAX_ZTREE_HEIGHT);

			capture_list_remove(node);
			capture_list_push_back(&atom->dirty_nodes[level], node);
		}
	}

	UNLOCK_ATOM (atom);

	page = jnode_page(node);
	if (page != NULL)
		page_cache_get(page);
	else
		/* FIXME-NIKITA dubious. What if jnode doesn't have page,
		   because it was early flushed, or ->releasepaged? */
		assert("zam-596", znode_above_root(JZNODE(node)));

	UNLOCK_JNODE(node);

	/* jnode lock is not needed for the rest of jnode_set_dirty(). */

	if (page != NULL) {
		set_page_dirty_internal(page);
		page_cache_release(page);
	}

	if (jnode_is_znode(node)) {
		reiser4_tree *tree;
		znode *z;

		tree = jnode_get_tree(node);
		z = JZNODE(node);
		/* bump version counter in znode */
		z->version = znode_build_version(tree);
		/* FIXME: This makes no sense, delete it, reenable nikita-1900:

		   the flush code sets a node dirty even though it is read
		   locked... but it captures it first.  However, the new
		   assertion (jmacd-9777) seems to contradict the statement
		   above, that a node is captured before being captured.
		   Perhaps that is no longer true. */
		assert("nikita-1900", znode_is_write_locked(z));
		assert("jmacd-9777", node->atom != NULL);
		ON_DEBUG_MODIFY(z->cksum = znode_is_loaded(z) ? znode_checksum(z) : 0);
	}

}

/* Unset the dirty status for the node if necessary spin locks are already taken */
void
jnode_set_clean_nolock(jnode * node)
{
	txn_atom *atom = node->atom;

	assert("zam-748", spin_jnode_is_locked(node));
	assert("zam-750", ergo(atom, spin_atom_is_locked(atom)));

	if (jnode_is_dirty(node)) {

		JF_CLR(node, JNODE_DIRTY);

		assert("jmacd-9366", !jnode_is_dirty(node));

#if REISER4_DEBUG_MODIFY
		if (jnode_is_znode(node) && jnode_is_loaded(node))
			JZNODE(node)->cksum = znode_checksum(JZNODE(node));
#endif

		/*trace_on (TRACE_FLUSH, "clean %sformatted node %p\n", 
		   jnode_is_unformatted (node) ? "un" : "", node); */
	}

	/* do not steal nodes from flush queue */
	if (!JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
		/* Now it's possible that atom may be NULL, in case this was called
		   from invalidate page */
		if (atom != NULL) {

			capture_list_remove_clean(node);
			capture_list_push_front(&atom->clean_nodes, node);
		}
	}
}

/* Unset the dirty status for this jnode.  If the jnode is dirty, this involves locking the atom (for its capture
   lists), removing from the dirty_nodes list and pushing in to the clean list. */
void
jnode_set_clean(jnode * node)
{
	txn_atom *atom;

	assert("umka-205", node != NULL);
	assert("jmacd-1083", spin_jnode_is_not_locked(node));

	LOCK_JNODE(node);

	atom = atom_locked_by_jnode(node);

	jnode_set_clean_nolock(node);

	if (atom)
		UNLOCK_ATOM(atom);

	UNLOCK_JNODE(node);
}

/* move jnode to atom's overwrite set */
void jnode_set_wander (jnode * node)
{
	txn_atom * atom;

	assert("nikita-2431", node != NULL);
	assert("nikita-2432", !JF_ISSET(node, JNODE_RELOC));
	assert("zam-897", !JF_ISSET(node, JNODE_FLUSH_QUEUED));

	atom = node->atom;

	assert("zam-895", atom != NULL);
	assert("zam-894", spin_atom_is_locked(atom) || atom->stage >= ASTAGE_PRE_COMMIT);

	capture_list_remove_clean(node);
	capture_list_push_back(&atom->ovrwr_nodes, node);
	JF_SET(node, JNODE_OVRWR);
}

/* This function assigns a block to an atom, but first it must obtain the atom lock.  If
   the atom lock is busy, it returns -EAGAIN to avoid deadlock with a fusing atom.  Since
   the transaction handle is currently open, we know the atom must also be open. */
static int
capture_assign_block(txn_handle * txnh, jnode * node)
{
	txn_atom *atom;

	assert("umka-206", txnh != NULL);
	assert("umka-207", node != NULL);

	atom = txnh->atom;

	assert("umka-297", atom != NULL);

	if (!spin_trylock_atom(atom)) {

		/* EAGAIN releases locks. */
		UNLOCK_TXNH(txnh);
		UNLOCK_JNODE(node);

		/* NOTE-NIKITA Busy loop here? Look at the comment in
		   capture_assign_txnh(). */
		return -EAGAIN;

	} else {

		assert("jmacd-19", atom_isopen(atom));

		/* Add page to capture list. */
		capture_assign_block_nolock(atom, node);

		/* Success holds onto jnode & txnh locks.  Unlock atom. */
		UNLOCK_ATOM(atom);
		return 0;
	}
}

/* This function assigns a handle to an atom, but first it must obtain the atom lock.  If
   the atom is busy, it returns -EAGAIN to avoid deadlock with a fusing atom.  Unlike
   capture_assign_block, the atom may be closed but we cannot know this until the atom is
   locked.  If the atom is closed and the request is to read, it is as if the block is
   unmodified and the request is satisified without actually assigning the transaction
   handle.  If the atom is closed and the handle requests to write the block, then
   initiate copy-on-capture.
*/
static int
capture_assign_txnh(jnode * node, txn_handle * txnh, txn_capture mode)
{
	txn_atom *atom;

	assert("umka-208", node != NULL);
	assert("umka-209", txnh != NULL);

	atom = node->atom;

	assert("umka-298", atom != NULL);

	if (!spin_trylock_atom(atom)) {

		/* EAGAIN releases locks. */
		UNLOCK_JNODE(node);
		UNLOCK_TXNH(txnh);

		/* NOTE-NIKITA it looks like we have busy loop on atom spin
		   lock here. We cannot simply acquire and immediately release
		   atom spin lock here to avoid it because fusion can
		   invalidate atom object. The only way to synchronise against
		   this is jnode spin lock. Probably, atom->refcounter should
		   be used as real reference counter protecting atom from
		   destruction. */
		return -EAGAIN;

	} else if (atom->stage == ASTAGE_CAPTURE_WAIT) {

		/* The atom could be blocking requests--this is the first chance we've had
		   to test it.  Since this txnh is not yet assigned, the fuse_wait logic
		   is not to avoid deadlock, its just waiting.  Releases all three locks
		   and returns EAGAIN. */

		return capture_fuse_wait(node, txnh, atom, NULL, mode);

	} else if (atom->stage > ASTAGE_CAPTURE_WAIT) {

		/* The block is involved with a committing atom. */
		if (CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC) {

			/* A read request for a committing block can be satisfied w/o
			   COPY-ON-CAPTURE. */

			/* Success holds onto the jnode & txnh lock.  Continue to unlock
			   atom below. */

		} else {

			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			   releases all three locks. */
			return capture_copy(node, txnh, atom, NULL, mode);
		}

	} else {

		assert("jmacd-160", atom->stage == ASTAGE_CAPTURE_FUSE);

		/* Add txnh to active list. */
		capture_assign_txnh_nolock(atom, txnh);

		/* Success holds onto the jnode & txnh lock.  Continue to unlock atom
		   below. */
	}

	/* Unlock the atom */
	UNLOCK_ATOM(atom);
	return 0;
}

int
capture_super_block(struct super_block *s)
{
	int result;
	znode *fake;
	lock_handle lh;

	init_lh(&lh);
	result = get_fake_znode(get_tree(s), 
				ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI, &lh);
	if (result)
		return result;

	fake = lh.node;
	/* Grabbing one block for superblock */
	if ((result = reiser4_grab_space_force((__u64)1, BA_RESERVED, "capture_super_block")) != 0)
		return result;
	
	znode_set_dirty(fake);

	done_lh(&lh);
	return 0;
}

/* Wakeup every handle on the atom's WAITFOR list */
/* Audited by: umka (2002.06.13) */
static void
wakeup_atom_waitfor_list(txn_atom * atom)
{
	txn_wait_links *wlinks;

	assert("umka-210", atom != NULL);

	/* atom is locked */
	for (wlinks = fwaitfor_list_front(&atom->fwaitfor_list);
	     /**/ !fwaitfor_list_end(&atom->fwaitfor_list, wlinks); wlinks = fwaitfor_list_next(wlinks)) {

		/* Wake up. */
		reiser4_wake_up(wlinks->_lock_stack);
	}
}

/* Wakeup every handle on the atom's WAITING list */
/* Audited by: umka (2002.06.13) */
static void
wakeup_atom_waiting_list(txn_atom * atom)
{
	txn_wait_links *wlinks;

	assert("umka-211", atom != NULL);

	/* atom is locked */
	for (wlinks = fwaiting_list_front(&atom->fwaiting_list);
	     /**/ !fwaiting_list_end(&atom->fwaiting_list, wlinks); wlinks = fwaiting_list_next(wlinks)) {

		/* Wake up. */
		reiser4_wake_up(wlinks->_lock_stack);
	}
}

/* The general purpose of this function is to wait on the first of two possible events.
   The situation is that a handle (and its atom atomh) is blocked trying to capture a
   block (i.e., node) but the node's atom (atomf) is in the CAPTURE_WAIT state.  The
   handle's atom (atomh) is not in the CAPTURE_WAIT state.  However, atomh could fuse with
   another atom or, due to age, enter the CAPTURE_WAIT state itself, at which point it
   needs to unblock the handle to avoid deadlock.  When the txnh is unblocked it will
   proceed and fuse the two atoms in the CAPTURE_WAIT state.
  
   In other words, if either atomh or atomf change state, the handle will be awakened,
   thus there are two lists per atom: WAITING and WAITFOR.
   
   This is also called by capture_assign_txnh with (atomh == NULL) to wait for atomf to
   close but it is not assigned to an atom of its own.
  
   Lock ordering in this method: all four locks are held: JNODE_LOCK, TXNH_LOCK,
   BOTH_ATOM_LOCKS.  Result: all four locks are released.
*/
static int
capture_fuse_wait(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode)
{
	int ret;

	/* Initialize the waiting list links. */
	txn_wait_links wlinks;

	assert("umka-212", node != NULL);
	assert("umka-213", txnh != NULL);
	assert("umka-214", atomf != NULL);

	if ((mode & TXN_CAPTURE_NONBLOCKING) != 0) {
		UNLOCK_JNODE(node);
		UNLOCK_TXNH(txnh);
		UNLOCK_ATOM(atomf);

		if (atomh) {
			UNLOCK_ATOM(atomh);
		}

		trace_on(TRACE_TXN, "thread %u nonblocking on atom %u\n", current->pid, atomf->atom_id);

		return -EAGAIN;
	}

	init_wlinks(&wlinks);

	/* We do not need the node lock. */
	UNLOCK_JNODE(node);

	/* Add txnh to atomf's waitfor list, unlock atomf. */
	fwaitfor_list_push_back(&atomf->fwaitfor_list, &wlinks);
	atomic_inc(&atomf->refcount);
	UNLOCK_ATOM(atomf);

	if (atomh) {
		/* Add txnh to atomh's waiting list, unlock atomh. */
		fwaiting_list_push_back(&atomh->fwaiting_list, &wlinks);
		atomic_inc(&atomh->refcount);
		UNLOCK_ATOM(atomh);
	}

	trace_on(TRACE_TXN, "thread %u waitfor %u waiting %u\n", current->pid,
		 atomf->atom_id, atomh ? atomh->atom_id : 0);

	/* Go to sleep. */
	UNLOCK_TXNH(txnh);

	if ((ret = prepare_to_sleep(wlinks._lock_stack)) != 0) {
		trace_on(TRACE_TXN, "thread %u deadlock blocking on atom %u\n", current->pid, atomf->atom_id);
	} else {
		ret = go_to_sleep(wlinks._lock_stack, ADD_TO_SLEPT_IN_WAIT_ATOM);

		if (ret == 0) {
			ret = -EAGAIN;
		}

		trace_on(TRACE_TXN, "thread %u wakeup %u waiting %u\n",
			 current->pid, atomf->atom_id, atomh ? atomh->atom_id : 0);
	}

	/* Remove from the waitfor list. */
	LOCK_ATOM(atomf);
	fwaitfor_list_remove(&wlinks);
	atom_dec_and_unlock(atomf);

	if (atomh) {
		/* Remove from the waiting list. */
		LOCK_ATOM(atomh);
		fwaiting_list_remove(&wlinks);
		atom_dec_and_unlock(atomh);
	}

	assert("nikita-2186", ergo(ret, spin_jnode_is_not_locked(node)));
	return ret;
}

/* Perform the necessary work to prepare for fusing two atoms, which involves acquiring two
   atom locks in the proper order.  If one of the node's atom is blocking fusion (i.e., it
   is in the CAPTURE_WAIT stage) and the handle's atom is not then the handle's request is
   put to sleep.  If the node's atom is committing, then the node can be copy-on-captured.
   Otherwise, pick the atom with fewer pointers to be fused into the atom with more
   pointer and call capture_fuse_into.
*/
/* Audited by: umka (2002.06.13) */
static int
capture_init_fusion(jnode * node, txn_handle * txnh, txn_capture mode)
{
	txn_atom *atomf;
	txn_atom *atomh;

	assert("umka-216", txnh != NULL);
	assert("umka-217", node != NULL);

	atomh = txnh->atom;
	atomf = node->atom;

	/* Have to perform two trylocks here. */
	if (!spin_trylock_atom(atomf)) {
		goto noatomf_out;
	}

	if (!spin_trylock_atom(atomh)) {
		/* Release locks and try again. */
		UNLOCK_ATOM(atomf);
noatomf_out:
		UNLOCK_JNODE(node);
		UNLOCK_TXNH(txnh);
		return -EAGAIN;
	}

	/* The txnh atom must still be open (since the txnh is active)...  the node atom may
	   be in some later stage (checked next). */
	assert("jmacd-20", atom_isopen(atomh));

	/* If the node atom is in the FUSE_WAIT state then we should wait, except to
	   avoid deadlock we still must fuse if the txnh atom is also in FUSE_WAIT. */
	if (atomf->stage == ASTAGE_CAPTURE_WAIT && atomh->stage != ASTAGE_CAPTURE_WAIT) {

		/* This unlocks all four locks and returns EAGAIN. */
		return capture_fuse_wait(node, txnh, atomf, atomh, mode);

	} else if (atomf->stage > ASTAGE_CAPTURE_WAIT) {

		/* The block is involved with a comitting atom. */
		if (CAPTURE_TYPE(mode) == TXN_CAPTURE_READ_ATOMIC) {
			/* A read request for a committing block can be satisfied w/o
			   COPY-ON-CAPTURE.  Success holds onto the jnode & txnh
			   locks. */
			UNLOCK_ATOM(atomf);
			UNLOCK_ATOM(atomh);
			return 0;
		} else {
			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			   releases all four locks. */
			return capture_copy(node, txnh, atomf, atomh, mode);
		}
	}

	/* Because atomf's stage <= CAPTURE_WAIT */
	assert("jmacd-175", atom_isopen(atomf));

	/* If we got here its either because the atomh is in CAPTURE_WAIT or because the
	   atomf is not in CAPTURE_WAIT. */
	assert("jmacd-176", (atomh->stage == ASTAGE_CAPTURE_WAIT || atomf->stage != ASTAGE_CAPTURE_WAIT));

	/* Now release the txnh lock: only holding the atoms at this point. */
	UNLOCK_TXNH(txnh);
	UNLOCK_JNODE(node);

	/* Decide which should be kept and which should be merged. */
	if (atom_pointer_count(atomf) < atom_pointer_count(atomh)) {
		capture_fuse_into(atomf, atomh);
	} else {
		capture_fuse_into(atomh, atomf);
	}

	/* Atoms are unlocked in capture_fuse_into.  No locks held. */
	return -EAGAIN;
}

/* This function splices together two jnode lists (small and large) and sets all jnodes in
   the small list to point to the large atom.  Returns the length of the list. */
static int
capture_fuse_jnode_lists(txn_atom * large, capture_list_head * large_head, capture_list_head * small_head)
{
	int count = 0;
	jnode *node;

	assert("umka-218", large != NULL);
	assert("umka-219", large_head != NULL);
	assert("umka-220", small_head != NULL);

	/* For every jnode on small's capture list... */
	for (node = capture_list_front(small_head);
	     /**/ !capture_list_end(small_head, node); node = capture_list_next(node)) {

		count += 1;

		/* With the jnode lock held, update atom pointer. */
		UNDER_SPIN_VOID(jnode, node, node->atom = large);
	}

	/* Splice the lists. */
	capture_list_splice(large_head, small_head);

	return count;
}

/* This function splices together two txnh lists (small and large) and sets all txn handles in
   the small list to point to the large atom.  Returns the length of the list. */
/* Audited by: umka (2002.06.13) */
static int
capture_fuse_txnh_lists(txn_atom * large, txnh_list_head * large_head, txnh_list_head * small_head)
{
	int count = 0;
	txn_handle *txnh;

	assert("umka-221", large != NULL);
	assert("umka-222", large_head != NULL);
	assert("umka-223", small_head != NULL);

	/* Adjust every txnh to the new atom. */
	for (txnh = txnh_list_front(small_head); /**/ !txnh_list_end(small_head, txnh); txnh = txnh_list_next(txnh)) {

		count += 1;

		/* With the txnh lock held, update atom pointer. */
		LOCK_TXNH(txnh);
		txnh->atom = large;
		UNLOCK_TXNH(txnh);
	}

	/* Splice the txn_handle list. */
	txnh_list_splice(large_head, small_head);

	return count;
}

/* This function fuses two atoms.  The captured nodes and handles belonging to SMALL are
   added to LARGE and their ->atom pointers are all updated.  The associated counts are
   updated as well, and any waiting handles belonging to either are awakened.  Finally the
   smaller atom's refcount is decremented.
*/
static void
capture_fuse_into(txn_atom * small, txn_atom * large)
{
	int level;
	unsigned zcount = 0;
	unsigned tcount = 0;

	assert("umka-224", small != NULL);
	assert("umka-225", small != NULL);

	assert("umka-299", spin_atom_is_locked(large));
	assert("umka-300", spin_atom_is_locked(small));

	assert("jmacd-201", atom_isopen(small));
	assert("jmacd-202", atom_isopen(large));

	trace_on(TRACE_TXN, "fuse atom %u into %u\n", small->atom_id, large->atom_id);

	/* Splice and update the per-level dirty jnode lists */
	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		zcount += capture_fuse_jnode_lists(large, &large->dirty_nodes[level], &small->dirty_nodes[level]);
	}

	/* Splice and update the [clean,dirty] jnode and txnh lists */
	zcount += capture_fuse_jnode_lists(large, &large->clean_nodes, &small->clean_nodes);
	zcount += capture_fuse_jnode_lists(large, &large->ovrwr_nodes, &small->ovrwr_nodes);
	tcount += capture_fuse_txnh_lists(large, &large->txnh_list, &small->txnh_list);

	/* Check our accounting. */
	assert("jmacd-1063", zcount + small->num_queued == small->capture_count);
	assert("jmacd-1065", tcount == small->txnh_count);

	/* sum numbers of waiters threads */
	large->nr_waiters += small->nr_waiters;
	small->nr_waiters = 0;

	/* splice flush queues */
	fuse_fq(large, small);

	/* count flushers in result atom */
	large->nr_flushers += small->nr_flushers;
	small->nr_flushers = 0;

	/* Transfer list counts to large. */
	large->txnh_count += small->txnh_count;
	large->capture_count += small->capture_count;

	/* Add all txnh references to large. */
	atomic_add(small->txnh_count, &large->refcount);
	atomic_sub(small->txnh_count, &small->refcount);

	/* Reset small counts */
	small->txnh_count = 0;
	small->capture_count = 0;

	/* Assign the oldest start_time, merge flags. */
	large->start_time = min(large->start_time, small->start_time);
	large->flags |= small->flags;

	/* Merge blocknr sets. */
	blocknr_set_merge(&small->delete_set, &large->delete_set);
	blocknr_set_merge(&small->wandered_map, &large->wandered_map);

	/* Merge allocated/deleted file counts */
	large->nr_objects_deleted += small->nr_objects_deleted;
	large->nr_objects_created += small->nr_objects_created;

	small->nr_objects_deleted = 0;
	small->nr_objects_created = 0;

	/* Merge allocated blocks counts */
	large->nr_blocks_allocated += small->nr_blocks_allocated;

	/* Merge blocks reserved for overwrite set. */
	large->flush_reserved += small->flush_reserved;
	small->flush_reserved = 0;
	    
	/* Notify any waiters--small needs to unload its wait lists.  Waiters actually remove
	   themselves from the list before returning from the fuse_wait function. */
	wakeup_atom_waitfor_list(small);
	wakeup_atom_waiting_list(small);

	if (large->stage < small->stage) {
		/* Large only needs to notify if it has changed state. */
		large->stage = small->stage;
		wakeup_atom_waitfor_list(large);
		wakeup_atom_waiting_list(large);
	}

	small->stage = ASTAGE_FUSED;

	/* Unlock atoms */
	UNLOCK_ATOM(large);
	atom_dec_and_unlock(small);
}

/* TXNMGR STUFF */

/* Perform copy-on-capture of a block.  INCOMPLETE CODE. */
static int
capture_copy(jnode * node, txn_handle * txnh, txn_atom * atomf, txn_atom * atomh, txn_capture mode)
{
	trace_on(TRACE_TXN, "capture_copy: fuse wait\n");

	return capture_fuse_wait(node, txnh, atomf, atomh, mode);
#if 0
	/* The txnh and its (possibly NULL) atom's locks are not needed at this
	   point. */

	UNLOCK_TXNH(txnh);

	if (atomh != NULL) {
		UNLOCK_ATOM(atomh);
	}

	uncapture_block(atomf, node);

	/* FIXME_JMACD What happens here?  Changes to: zstate?, buffer, data is copied -josh */

	/* EAGAIN implies all locks are released. */
	UNLOCK_ATOM(atomf);
	ON_SMP(assert("nikita-2187", spin_jnode_is_not_locked(node)));
#endif
	return -EIO;
}

/* Release a block from the atom, reversing the effects of being captured.
   Currently this is only called when the atom commits. */
void
uncapture_block(txn_atom * atom, jnode * node)
{
	assert("umka-226", node != NULL);
	assert("umka-228", atom != NULL);

	assert("jmacd-1021", node->atom == atom);
	assert("jmacd-1022", spin_jnode_is_not_locked(node));
	assert("jmacd-1023", atom_is_protected(atom));

	/*trace_on (TRACE_TXN, "uncapture %p from atom %u (captured %u)\n", node, atom->atom_id, atom->capture_count); */

	LOCK_JNODE(node);

	JF_CLR(node, JNODE_DIRTY);
	JF_CLR(node, JNODE_RELOC);
	JF_CLR(node, JNODE_OVRWR);
	JF_CLR(node, JNODE_CREATED);
#if REISER4_DEBUG
	node->written = 0;
#endif

	if (!JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
		/* do not remove jnode from capture list if it is on flush queue */
		capture_list_remove_clean(node);
		atom->capture_count -= 1;
		node->atom = NULL;
		UNLOCK_JNODE(node);

		/*trace_if (TRACE_FLUSH, print_page ("uncapture", node->pg)); */

		jput(node);
	} else
		UNLOCK_JNODE(node);

	ON_DEBUG_CONTEXT(--lock_counters()->t_refs);
}

/* Unconditional insert of jnode into atom's clean list. Currently used in
   bitmap-based allocator code for adding modified bitmap blocks the
   transaction. @atom and @node are spin locked */
void
insert_into_atom_ovrwr_list(txn_atom * atom, jnode * node)
{
	assert("zam-538", spin_atom_is_locked(atom) || atom->stage >= ASTAGE_PRE_COMMIT);
	assert("zam-539", spin_jnode_is_locked(node));
	assert("zam-899", JF_ISSET(node, JNODE_OVRWR));
	assert("zam-543", node->atom == NULL);

	capture_list_push_front(&atom->ovrwr_nodes, node);
	jref(node);
	node->atom = atom;
	atom->capture_count++;
}

/* return 1 if two dirty jnodes belong to one atom, 0 - otherwise */
int
jnodes_of_one_atom(jnode * j1, jnode * j2)
{
	int ret = 0;
	int finish = 0;

	assert("zam-9003", j1 != j2);
	/*assert ("zam-9004", jnode_check_dirty (j1)); */
	assert("zam-9005", jnode_check_dirty(j2));

	do {
		LOCK_JNODE(j1);
		assert("zam-9001", j1->atom != NULL);
		if (spin_trylock_jnode(j2)) {
			assert("zam-9002", j2->atom != NULL);
			ret = (j2->atom == j1->atom);
			finish = 1;

			UNLOCK_JNODE(j2);
		}
		UNLOCK_JNODE(j1);
	} while (!finish);

	return ret;
}

/* DEBUG HELP */

#if REISER4_DEBUG_OUTPUT
void
info_atom(const char *prefix, txn_atom * atom)
{
	if (atom == NULL) {
		printk("%s: no atom\n", prefix);
		return;
	}

	printk("%s: refcount: %i id: %i flags: %x txnh_count: %i"
	       " capture_count: %i stage: %x start: %lu\n", prefix,
	       atomic_read(&atom->refcount), atom->atom_id, atom->flags, atom->txnh_count,
	       atom->capture_count, atom->stage, atom->start_time);
}

void
print_atom(const char *prefix, txn_atom * atom)
{
	jnode *pos_in_atom;
	char list[32];
	int level;

	assert("umka-229", atom != NULL);

	info_atom(prefix, atom);

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {

		sprintf(list, "capture level %d", level);

		for (pos_in_atom =
		     capture_list_front(&atom->dirty_nodes[level]);
		     /**/ !capture_list_end(&atom->dirty_nodes[level],
					    pos_in_atom); pos_in_atom = capture_list_next(pos_in_atom)) {

			info_jnode(list, pos_in_atom);
			printk("\n");
		}
	}

	for (pos_in_atom = capture_list_front(&atom->clean_nodes);
	     /**/ !capture_list_end(&atom->clean_nodes, pos_in_atom); pos_in_atom = capture_list_next(pos_in_atom)) {

		info_jnode("clean", pos_in_atom);
		printk("\n");
	}
}
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
