/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* We assume that reiserfs tree operations, such as modification (balancing),
   lookup (search_by_key) and maybe super balancing (squeeze left) should
   require locking of small or huge chunks of tree nodes to perform these
   operations.  We need to introduce per node lock object (it will be part of
   znode object which is associated with each node (buffer) in cache), and we
   need special techniques to avoid deadlocks.
  
   V4 LOCKING ALGORITHM DESCRIPTION
  
   Suppose we have a set of processes which do lock (R/W) of tree nodes. Each
   process have a set (m.b. empty) of already locked nodes ("process locked
   set"). Each process may have a pending lock request to the node locked by
   another process (this request cannot be satisfied without unlocking the
   node because request mode and node lock mode are not compatible).

   The deadlock situation appears when we have a loop constructed from
   process locked sets and lock request vectors.

  
   NOTE: we can have loops there because initial strict tree structure is
   extended in reiser4 tree by having of sibling pointers which connect
   neighboring cached nodes.
  
   +-P1-+          +-P3-+
   |+--+|   V1     |+--+|
   ||N1|| -------> ||N3||
   |+--+|          |+--+|
   +----+          +----+
     ^               |
     |V2             |V3
     |               v
   +---------P2---------+
   |+--+            +--+|
   ||N2|  --------  |N4||
   |+--+            +--+|
   +--------------------+
  
   The basic deadlock avoidance mechanism used by everyone is to acquire locks
   only in one predetermined order -- it makes loop described above
   impossible.  The problem we have with chosen balancing algorithm is that we
   could not find predetermined order of locking there. Tree lookup goes from
   top to bottom locking more than one node at time, tree change (AKA
   balancing) goes from bottom, two (or more) tree change operations may lock
   same set of nodes in different orders. So, basic deadlock avoidance scheme
   does not work there.
  
   Instead of having one predetermined order of getting locks for all
   processes we divide all processes into two classes, and each class
   has its own order of locking.
  
   Processes of first class (we call it L-class) take locks in from top to
   tree bottom and from right to left, processes from second class (H-class)
   take locks from bottom to top and left to right. It is not a complete
   definition for the order of locking. Exact definitions will be given later,
   now we should know that some locking orders are predefined for both
   classes, L and H.
  
   How does it help to avoid deadlocks ?
  
   Suppose we have a deadlock with n processes. Processes from one
   class never deadlock because they take locks in one consistent
   order.
  
   So, any possible deadlock loop must have L-class processes as well as H-class
   ones. This lock.c contains code that prevents deadlocks between L- and
   H-class processes by using process lock priorities.
  
   We assign low priority to all L-class processes and high priority to all
   processes from class H. There are no other lock priority levels except low
   and high. We know that any deadlock loop contains at least one node locked
   by L-class process and requested by H-class process. It is enough to avoid
   deadlocks if this situation is caught and resolved.
  
   V4 DEADLOCK PREVENTION ALGORITHM IMPLEMENTATION.
  
   The deadlock prevention algorithm is based on comparing of
   priorities of node owners (processes which keep znode locked) and
   requesters (processes which want to acquire a lock on znode).  We
   implement a scheme where low-priority owners yield locks to
   high-priority requesters. We created a signal passing system that
   is used to ask low-priority processes to yield one or more locked
   znodes.
  
   The condition when a znode needs to change its owners is described by the
   following formula:

   #############################################
   #                                           #
   # (number of high-priority requesters) >  0 #
   #                AND                        #
   # (numbers of high-priority owners)    == 0 #
   #                                           #
   #############################################

    
     FIXME: It is a bit different from initial statement of resolving of all
     situations where we have a node locked by low-priority process and
     requested by high-priority one.
    
     The difference is that in current implementation a low-priority process
     delays node releasing if another high-priority process owns this node.
    
     It is not proved yet that modified deadlock check prevents deadlocks. It
     is possible we should return to strict deadlock check formula as
     described above in the proof (i.e. any low pri owners release locks when
     high-pri lock request arrives, unrelated to another high-pri owner
     existence). -zam.

   It is enough to avoid deadlocks if we prevent any low-priority process from
   falling asleep if its locked set contains a node which satisfies the
   deadlock condition.
  
   That condition is implicitly or explicitly checked in all places where new
   high-priority request may be added or removed from node request queue or
   high-priority process takes or releases a lock on node. The main
   goal of these checks is to never lose the moment when node becomes "has
   wrong owners" and send "must-yield-this-lock" signals to its low-pri owners
   at that time.
  
   The information about received signals is stored in the per-process
   structure (lock stack) and analyzed before low-priority process is going to
   sleep but after a "fast" attempt to lock a node fails. Any signal wakes
   sleeping process up and forces him to re-check lock status and received
   signal info. If "must-yield-this-lock" signals were received the locking
   primitive (longterm_lock_znode()) fails with -EDEADLK error code.
  
   USING OF V4 LOCKING ALGORITHM IN V4 TREE TRAVERSAL CODE
  
   Let's complete definitions of lock orders used by L- and H-class
   processes.
  
   1. H-class:
      We count all tree nodes as shown at the picture:

  .
  .
  .
 (m+1)
  |  \
  |    \
 (n+1)  (n+2) ..                     (m)
  | \   | \                           | \
  1  2  3  4  5  6  7  8  9  10 .. (n-1) (n)

   i.e. leaves first, then all nodes at one level above and so on until tree
   root. H-class process acquires (and requests) locks in increasing order of
   assigned numbers. Current implementation of v4 balancing follows these
   rules.

   2. L-class:
   Count all tree nodes in recursive order:

 .
 .
 13---
 |     \
 8      12
 | \      \
 3   7-    (11)
 |\  |\ \  | \
 1 2 4 5 6 9 (10) ...

   and L-class process requests locks in decreasing order of assigned
   numbers. You see that any movement left or down decreases the node number.
   Recursive counting was used because one situation in
   reiser4_get_left_neighbor() where process does low-pri lock request (to
   left) after locking of a set of nodes as high-pri process in upward
   direction.

 x <- c
      ^
      |
      b
      ^
      |
      a

   Nodes a-b-c are locked in accordance with H-class rules, then the process
   changes its class and tries to lock node x. Recursive order guarantees that
   node x will have a number less than all already locked nodes (a-b-c)
   numbers.
  
   V4 LOCKING DRAWBACKS
   
   The deadlock prevention code may signal about possible deadlock in the
   situation where is no deadlock actually. 
  
   Suppose we have a tree balancing operation in progress. One level is
   completely balanced and we are going to propagate tree changes on the level
   above. We locked leftmost node in the set of all immediate parents of all
   modified nodes (by current balancing operation) on already balanced level
   as it requires by locking order defined for H-class (high-priority)
   processes.  At this moment we want to look on left neighbor for free space
   on this level and thus avoid new node allocation. That locking is done in
   the order not defined for H-class, but for L-class process. It means we
   should change the process locking priority and make process locked set
   available to high-priority requests from any balancing that goes upward. It
   is not good.  First, for most cases it is not a deadlock situation; second,
   locking of left neighbor node is impossible in that situation. -EDEADLK is
   returned from longterm_lock_znode() until we release all nodes requested by
   high-pri processes, we can't do it because nodes we need to unlock contain
   modified data, we can't unroll changes due to reiser4 journal limits.
  
    +-+     +-+
    |X| <-- | |
    +-+     +-+--LOW-PRI-----+
            |already balanced|
            +----------------+
                ^
                |
              +----HI-PRI-------+
              |another balancing|
              +-----------------+
  
   The solution is not to lock nodes to left in described situation. Current
   implementation of reiser4 tree balancing algorithms uses non-blocking
   try_lock call in that case.
  
   LOCK STRUCTURES DESCRIPTION
  
   The following data structures are used in reiser4 locking
   implementation:

   Lock object is a znode. All fields related to long-term locking are grouped
   into znode->lock embedded object.
  
   Lock stack is a per thread object.  It owns all znodes locked by the
   thread. One znode may be locked by several threads in case of read lock or
   one znode may be write locked by one thread several times. The special link
   objects (lock handles) support n<->m relation between znodes and lock
   owners.

   <Thread 1>                       <Thread 2>

   +---------+                     +---------+ 
   |  LS1    |		           |  LS2    |
   +---------+			   +---------+
       ^                                ^
       |---------------+                +----------+
       v               v                v          v
   +---------+      +---------+    +---------+   +---------+
   |  LH1    |      |   LH2   |	   |  LH3    |   |   LH4   |
   +---------+	    +---------+	   +---------+   +---------+
       ^                   ^            ^           ^
       |                   +------------+           |
       v                   v                        v
   +---------+      +---------+                  +---------+
   |  Z1     |	    |	Z2    |                  |  Z3     |
   +---------+	    +---------+                  +---------+ 

   Thread 1 locked znodes Z1 and Z2, thread 2 locked znodes Z2 and Z3. The
   picture above shows that lock stack LS1 has a list of 2 lock handles LH1
   and LH2, lock stack LS2 has a list with lock handles LH3 and LH4 on it.
   Znode Z1 is locked by only one thread, znode has only one lock handle LH1
   on its list, similar situation is for Z3 which is locked by the thread 2
   only. Z2 is locked (for read) twice by different threads and two lock
   handles are on its list. Each lock handle represents a single relation of a
   locking of a znode by a thread. Locking of a znode is an establishing of a
   locking relation between the lock stack and the znode by adding of a new
   lock handle to lists of lock handles: one that list is owned by lock stack
   and it links all lock handles for all znodes locked by the lock stack,
   another list groups all lock handles for all locks stacks which locked the
   znode.

   Yet another relation may exist between znode and lock owners.  If lock
   procedure cannot immediately take lock on an object it adds the lock owner
   on special `requestors' list belongs to znode.  That list represents a
   queue of pending lock requests.  Because one lock owner may request only
   only one lock object in a time, it is a 1->n relation between lock objects
   and a lock owner implemented as it written above. Full information
   (priority, pointers to lock and link objects) about each lock request is
   stored in lock owner structure in `request' field.
  
   SHORT_TERM LOCKING
  
   This is a list of primitive operations over lock stacks / lock handles /
   znodes and locking descriptions for them.

   1. locking / unlocking which is done by two list insertion/deletion is
      protected by znode spinlock.  A simultaneous access to the list owned by
      znode is impossible because the only one spinlock embedded into the
      znode should be taken.  The list owned by lock stack can be modified
      only by thread who owns the lock stack and nobody else can modify/read
      it. There is nothing to be protected by a spinlock or something else.

   2. adding/removing a lock request to/from znode requesters list. The rule
      is that znode spinlock should be taken for this.

   3. accessing a set of lock stacks who locked given znode is done with znode
      spinlock taken. Nobody can lock/unlock znode at this time and modify
      list of lock handles, and, thereby, the set of lock stacks.

   4. Lock stacks can be accessed by from different ways: The thread a lock
      stack belongs to may change lock stack state, lock or unlock
      znodes. Another way is when somebody who keep znode spin-locked accesses
      its lock owners. The parallel access conflicts are solved by using
      atomic variables for lock stack fields and lock stack spinlock.
*/

/* Josh's explanation to Zam on why the locking and capturing code are intertwined.
  
   FIXME: DEADLOCK STILL OBSERVED:!!!
  
   Point 1. The order in which a node is captured matters.  If a read-capture arrives
   before a write-capture, the read-capture may cause no capturing "work" to be done at
   all, whereas the write-capture may cause copy-on-capture to occur.  For this to be
   correct the writer cannot beat the reader to the lock, if they are captured in the
   opposite order.
  
   Point 2. Just as locking can block waiting for the request to be satisfied, capturing
   can block waiting for expired atoms to commit.
  
   Point 3. It is not acceptable to first acquire the lock and then block waiting to
   capture, especially when ignorant of deadlock.  There is no reason to lock until the
   capture has succeeded.  To block in "capture" should be the same as to block waiting
   for a lock, therefore a deadlock condition will cause the capture request to return
   -EDEADLK.
  
   Point 4. It is acceptable to first capture and then wait to lock.  BUT, once the
   capture request succeeds the lock request cannot be satisfied out-of-order.  For
   example, once a read-capture is satisfied no writers may acquire the lock until the
   reader has a chance to read the block.
  
   Point 5. Summary: capture requests must be partially-ordered with respect to lock
   requests.
  
   What this means is for a regular lock_znode request (try_lock is slightly simpler).
  
     1. acquire znode spinlock, check whether the lock request is compatible
        \_ and if not, make request and sleep on lock_stack semaphore
        |_ and when it wakes, go to step #1
     2. perform a try_capture request
        \_ and if it would block, sleep on lock_stack semaphore
        |_ and when it wakes, go to step #1
        |_ if the try_capture request is successful, it returns with
           the znode locked
     3. since try_capture occasionally releases the znode lock due to
        spinlock-ordering constraints (there's a pointer cycle atom->node->atom),
        recheck whether lock request is still compatible.
        \_ and if it is not, same as step #1
     4. before releasing znode spinlock, call lock_object() as before.  */

#include "debug.h"
#include "txnmgr.h"
#include "znode.h"
#include "jnode.h"
#include "tree.h"
#include "plugin/node/node.h"
#include "super.h"

#include <linux/spinlock.h>

#if REISER4_DEBUG
static int request_is_deadlock_safe(znode *, znode_lock_mode, 
				    znode_lock_request);
#endif

#define ADDSTAT(node, counter) 						\
	reiser4_stat_inc_at_level(znode_get_level(node), znode.counter)

/* Returns a lock owner associated with current thread */
lock_stack *
get_current_lock_stack(void)
{
	return &get_current_context()->stack;
}

/* Wakes up all low priority owners informing them about possible deadlock */
static void
wake_up_all_lopri_owners(znode * node)
{
	lock_handle *handle;

	assert("nikita-1824", spin_zlock_is_locked(&node->lock));
	for_all_tslist(owners, &node->lock.owners, handle) {
		spin_lock_stack(handle->owner);

		assert("nikita-1832", handle->node == node);
		/* count this signal in owner->nr_signaled */
		if (!handle->signaled) {
			handle->signaled = 1;
			atomic_inc(&handle->owner->nr_signaled);
		}
		/* Wake up a single process */
		__reiser4_wake_up(handle->owner);

		spin_unlock_stack(handle->owner);
	}
}

/* Adds a lock to a lock owner, which means creating a link to the lock and
   putting the link into the two lists all links are on (the doubly linked list
   that forms the lock_stack, and the doubly linked list of links attached
   to a lock.
*/
static inline void
link_object(lock_handle * handle, lock_stack * owner, znode * node)
{
	assert("jmacd-810", handle->owner == NULL);
	assert("nikita-1828", owner == get_current_lock_stack());
	assert("nikita-1830", spin_zlock_is_locked(&node->lock));

	handle->owner = owner;
	handle->node = node;
	locks_list_push_back(&owner->locks, handle);
	owners_list_push_front(&node->lock.owners, handle);
	handle->signaled = 0;
}

/* Breaks a relation between a lock and its owner */
static inline void
unlink_object(lock_handle * handle)
{
	assert("zam-354", handle->owner != NULL);
	assert("nikita-1608", handle->node != NULL);
	assert("nikita-1633", spin_zlock_is_locked(&handle->node->lock));
	assert("nikita-1829", handle->owner == get_current_lock_stack());

	locks_list_remove_clean(handle);
	owners_list_remove_clean(handle);

	/* indicates that lock handle is free now */
	handle->owner = NULL;
}

/* Actually locks an object knowing that we are able to do this */
static void
lock_object(lock_stack * owner)
{
	lock_request *request;
	znode        *node;
	assert("nikita-1839", owner == get_current_lock_stack());

	request = &owner->request;
	node    = request->node;
	assert("nikita-1834", spin_zlock_is_locked(&node->lock));
	if (request->mode == ZNODE_READ_LOCK) {
		node->lock.nr_readers++;
	} else {
		/* check that we don't switched from read to write lock */
		assert("nikita-1840", node->lock.nr_readers <= 0);
		/* We allow recursive locking; a node can be locked several
		   times for write by same process */
		node->lock.nr_readers--;
	}

	link_object(request->handle, owner, node);

	if (owner->curpri) {
		node->lock.nr_hipri_owners++;
	}
	ON_TRACE(TRACE_LOCKS,
		 "%spri lock: %p node: %p: hipri_owners: %u: nr_readers: %d\n",
		 owner->curpri ? "hi" : "lo", owner, node, node->lock.nr_hipri_owners, node->lock.nr_readers);
}

/* Check for recursive write locking */
static int
recursive(lock_stack * owner)
{
	int ret;
	znode *node;

	node = owner->request.node;

	/* Owners list is not empty for a locked node */
	assert("zam-314", !owners_list_empty(&node->lock.owners));
	assert("nikita-1841", owner == get_current_lock_stack());
	assert("nikita-1848", spin_zlock_is_locked(&node->lock));

	ret = (owners_list_front(&node->lock.owners)->owner == owner);

	/* Recursive read locking should be done usual way */
	assert("zam-315", !ret || owner->request.mode == ZNODE_WRITE_LOCK);
	/* mixing of read/write locks is not allowed */
	assert("zam-341", !ret || znode_is_wlocked(node));

	return ret;
}

#if REISER4_DEBUG
/* Returns true if the lock is held by the calling thread. */
int
znode_is_any_locked(const znode * node)
{
	lock_handle *handle;
	lock_stack *stack;
	int ret;

	if (!znode_is_locked(node)) {
		return 0;
	}

	stack = get_current_lock_stack();

	spin_lock_stack(stack);

	ret = 0;

	for_all_tslist(locks, &stack->locks, handle) {
		if (handle->node == node) {
			ret = 1;
			break;
		}
	}

	spin_unlock_stack(stack);

	return ret;
}

#endif

/* Returns true if a write lock is held by the calling thread. */
int
znode_is_write_locked(const znode * node)
{
	lock_stack *stack;
	lock_handle *handle;

	assert("jmacd-8765", node != NULL);

	if (!znode_is_wlocked(node)) {
		return 0;
	}

	stack = get_current_lock_stack();

	/* If it is write locked, then all owner handles must equal the current stack. */
	handle = owners_list_front(&node->lock.owners);

	return (handle->owner == stack);
}

/* This "deadlock" condition is the essential part of reiser4 locking
   implementation. This condition is checked explicitly by calling
   check_deadlock_condition() or implicitly in all places where znode lock
   state (set of owners and request queue) is changed. Locking code is
   designed to use this condition to trigger procedure of passing object from
   low priority owner(s) to high priority one(s).
  
   The procedure results in passing an event (setting lock_handle->signaled
   flag) and count this event in nr_signaled field of owner's lock stack
   object and wakeup owner's process.
*/
static inline int
check_deadlock_condition(znode * node)
{
	assert("nikita-1833", spin_zlock_is_locked(&node->lock));
	return node->lock.nr_hipri_requests > 0 && node->lock.nr_hipri_owners == 0;
}

/* checks lock/request compatibility */
static int
check_lock_object(lock_stack * owner)
{
	znode *node = owner->request.node;

	assert("nikita-1842", owner == get_current_lock_stack());
	assert("nikita-1843", spin_zlock_is_locked(&node->lock));

	/* See if the node is disconnected. */
	if (unlikely(ZF_ISSET(node, JNODE_IS_DYING))) {
		ON_TRACE(TRACE_LOCKS, "attempt to lock dying znode: %p", node);
		return RETERR(-EINVAL);
	}

	/* Do not ever try to take a lock if we are going in low priority
	   direction and a node have a high priority request without high
	   priority owners. */
	if (unlikely(!owner->curpri && check_deadlock_condition(node))) {
		return RETERR(-EAGAIN);
	}

	if (unlikely(!is_lock_compatible(node, owner->request.mode))) {
		return RETERR(-EAGAIN);
	}

	return 0;
}

static int
can_lock_object(lock_stack * owner)
{
	int result;
	znode *node = owner->request.node;

	result = check_lock_object(owner);
	if (REISER4_STATS && znode_get_level(node) > 0) {
		if (result != 0)
			ADDSTAT(node, lock_contented);
		else
			ADDSTAT(node, lock_uncontented);
	}
	return result;
}

/* Setting of a high priority to the process. It clears "signaled" flags
   because znode locked by high-priority process can't satisfy our "deadlock
   condition". */
static void
set_high_priority(lock_stack * owner)
{
	assert("nikita-1846", owner == get_current_lock_stack());
	/* Do nothing if current priority is already high */
	if (!owner->curpri) {
		/* We don't need locking for owner->locks list, because, this
		 * function is only called with the lock stack of the current
		 * thread, and no other thread can play with owner->locks list
		 * and/or change ->node pointers of lock handles in this list.
		 *
		 * (Interrupts also are not involved.)
		 */
		lock_handle *item = locks_list_front(&owner->locks);
		while (!locks_list_end(&owner->locks, item)) {
			znode *node = item->node;

			LOCK_ZLOCK(&node->lock);

			node->lock.nr_hipri_owners++;

			ON_TRACE(TRACE_LOCKS,
				 "set_hipri lock: %p node: %p: hipri_owners after: %u nr_readers: %d\n",
				 item, node, node->lock.nr_hipri_owners, node->lock.nr_readers);

			/* we can safely set signaled to zero, because
			   previous statement (nr_hipri_owners ++) guarantees
			   that signaled will be never set again. */
			item->signaled = 0;
			UNLOCK_ZLOCK(&node->lock);;

			item = locks_list_next(item);
		}
		owner->curpri = 1;
		atomic_set(&owner->nr_signaled, 0);
	}
}

/* Sets a low priority to the process. */
static void
set_low_priority(lock_stack * owner)
{
	assert("nikita-3075", owner == get_current_lock_stack());
	/* Do nothing if current priority is already low */
	if (owner->curpri) {
		/* scan all locks (lock handles) held by @owner, which is
		   actually current thread, and check whether we are reaching
		   deadlock possibility anywhere.
		*/
		lock_handle *handle = locks_list_front(&owner->locks);
		while (!locks_list_end(&owner->locks, handle)) {
			znode *node = handle->node;
			LOCK_ZLOCK(&node->lock);
			/* this thread just was hipri owner of @node, so
			   nr_hipri_owners has to be greater than zero. */
			ON_TRACE(TRACE_LOCKS,
				 "set_lopri lock: %p node: %p: hipri_owners before: %u nr_readers: %d\n",
				 handle, node, node->lock.nr_hipri_owners, node->lock.nr_readers);
			assert("nikita-1835", node->lock.nr_hipri_owners > 0);
			node->lock.nr_hipri_owners--;
			/* If we have deadlock condition, adjust a nr_signaled
			   field. It is enough to set "signaled" flag only for
			   current process, other low-pri owners will be
			   signaled and waken up after current process unlocks
			   this object and any high-priority requestor takes
			   control. */
			if (check_deadlock_condition(node)
			    && !handle->signaled) {
				handle->signaled = 1;
				atomic_inc(&owner->nr_signaled);
			}
			UNLOCK_ZLOCK(&node->lock);
			handle = locks_list_next(handle);
		}
		owner->curpri = 0;
	}
}

#define MAX_CONVOY_SIZE ((unsigned)(NR_CPUS - 1))

/* helper function used by longterm_unlock_znode() to wake up requestor(s). */
/*
 * In certain multi threaded work loads jnode spin lock is the most
 * contented one. Wake up of threads waiting for znode is, thus,
 * important to do right. There are three well known strategies:
 *
 *  (1) direct hand-off. Hasn't been tried.
 *
 *  (2) wake all (thundering herd). This degrades performance in our
 *      case.
 *
 *  (3) wake one. Simplest solution where requestor in the front of
 *      requestors list is awaken under znode spin lock is not very
 *      good on the SMP, because first thing requestor will try to do
 *      after waking up on another CPU is to acquire znode spin lock
 *      that is still held by this thread. As an optimization we grab
 *      lock stack spin lock, release znode spin lock and wake
 *      requestor. done_context() synchronize against stack spin lock
 *      to avoid (impossible) case where requestor has been waked by
 *      some other thread (wake_up_all_lopri_owners(), or something
 *      similar) and managed to exit before we waked it up.
 *
 *      Effect of this optimization wasn't big, after all.
 *
 */
static void
wake_up_requestor(znode *node)
{
#ifdef CONFIG_SMP
	requestors_list_head *creditors;
	lock_stack           *convoy[MAX_CONVOY_SIZE];
	int                   convoyused;
	int                   convoylimit;

	assert("nikita-3180", node != NULL);
	assert("nikita-3181", spin_zlock_is_locked(&node->lock));

	ADDSTAT(node, wakeup);

	convoyused = 0;
	convoylimit = min(num_online_cpus() - 1, MAX_CONVOY_SIZE);
	creditors = &node->lock.requestors;
	if (!requestors_list_empty(creditors)) {
		convoy[0] = requestors_list_front(creditors);
		convoyused = 1;
		ADDSTAT(node, wakeup_found);
		/*
		 * it has been verified experimentally, that there are no
		 * convoys on the leaf level.
		 */
		if (znode_get_level(node) != LEAF_LEVEL &&
		    convoy[0]->request.mode == ZNODE_READ_LOCK) {
			lock_stack *item;

			ADDSTAT(node, wakeup_found_read);
			for (item = requestors_list_next(convoy[0]);
			          ! requestors_list_end(creditors, item);
			     item = requestors_list_next(item)) {
				ADDSTAT(node, wakeup_scan);
				if (item->request.mode == ZNODE_READ_LOCK) {
					ADDSTAT(node, wakeup_convoy);
					convoy[convoyused] = item;
					++ convoyused;
					/*
					 * it is safe to spin lock multiple
					 * lock stacks here, because lock
					 * stack cannot sleep on more than one
					 * requestors queue.
					 */
					/*
					 * use raw spin_lock in stead of macro
					 * wrappers, because spin lock
					 * profiling code cannot cope with so
					 * many locks held at the same time.
					 */
					spin_lock(&item->sguard.lock);
					if (convoyused == convoylimit)
						break;
				}
			}
		}
		spin_lock(&convoy[0]->sguard.lock);
	}

	UNLOCK_ZLOCK(&node->lock);
	
	while (convoyused > 0) {
		-- convoyused;
		__reiser4_wake_up(convoy[convoyused]);
		spin_unlock(&convoy[convoyused]->sguard.lock);
	}
#else
	/* uniprocessor case: keep it simple */
	if (!requestors_list_empty(&node->lock.requestors)) {
		lock_stack *requestor;

		requestor = requestors_list_front(&node->lock.requestors);
		reiser4_wake_up(requestor);
	}

	UNLOCK_ZLOCK(&node->lock);
#endif
}

#undef MAX_CONVOY_SIZE

/* unlock a znode long term lock */
void
longterm_unlock_znode(lock_handle * handle)
{
	znode *node = handle->node;
	lock_stack *oldowner = handle->owner;

	assert("jmacd-1021", handle != NULL);
	assert("jmacd-1022", handle->owner != NULL);
	ON_CONTEXT(assert("nikita-1392", lock_counters()->long_term_locked_znode > 0));

	assert("zam-130", oldowner == get_current_lock_stack());

	ON_DEBUG_CONTEXT(--lock_counters()->long_term_locked_znode);

	ADDSTAT(node, unlock);

	LOCK_ZLOCK(&node->lock);

	assert("zam-101", znode_is_locked(node));

	/* Adjust a number of high priority owners of this lock */
	if (oldowner->curpri) {
		assert("nikita-1836", node->lock.nr_hipri_owners > 0);
		node->lock.nr_hipri_owners--;
	}
	ON_TRACE(TRACE_LOCKS,
		 "%spri unlock: %p node: %p: hipri_owners: %u nr_readers %d\n",
		 oldowner->curpri ? "hi" : "lo", handle, node, node->lock.nr_hipri_owners, node->lock.nr_readers);

	/* Last write-lock release. */
	if (znode_is_wlocked_once(node)) {

		ON_DEBUG_MODIFY(znode_post_write(node));

		/* Handle znode deallocation */
		if (ZF_ISSET(node, JNODE_HEARD_BANSHEE)) {
			/* invalidate lock. FIXME-NIKITA locking.  This doesn't
			   actually deletes node, only removes it from
			   sibling list and invalidates lock. Lock
			   invalidation includes waking up all threads
			   still waiting on this node and notifying them
			   that node is dying.
			*/
			UNLOCK_ZLOCK(&node->lock);
			ON_DEBUG(check_lock_data());
			ON_DEBUG(check_lock_node_data(node));
			ON_DEBUG(node_check(node, 0));
			forget_znode(handle);
			assert("nikita-2191", znode_invariant(node));
			zput(node);
			return;
		}
	}

	if (handle->signaled)
		atomic_dec(&oldowner->nr_signaled);

	/* Unlocking means owner<->object link deletion */
	unlink_object(handle);

	/* This is enough to be sure whether an object is completely
	   unlocked. */
	if (znode_is_rlocked(node))
		node->lock.nr_readers--;
	else
		node->lock.nr_readers++;

	/* If the node is locked it must have an owners list.  Likewise, if the node is
	   unlocked it must have an empty owners list. */
	assert("zam-319", equi(znode_is_locked(node), 
			       !owners_list_empty(&node->lock.owners)));

	/* If there are pending lock requests we wake up a requestor */
	if (!znode_is_wlocked(node))
		wake_up_requestor(node);
	else
		UNLOCK_ZLOCK(&node->lock);

	assert("nikita-3182", spin_zlock_is_not_locked(&node->lock));
	/* minus one reference from handle->node */
	handle->node = NULL;
	assert("nikita-2190", znode_invariant(node));
	ON_DEBUG(check_lock_data());
	ON_DEBUG(check_lock_node_data(node));
	zput(node);
}

static int
lock_tail(lock_stack *owner, int wake_up_next, int ok, znode_lock_mode mode)
{
	znode *node = owner->request.node;

	assert("jmacd-807", spin_zlock_is_locked(&node->lock));

	/* If we broke with (ok == 0) it means we can_lock, now do it. */
	if (ok == 0) {
		lock_object(owner);
		owner->request.mode = 0;
		if (mode == ZNODE_READ_LOCK)
			wake_up_next = 1;
	}

	if (wake_up_next)
		wake_up_requestor(node);
	else
		UNLOCK_ZLOCK(&node->lock);

	if (ok == 0) {
		/* count a reference from lockhandle->node 
		  
		   znode was already referenced at the entry to this function,
		   hence taking spin-lock here is not necessary (see comment
		   in the zref()).
		*/
		zref(node);

		ON_DEBUG_CONTEXT(++lock_counters()->long_term_locked_znode);
		if (REISER4_DEBUG_NODE && mode == ZNODE_WRITE_LOCK) {
			node_check(node, 0);
			ON_DEBUG_MODIFY(znode_pre_write(node));
		}
	}

	ON_DEBUG(check_lock_data());
	ON_DEBUG(check_lock_node_data(node));
	return ok;
}

/* locks given lock object */
int 
longterm_lock_znode(
	/* local link object (may be allocated on the process owner); */
	lock_handle * handle,
	/* znode we want to lock. */
	znode * node,
	/* {ZNODE_READ_LOCK, ZNODE_WRITE_LOCK}; */
	znode_lock_mode mode,
	/* {0, -EINVAL, -EDEADLK}, see return codes description. */
	znode_lock_request request) {

	int          ret;
	int          hipri             = (request & ZNODE_LOCK_HIPRI) != 0;
	int          wake_up_next      = 0;
	txn_capture  cap_flags         = 0;
	int          non_blocking      = 0;
	int          has_atom;
	zlock       *lock;
	txn_handle  *txnh;
	tree_level   level;
	txn_capture  cap_mode;

	/* Get current process context */
	lock_stack *owner = get_current_lock_stack();

	/* Check that the lock handle is initialized and isn't already being used. */
	assert("jmacd-808", handle->owner == NULL);
	assert("nikita-3026", schedulable());
	assert("nikita-3219", request_is_deadlock_safe(node, mode, request));

	if (request & ZNODE_LOCK_NONBLOCK) {
		cap_flags |= TXN_CAPTURE_NONBLOCKING;
		non_blocking = 1;
	}

	if (request & ZNODE_LOCK_DONT_FUSE)
		cap_flags |= TXN_CAPTURE_DONT_FUSE;

	/* If we are changing our process priority we must adjust a number
	   of high priority owners for each znode that we already lock */
	if (hipri) {
		set_high_priority(owner);
	} else {
		set_low_priority(owner);
	}

	level = znode_get_level(node);
	ADDSTAT(node, lock);

	/* Fill request structure with our values. */
	owner->request.mode = mode;
	owner->request.handle = handle;
	owner->request.node = node;

	txnh = get_current_context()->trans;
	lock = &node->lock;

	has_atom = (txnh->atom != NULL);

	if (REISER4_STATS) {
		if (mode == ZNODE_READ_LOCK)
			ADDSTAT(node, lock_read);
		else
			ADDSTAT(node, lock_write);

		if (hipri)
			ADDSTAT(node, lock_hipri);
		else
			ADDSTAT(node, lock_lopri);
	}

	cap_mode = (mode == ZNODE_WRITE_LOCK) ? TXN_CAPTURE_WRITE : 0;

	/* Synchronize on node's zlock guard lock. */
	LOCK_ZLOCK(lock);

	if (znode_is_locked(node) && 
	    mode == ZNODE_WRITE_LOCK && recursive(owner))
		return lock_tail(owner, wake_up_next, 0, mode);

	for (;;) {
		ADDSTAT(node, lock_iteration);

		/* Check the lock's availability: if it is unavaiable we get EAGAIN, 0
		   indicates "can_lock", otherwise the node is invalid.  */
		ret = can_lock_object(owner);

		if (unlikely(ret == -EINVAL)) {
			/* @node is dying. Leave it alone. */
			/* wakeup next requestor to support lock invalidating */
			wake_up_next = 1;
			ADDSTAT(node, lock_dying);
			break;
		}

		if (unlikely(ret == -EAGAIN && non_blocking)) {
			/* either locking of @node by the current thread will
			 * lead to the deadlock, or lock modes are
			 * incompatible. */
			ADDSTAT(node, lock_cannot_lock);
			break;
		}

		assert("nikita-1844", (ret == 0) || ((ret == -EAGAIN) && !non_blocking));
		/* If we can get the lock... Try to capture first before
		   taking the lock.*/

		/* first handle commonest case where node and txnh are already
		 * in the same atom. */
		/* safe to do without taking locks, because:
		 *
		 * 1. read of aligned word is atomic with respect to writes to
		 * this word 
		 *
		 * 2. false negatives are handled in try_capture_args().
		 *
		 * 3. false positives are impossible.
		 *
		 * PROOF: left as an exercise to the curious reader.
		 *
		 * Just kidding. Here is one:
		 *
		 * At the time T0 txnh->atom is stored in txnh_atom.
		 *
		 * At the time T1 node->atom is stored in node_atom.
		 *
		 * At the time T2 we observe that 
		 *
		 *     txnh_atom != NULL && node_atom == txnh_atom.
		 *
		 * Imagine that at this moment we acquire node and txnh spin
		 * lock in this order. Suppose that under spin lock we have
		 *
		 *     node->atom != txnh->atom,                       (S1)
		 *
		 * at the time T3.
		 *
		 * txnh->atom != NULL still, because txnh is open by the
		 * current thread.
		 *
		 * Suppose node->atom == NULL, that is, node was un-captured
		 * between T1, and T3. But un-capturing of formatted node is
		 * always preceded by the call to invalidate_lock(), which
		 * marks znode as JNODE_IS_DYING under zlock spin
		 * lock. Contradiction, because can_lock_object() above checks
		 * for JNODE_IS_DYING. Hence, node->atom != NULL at T3.
		 *
		 * Suppose that node->atom != node_atom, that is, atom, node
		 * belongs to was fused into another atom: node_atom was fused
		 * into node->atom. Atom of txnh was equal to node_atom at T2,
		 * which means that under spin lock, txnh->atom == node->atom,
		 * because txnh->atom can only follow fusion
		 * chain. Contradicts S1.
		 *
		 * The same for hypothesis txnh->atom != txnh_atom. Hence,
		 * node->atom == node_atom == txnh_atom == txnh->atom. Again
		 * contradicts S1. Hence S1 is false. QED.
		 *
		 */

		if (likely(has_atom && ZJNODE(node)->atom == txnh->atom)) {
			ADDSTAT(node, lock_no_capture);
		} else {
			/*
			 * unlock zlock spin lock here. It is possible for
			 * longterm_unlock_znode() to sneak in here, but there
			 * is no harm: invalidate_lock() will mark znode as
			 * JNODE_IS_DYING and this will be noted by
			 * can_lock_object() below.
			 */
			UNLOCK_ZLOCK(lock);
			spin_lock_znode(node);
			ret = try_capture_args(ZJNODE(node), txnh, mode,
					       cap_flags, non_blocking, 
					       cap_mode);
			spin_unlock_znode(node);
			LOCK_ZLOCK(lock);
			if (unlikely(ret != 0)) {
				/* In the failure case, the txnmgr releases
				   the znode's lock (or in some cases, it was
				   released a while ago).  There's no need to
				   reacquire it so we should return here,
				   avoid releasing the lock. */
				owner->request.mode = 0;
				/* next requestor may not fail */
				wake_up_next = 1;
				break;
			}

			/* Check the lock's availability again -- this is
			   because under some circumstances the capture code
			   has to release and reacquire the znode spinlock. */
			ret = can_lock_object(owner);
		}

		/* This time, a return of (ret == 0) means we can lock, so we
		   should break out of the loop. */
		if (likely(ret != -EAGAIN || non_blocking)) {
			ADDSTAT(node, lock_can_lock);
			break;
		}

		/* Lock is unavailable, we have to wait. */

		/* By having semaphore initialization here we cannot lose
		   wakeup signal even if it comes after `nr_signaled' field
		   check. */
		ret = prepare_to_sleep(owner);
		if (unlikely(ret != 0)) {
			break;
		}

		assert("nikita-1837", spin_zlock_is_locked(&node->lock));
		if (hipri) {
			/* If we are going in high priority direction then
			   increase high priority requests counter for the
			   node */
			lock->nr_hipri_requests++;
			/* If there are no high priority owners for a node,
			   then immediately wake up low priority owners, so
			   they can detect possible deadlock */
			if (lock->nr_hipri_owners == 0)
				wake_up_all_lopri_owners(node);
			/* And prepare a lock request */
			requestors_list_push_front(&lock->requestors, owner);
		} else {
			/* If we are going in low priority direction then we
			   set low priority to our process. This is the only
			   case  when a process may become low priority */
			/* And finally prepare a lock request */
			requestors_list_push_back(&lock->requestors, owner);
		}

		/* Ok, here we have prepared a lock request, so unlock
		   a znode ...*/
		UNLOCK_ZLOCK(lock);
		/* ... and sleep */
		go_to_sleep(owner, level);

		LOCK_ZLOCK(lock);

		if (hipri) {
			assert("nikita-1838", lock->nr_hipri_requests > 0);
			lock->nr_hipri_requests--;
		}

		requestors_list_remove(owner);
	}

	assert("jmacd-807/a", spin_zlock_is_locked(&node->lock));
	return lock_tail(owner, wake_up_next, ret, mode);
}

/* lock object invalidation means changing of lock object state to `INVALID'
   and waiting for all other processes to cancel theirs lock requests. */
void
invalidate_lock(lock_handle * handle	/* path to lock
					   * owner and lock
					   * object is being
					   * invalidated. */ )
{
	znode *node = handle->node;
	lock_stack *owner = handle->owner;
	lock_stack *rq;

	assert("zam-325", owner == get_current_lock_stack());

	LOCK_ZLOCK(&node->lock);

	assert("zam-103", znode_is_write_locked(node));
	assert("nikita-1393", !ZF_ISSET(node, JNODE_LEFT_CONNECTED));
	assert("nikita-1793", !ZF_ISSET(node, JNODE_RIGHT_CONNECTED));
	assert("nikita-1394", ZF_ISSET(node, JNODE_HEARD_BANSHEE));
	assert("nikita-3097", znode_is_wlocked_once(node));

	if (handle->signaled)
		atomic_dec(&owner->nr_signaled);

	ZF_SET(node, JNODE_IS_DYING);
	unlink_object(handle);
	node->lock.nr_readers = 0;

	/* all requestors will be informed that lock is invalidated. */
	for_all_tslist(requestors, &node->lock.requestors, rq) {
		reiser4_wake_up(rq);
	}

	/* We use that each unlock() will wakeup first item from requestors
	   list; our lock stack is the last one. */
	while (!requestors_list_empty(&node->lock.requestors)) {
		requestors_list_push_back(&node->lock.requestors, owner);

		prepare_to_sleep(owner);

		UNLOCK_ZLOCK(&node->lock);
		go_to_sleep(owner, znode_get_level(node));
		LOCK_ZLOCK(&node->lock);

		requestors_list_remove(owner);
	}

	UNLOCK_ZLOCK(&node->lock);
}

/* Initializes lock_stack. */
void
init_lock_stack(lock_stack * owner	/* pointer to
					   * allocated
					   * structure. */ )
{
	/* xmemset(,0,) is done already as a part of reiser4 context
	 * initialization */
	/* xmemset(owner, 0, sizeof (lock_stack)); */
	locks_list_init(&owner->locks);
	requestors_list_clean(owner);
	spin_stack_init(owner);
	owner->curpri = 1;
	sema_init(&owner->sema, 0);
}

/* Initializes lock object. */
void
reiser4_init_lock(zlock * lock	/* pointer on allocated
				   * uninitialized lock object
				   * structure. */ )
{
	xmemset(lock, 0, sizeof (zlock));
	spin_zlock_init(lock);
	requestors_list_init(&lock->requestors);
	owners_list_init(&lock->owners);
}

/* lock handle initialization */
void
init_lh(lock_handle * handle)
{
	xmemset(handle, 0, sizeof *handle);
	locks_list_clean(handle);
	owners_list_clean(handle);
}

/* freeing of lock handle resources */
void
done_lh(lock_handle * handle)
{
	assert("zam-342", handle != NULL);
	if (handle->owner != NULL)
		longterm_unlock_znode(handle);
}

/* What kind of lock? */
znode_lock_mode lock_mode(lock_handle * handle)
{
	if (handle->owner == NULL) {
		return ZNODE_NO_LOCK;
	} else if (znode_is_rlocked(handle->node)) {
		return ZNODE_READ_LOCK;
	} else {
		return ZNODE_WRITE_LOCK;
	}
}

/* Transfer a lock handle (presumably so that variables can be moved between stack and
   heap locations). */
static void
move_lh_internal(lock_handle * new, lock_handle * old, int unlink_old)
{
	znode *node = old->node;
	lock_stack *owner = old->owner;
	int signaled;

	/* locks_list, modified by link_object() is not protected by
	   anything. This is valid because only current thread ever modifies
	   locks_list of its lock_stack.
	*/
	assert("nikita-1827", owner == get_current_lock_stack());
	assert("nikita-1831", new->owner == NULL);

	LOCK_ZLOCK(&node->lock);

	new->node = node;

	signaled = old->signaled;
	if (unlink_old) {
		unlink_object(old);
	} else {
		if (node->lock.nr_readers > 0) {
			node->lock.nr_readers += 1;
		} else {
			node->lock.nr_readers -= 1;
		}
		if (signaled) {
			atomic_inc(&owner->nr_signaled);
		}
		if (owner->curpri) {
			node->lock.nr_hipri_owners += 1;
		}
		ON_DEBUG_CONTEXT(++lock_counters()->long_term_locked_znode);

		zref(node);
	}
	link_object(new, owner, node);
	new->signaled = signaled;

	UNLOCK_ZLOCK(&node->lock);
}

void
move_lh(lock_handle * new, lock_handle * old)
{
	move_lh_internal(new, old, /*unlink_old */ 1);
}

void
copy_lh(lock_handle * new, lock_handle * old)
{
	move_lh_internal(new, old, /*unlink_old */ 0);
}

/* after getting -EDEADLK we unlock znodes until this function returns false */
int
check_deadlock(void)
{
	lock_stack *owner = get_current_lock_stack();
	return atomic_read(&owner->nr_signaled) != 0;
}

/* Reset the semaphore (under protection of lock_stack spinlock) to avoid lost
   wake-up. */
int
prepare_to_sleep(lock_stack * owner)
{
	assert("nikita-1847", owner == get_current_lock_stack());
/* NIKITA-FIXME-HANS: ZAM-FIXME-HANS: resolve this experiment */
	if (0) {
		/* NOTE-NIKITA: I commented call to sema_init() out hoping
		   that it is the reason or thread sleeping in
		   down(&owner->sema) without any other thread running.
		  
		   Anyway, it is just an optimization: is semaphore is not
		   reinitialised at this point, in the worst case
		   longterm_lock_znode() would have to iterate its loop once
		   more.
		*/
		spin_lock_stack(owner);
		sema_init(&owner->sema, 0);
		spin_unlock_stack(owner);
	}
/* ZAM-FIXME-HANS: comment this */
	if (unlikely(atomic_read(&owner->nr_signaled) != 0 && !owner->curpri)) {
		return RETERR(-EDEADLK);
	}
	return 0;
}

/* Wakes up a single thread */
void
__reiser4_wake_up(lock_stack * owner)
{
	up(&owner->sema);
}

/* Puts a thread to sleep */
void
__go_to_sleep(lock_stack * owner
#if REISER4_STATS
	    , int node_level
#endif
)
{
#ifdef CONFIG_REISER4_STATS
	unsigned long sleep_start = jiffies;
#endif
	/* Well, we might sleep here, so holding of any spinlocks is no-no */
	assert("nikita-3027", schedulable());
	/* return down_interruptible(&owner->sema); */
	down(&owner->sema);
#ifdef CONFIG_REISER4_STATS
	switch (node_level) {
	    case ADD_TO_SLEPT_IN_WAIT_EVENT:
		    reiser4_stat_add(txnmgr.slept_in_wait_event, jiffies - sleep_start);
		    break;
	    case ADD_TO_SLEPT_IN_WAIT_ATOM:
		    reiser4_stat_add(txnmgr.slept_in_wait_atom, jiffies - sleep_start);
		    break;
	    default:
		    reiser4_stat_add_at_level(node_level, time_slept, 
					      jiffies - sleep_start);
	}
#endif
}

int
lock_stack_isclean(lock_stack * owner)
{
	if (locks_list_empty(&owner->locks)) {
		assert("zam-353", atomic_read(&owner->nr_signaled) == 0);
		return 1;
	}

	return 0;
}

#if REISER4_DEBUG_OUTPUT
/* Debugging help */
void
print_lock_stack(const char *prefix, lock_stack * owner)
{
	lock_handle *handle;

	spin_lock_stack(owner);

	printk("%s:\n", prefix);
	printk(".... nr_signaled %d\n", atomic_read(&owner->nr_signaled));
	printk(".... curpri %s\n", owner->curpri ? "high" : "low");

	if (owner->request.mode != 0) {
		printk(".... current request: %s", owner->request.mode == ZNODE_WRITE_LOCK ? "write" : "read");
		print_address("", znode_get_block(owner->request.node));
	}

	printk(".... current locks:\n");

	for_all_tslist(locks, &owner->locks, handle) {
		if (handle->node != NULL)
			print_address(znode_is_rlocked(handle->node) ?
				      "......  read" : "...... write", znode_get_block(handle->node));
	}

	spin_unlock_stack(owner);
}
#endif

#if REISER4_DEBUG

void
check_lock_stack(lock_stack * stack)
{
	spin_lock_stack(stack);
	locks_list_check(&stack->locks);
	spin_unlock_stack(stack);
}

extern spinlock_t active_contexts_lock;
extern context_list_head active_contexts;

void
check_lock_data()
{
	if (0) {
		reiser4_context *context;

		spin_lock(&active_contexts_lock);
		for_all_tslist(context, &active_contexts, context) {
			check_lock_stack(&context->stack);
		}
		spin_unlock(&active_contexts_lock);
	} else
		check_lock_stack(&get_current_context()->stack);
}

void
check_lock_node_data(znode * node)
{
	LOCK_ZLOCK(&node->lock);
	owners_list_check(&node->lock.owners);
	requestors_list_check(&node->lock.requestors);
	UNLOCK_ZLOCK(&node->lock);
}

static int
request_is_deadlock_safe(znode * node, znode_lock_mode mode,
			 znode_lock_request request)
{
	lock_stack *owner;

	owner = get_current_lock_stack();
	if (request & ZNODE_LOCK_HIPRI && !(request & ZNODE_LOCK_NONBLOCK) &&
	    znode_get_level(node) != 0) {
		lock_handle *item;

		for_all_tslist(locks, &owner->locks, item) {
			znode *other = item->node;

			if (znode_get_level(other) == 0)
				continue;
			if (znode_get_level(other) > znode_get_level(node))
				return 0;
		}
	}
	return 1;
}

#endif

/* return pointer to static storage with name of lock_mode. For
    debugging */
const char *
lock_mode_name(znode_lock_mode lock /* lock mode to get name of */ )
{
	if (lock == ZNODE_READ_LOCK)
		return "read";
	else if (lock == ZNODE_WRITE_LOCK)
		return "write";
	else {
		static char buf[30];

		sprintf(buf, "unknown: %i", lock);
		return buf;
	}
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
