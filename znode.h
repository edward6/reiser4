/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of znode (Zam's node). */

#ifndef __ZNODE_H__
#define __ZNODE_H__

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "spin_macros.h"
#include "key.h"
#include "coord.h"
#include "tslist.h"
#include "plugin/node/node.h"
#include "jnode.h"
#include "readahead.h"

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pagemap.h>	/* for PAGE_CACHE_SIZE */
#include <asm/atomic.h>
#include <asm/semaphore.h>

/* per-znode lock requests queue; list items are lock owner objects
   which want to lock given znode */
TS_LIST_DECLARE(requestors);
/* per-znode list of lock handles for this znode
   
   Locking: protected by znode spin lock. */
TS_LIST_DECLARE(owners);
/* per-owner list of lock handles that point to locked znodes which
   belong to one lock owner 

   Locking: this list is only accessed by the thread owning the lock stack this
   list is attached to. Hence, no locking is necessary.
*/
TS_LIST_DECLARE(locks);

/* Per-znode lock object */
struct zlock {
	/* The number of readers if positive; the number of recursively taken
	   write locks if negative */
	/*  0 */ int nr_readers;
	/* A number of processes (lock_stacks) that have this object
	   locked with high priority */
	/*  4 */ unsigned nr_hipri_owners;
	/* A number of attempts to lock znode in high priority direction */
	/*  8 */ unsigned nr_hipri_requests;
	/* A linked list of lock_handle objects that contains pointers
	   for all lock_stacks which have this lock object locked */
	/* 12 */ owners_list_head owners;
	/* A linked list of lock_stacks that wait for this lock */
	/* 20 */ requestors_list_head requestors;
	/* 28 */
};

/* &znode - node in a reiser4 tree.
  
   NOTE-NIKITA fields in this struct have to be rearranged (later) to reduce
   cacheline pressure.
  
   Locking: 
  
   Long term: data in a disk node attached to this znode are protected
   by long term, deadlock aware lock ->lock;
  
   Spin lock: the following fields are protected by the spin lock:
  
    ->lock

   Following fields are protected by the global tree lock:
  
    ->left
    ->right
    ->in_parent

   Following fields are protected by the global delimiting key lock (dk_lock):
  
    ->ld_key (to update ->ld_key long-term lock on the node is also required)
    ->rd_key
  
   Atomic counters
  
    ->c_count
  
   Following fields are protected by the long term lock:

    ->nr_items

   ->node_plugin is never changed once set. This means that after code made
   itself sure that field is valid it can be accessed without any additional
   locking.

   ->level is immutable.

   Invariants involving this data-type:

      [znode-fake]
      [znode-level]
      [znode-c_count]
      [znode-refs]
      [jnode-refs]
      [jnode-queued]

    For this to be made into a clustering or NUMA filesystem, we would want to eliminate all of the global locks.
    Suggestions for how to do that are desired.*/
struct znode {
	/* Embedded jnode. */
	jnode zjnode;

	/* contains three subfields, node, pos_in_node, and pos_in_unit.

	   pos_in_node and pos_in_unit are only hints that are cached to
	   speed up lookups during balancing. They are not required to be up to
	   date. Synched in find_child_ptr().
	  
	   This value allows us to avoid expensive binary searches.

	   in_parent->node points to the parent of this node, and is NOT a
	   hint.

	   why? NIKITA-FIXME-HANS Why what? HANS-FIXME-NIKITA
	*/
	coord_t in_parent;

	znode *left;
	znode *right;
	/* long term lock on node content. This lock supports deadlock
	   detection. See lock.c
	*/
	zlock lock;

	/* You cannot remove from memory a node that has children in
	   memory. This is because we rely on the fact that parent of given
	   node can always be reached without blocking for io. When reading a
	   node into memory you must increase the c_count of its parent, when
	   removing it from memory you must decrease the c_count.  This makes
	   the code simpler, and the cases where it is suboptimal are truly
	   obscure.
	  
	   All three znode reference counters ([cdx]_count) are atomic_t
	   because we don't want to take and release spinlock for each
	   reference addition/drop.
	*/
	atomic_t c_count;

	/* plugin of node attached to this znode. NULL if znode is not
	   loaded. */
	node_plugin *nplug;

	/* version of znode data. This is increased on each modification. */
	__u64 version;

	/* left delimiting key. Necessary to efficiently perform
	   balancing with node-level locking. Kept in memory only. */
	reiser4_key ld_key;
	/* right delimiting key. */
	reiser4_key rd_key;

	/* znode's tree level */
	__u16 level;
	__u16 nr_items;
#if REISER4_DEBUG_MODIFY
	/* In debugging mode, used to detect loss of znode_set_dirty()
	   notification. */
	__u32 cksum;
	spinlock_t cksum_guard;
#endif
};

/* In general I think these macros should not be exposed. */
#define znode_is_locked(node)          ((node)->lock.nr_readers != 0)
#define znode_is_rlocked(node)         ((node)->lock.nr_readers > 0)
#define znode_is_wlocked(node)         ((node)->lock.nr_readers < 0)
#define znode_is_wlocked_once(node)    ((node)->lock.nr_readers == -1)
#define znode_can_be_rlocked(node)     ((node)->lock.nr_readers >=0)
#define is_lock_compatible(node, mode) \
             (((mode) == ZNODE_WRITE_LOCK && !znode_is_locked(node)) \
           || ((mode) == ZNODE_READ_LOCK && znode_can_be_rlocked(node)))

/* Macros for accessing the znode state. */
#define	ZF_CLR(p,f)	        JF_CLR  (ZJNODE(p), (f))
#define	ZF_ISSET(p,f)	        JF_ISSET(ZJNODE(p), (f))
#define	ZF_SET(p,f)		JF_SET  (ZJNODE(p), (f))

/* Since we have R/W znode locks we need additional bidirectional `link'
   objects to implement n<->m relationship between lock owners and lock
   objects. We call them `lock handles'.

   Locking: see lock.c/"SHORT-TERM LOCKING"
*/
struct lock_handle {
	/* This flag indicates that a signal to yield a lock was passed to
	   lock owner and counted in owner->nr_signalled 
	  
	   Locking: this is accessed under spin lock on ->node.
	*/
	int signaled;
	/* A link to owner of a lock */
	lock_stack *owner;
	/* A link to znode locked */
	znode *node;
	/* A list of all locks for a process */
	locks_list_link locks_link;
	/* A list of all owners for a znode */
	owners_list_link owners_link;
};

/* A lock stack structure for accumulating locks owned by a process */
struct lock_stack {
	/* A guard lock protecting a lock stack */
	reiser4_spin_data sguard;
	/* number of znodes which were requested by high priority processes */
	atomic_t nr_signaled;
	/* Current priority of a process 
	  
	   This is only accessed by the current thread and thus requires no
	   locking.
	*/
	int curpri;
	/* A list of all locks owned by this process */
	locks_list_head locks;
	/* When lock_stack waits for the lock, it puts itself on double-linked
	   requestors list of that lock */
	requestors_list_link requestors_link;
	/* Current lock request info.
	  
	   This is only accessed by the current thread and thus requires no
	   locking.
	*/
	struct {
		/* A pointer to uninitialized link object */
		lock_handle *handle;
		/* A pointer to the object we want to lock */
		znode *node;
		/* Lock mode (ZNODE_READ_LOCK or ZNODE_WRITE_LOCK) */
		znode_lock_mode mode;
	} request;
	/* It is a lock_stack's synchronization object for when process sleeps
	   when requested lock not on this lock_stack but which it wishes to
	   add to this lock_stack is not immediately available. It is used
	   instead of wait_queue_t object due to locking problems (lost wake
	   up). "lost wakeup" occurs when process is waken up before he actually
	   becomes 'sleepy' (through sleep_on()). Using of semaphore object is
	   simplest way to avoid that problem.
	  
	   A semaphore is used in the following way: only the process that is
	   the owner of the lock_stack initializes it (to zero) and calls
	   down(sema) on it. Usually this causes the process to sleep on the
	   semaphore. Other processes may wake him up by calling up(sema). The
	   advantage to a semaphore is that up() and down() calls are not
	   required to preserve order. Unlike wait_queue it works when process
	   is woken up before getting to sleep. 
	  
	   NOTE-NIKITA: Transaction manager is going to have condition variables
	   (&kcondvar_t) anyway, so this probably will be replaced with
	   one in the future.
	  
	   After further discussion, Nikita has shown me that Zam's implementation is
	   exactly a condition variable.  The znode's {zguard,requestors_list} represents
	   condition variable and the lock_stack's {sguard,semaphore} guards entry and
	   exit from the condition variable's wait queue.  But the existing code can't
	   just be replaced with a more general abstraction, and I think its fine the way
	   it is. */
	struct semaphore sema;
};

/* defining of list manipulation functions for lists above */
TS_LIST_DEFINE(requestors, lock_stack, requestors_link);
TS_LIST_DEFINE(owners, lock_handle, owners_link);
TS_LIST_DEFINE(locks, lock_handle, locks_link);

/*\
   User-visible znode locking functions */

extern int longterm_lock_znode(lock_handle * handle, znode * node, znode_lock_mode mode, znode_lock_request request);
extern void longterm_unlock_znode(lock_handle * handle);

extern int check_deadlock(void);

extern lock_stack *get_current_lock_stack(void);

extern void init_lock_stack(lock_stack * owner);
extern void reiser4_init_lock(zlock * lock);

extern void init_lh(lock_handle *);
extern void move_lh(lock_handle * new, lock_handle * old);
extern void copy_lh(lock_handle * new, lock_handle * old);
extern void done_lh(lock_handle *);
extern znode_lock_mode lock_mode(lock_handle *);

extern int prepare_to_sleep(lock_stack * owner);

#if REISER4_STATS

#define ADD_TO_SLEPT_IN_WAIT_EVENT (-1)
#define ADD_TO_SLEPT_IN_WAIT_ATOM  (-2)

int __go_to_sleep(lock_stack*, int);
#define go_to_sleep(owner, level) __go_to_sleep(owner, level);

#else

int __go_to_sleep(lock_stack*);
#define go_to_sleep(owner, level) __go_to_sleep(owner)

#endif

extern void __reiser4_wake_up(lock_stack * owner);

extern int lock_stack_isclean(lock_stack * owner);

/* zlock object state check macros: only used in assertions.  Both forms imply that the
   lock is held by the current thread. */
extern int znode_is_write_locked(const znode * node);

/* lock ordering is: first take znode spin lock, then lock stack spin lock */
#define spin_ordering_pred_stack(stack) (1)
/* Same for lock_stack */
SPIN_LOCK_FUNCTIONS(stack, lock_stack, sguard);

static inline void
reiser4_wake_up(lock_stack * owner)
{
	spin_lock_stack(owner);
	__reiser4_wake_up(owner);
	spin_unlock_stack(owner);
}

extern void del_c_ref(znode *node);
extern znode *zget(reiser4_tree * tree, const reiser4_block_nr * const block,
		   znode * parent, tree_level level, int gfp_flag);
extern znode *zlook(reiser4_tree * tree, const reiser4_block_nr * const block);
extern int zload(znode * node);
extern int zload_ra(znode * node, ra_info_t *info);
extern int zinit_new(znode * node);
extern void zrelse(znode * node);
extern void znode_change_parent(znode * new_parent, reiser4_block_nr * block);

/* size of data in znode */
static inline unsigned
znode_size(const znode * node UNUSED_ARG /* znode to query */ )
{
	assert("nikita-1416", node != NULL);
	return PAGE_CACHE_SIZE;
}

extern unsigned znode_free_space(znode * node);
extern int znode_is_loaded(const znode * node);
extern int znode_is_loaded_nolock(const znode * node);

extern reiser4_key *znode_get_rd_key(znode * node);
extern reiser4_key *znode_get_ld_key(znode * node);

extern reiser4_key *znode_set_rd_key(znode * node, const reiser4_key * key);
extern reiser4_key *znode_set_ld_key(znode * node, const reiser4_key * key);

/* `connected' state checks */
static inline int
znode_is_right_connected(const znode * node)
{
	return ZF_ISSET(node, JNODE_RIGHT_CONNECTED);
}

static inline int
znode_is_left_connected(const znode * node)
{
	return ZF_ISSET(node, JNODE_LEFT_CONNECTED);
}

static inline int
znode_is_connected(const znode * node)
{
	return znode_is_right_connected(node) && znode_is_left_connected(node);
}

extern int znode_rehash(znode * node, const reiser4_block_nr * new_block_nr);
extern znode *znode_parent(const znode * node);
extern znode *znode_parent_nolock(const znode * node);
extern int znode_above_root(const znode * node);
extern int znode_is_true_root(const znode * node);
extern void zdrop(znode * node);
extern int znodes_init(void);
extern int znodes_done(void);
extern int znodes_tree_init(reiser4_tree * ztree);
extern void znodes_tree_done(reiser4_tree * ztree);
extern int znode_contains_key(znode * node, const reiser4_key * key);
extern int znode_contains_key_lock(znode * node, const reiser4_key * key);
extern int znode_invariant(const znode * node);
extern unsigned znode_save_free_space(znode * node);
extern unsigned znode_recover_free_space(znode * node);

extern int znode_just_created(const znode * node);

extern void zfree(znode * node);

#if REISER4_DEBUG_MODIFY
extern __u32 znode_checksum(const znode * node);
extern int znode_pre_write(znode * node);
extern int znode_post_write(znode * node);
#endif

const char *lock_mode_name(znode_lock_mode lock);

#if REISER4_DEBUG_OUTPUT
extern void print_znode(const char *prefix, const znode * node);
extern void info_znode(const char *prefix, const znode * node);
extern void print_znodes(const char *prefix, reiser4_tree * tree);
extern void print_lock_stack(const char *prefix, lock_stack * owner);
#else
#define print_znode( p, n ) noop
#define info_znode( p, n ) noop
#define print_znodes( p, t ) noop
#define print_lock_stack( p, o ) noop
#endif

/* Make it look like various znode functions exist instead of treating znodes as
   jnodes in znode-specific code. */
#define znode_page(x)               jnode_page ( ZJNODE(x) )
#define zdata(x)                    jdata ( ZJNODE(x) )
#define znode_get_block(x)          jnode_get_block ( ZJNODE(x) )
#define znode_created(x)            jnode_created ( ZJNODE(x) )
#define znode_set_created(x)        jnode_set_created ( ZJNODE(x) )

#define znode_is_dirty(x)           jnode_is_dirty    ( ZJNODE(x) )
#define znode_check_dirty(x)        jnode_check_dirty ( ZJNODE(x) )
#define znode_set_dirty(x)          jnode_set_dirty   ( ZJNODE(x) )
#define znode_set_clean(x)          jnode_set_clean   ( ZJNODE(x) )
#define znode_set_block(x, b)       jnode_set_block ( ZJNODE(x), (b) )

#define spin_lock_znode(x)          LOCK_JNODE ( ZJNODE(x) )
#define spin_unlock_znode(x)        UNLOCK_JNODE ( ZJNODE(x) )
#define spin_trylock_znode(x)       spin_trylock_jnode ( ZJNODE(x) )
#define spin_znode_is_locked(x)     spin_jnode_is_locked ( ZJNODE(x) )
#define spin_znode_is_not_locked(x) spin_jnode_is_not_locked ( ZJNODE(x) )

#if REISER4_DEBUG
extern int znode_x_count_is_protected(const znode * node);
#endif

static inline znode *
zref(znode * node)
{
	/* change of x_count from 0 to 1 is protected by tree spin-lock */
	assert("nikita-2517", znode_x_count_is_protected(node));
	return JZNODE(jref(ZJNODE(node)));
}

static inline void
zput(znode * node)
{
	jput(ZJNODE(node));
}

/* get the level field for a znode */
static inline tree_level
znode_get_level(const znode * node)
{
	return node->level;
}

/* get the level field for a jnode */
static inline tree_level
jnode_get_level(const jnode * node)
{
	if (jnode_is_znode(node))
		return znode_get_level(JZNODE(node));
	else
		/* unformatted nodes are all at the LEAF_LEVEL and for
		   "semi-formatted" nodes like bitmaps, level doesn't matter. */
		return LEAF_LEVEL;
}

static inline reiser4_tree *
znode_get_tree(const znode * node)
{
	assert("nikita-2692", node != NULL);
	return jnode_get_tree(ZJNODE(node));
}

/* Data-handles.  A data handle object manages pairing calls to zload() and zrelse().  We
   must load the data for a node in many places.  We could do this by simply calling
   zload() everywhere, the difficulty arises when we must release the loaded data by
   calling zrelse.  In a function with many possible error/return paths, it requires extra
   work to figure out which exit paths must call zrelse and those which do not.  The data
   handle automatically calls zrelse for every zload that it is responsible for.  In that
   sense, it acts much like a lock_handle.
*/
typedef struct load_count {
	znode *node;
	int d_ref;
} load_count;

extern void init_load_count(load_count * lc);	/* Initialize a load_count set the current node to NULL. */
extern void done_load_count(load_count * dh);	/* Finalize a load_count: call zrelse() if necessary */
extern int incr_load_count(load_count * dh);	/* Call zload() on the current node. */
extern int incr_load_count_znode(load_count * dh, znode * node);	/* Set the argument znode to the current node, call zload(). */
extern int incr_load_count_jnode(load_count * dh, jnode * node);	/* If the argument jnode is formatted, do the same as
									   * incr_load_count_znode, otherwise do nothing (unformatted nodes
									   * don't require zload/zrelse treatment). */
extern void move_load_count(load_count * new, load_count * old);	/* Move the contents of a load_count.  Old handle is released. */
extern void copy_load_count(load_count * new, load_count * old);	/* Copy the contents of a load_count.  Old handle remains held. */

/* Variable initializers for load_count. */
#define INIT_LOAD_COUNT ( load_count * ){ .node = NULL, .d_ref = 0 }
#define INIT_LOAD_COUNT_NODE( n ) ( load_count ){ .node = ( n ), .d_ref = 0 }

/* A convenience macro for use in assertions or debug-only code, where loaded data is only
   required to perform the debugging check.  This macro encapsulates an expression inside
   a pair of calls to zload()/zrelse(). */
#define WITH_DATA( node, exp )				\
({							\
	int __with_dh_result;				\
	znode *__with_dh_node;				\
							\
	__with_dh_node = ( node );			\
	__with_dh_result = zload( __with_dh_node );	\
	if( __with_dh_result == 0 ) {			\
		__with_dh_result = ( int )( exp );	\
		zrelse( __with_dh_node );		\
	}						\
	__with_dh_result;				\
})

/* Same as above, but accepts a return value in case zload fails. */
#define WITH_DATA_RET( node, ret, exp )			\
({							\
	int __with_dh_result;				\
	znode *__with_dh_node;				\
							\
	__with_dh_node = ( node );			\
	__with_dh_result = zload( __with_dh_node );	\
	if( __with_dh_result == 0 ) {			\
		__with_dh_result = ( int )( exp );	\
		zrelse( __with_dh_node );		\
	} else						\
		__with_dh_result = ( ret );		\
	__with_dh_result;				\
})

#if REISER4_DEBUG
#define STORE_COUNTERS						\
	lock_counters_info __entry_counters = *lock_counters()
#define CHECK_COUNTERS						\
ON_DEBUG_CONTEXT(						\
({								\
	__entry_counters.x_refs = lock_counters() -> x_refs;	\
	__entry_counters.t_refs = lock_counters() -> t_refs;	\
	__entry_counters.d_refs = lock_counters() -> d_refs;	\
	assert("nikita-2159",					\
	       !memcmp(&__entry_counters, lock_counters(),	\
		       sizeof __entry_counters));		\
}) )

#else
#define STORE_COUNTERS
#define CHECK_COUNTERS noop
#endif

#if REISER4_DEBUG
extern void check_lock_data(void);
extern void check_lock_node_data(znode * node);
#else
#define check_lock_data() noop
#define check_lock_node_data() noop
#endif

/* __ZNODE_H__ */
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
