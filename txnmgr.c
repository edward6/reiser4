/* Copyright (C) 2001, 2002 Hans Reiser.  All rights reserved.
 */

/* The txnmgr is a set of interfaces that keep track of atoms and transcrash handles.  The txnmgr processes
 * capture_block requests and manages the relationship between jnodes and atoms through the various stages of a
 * transcrash, and it also oversees the fusion and capture-on-copy processes.  The main difficulty with this task is
 * maintaining a deadlock-free lock ordering between atoms and jnodes/handles.  The reason for the difficulty is that
 * jnodes, handles, and atoms contain pointer circles, and the cycle must be broken.  The main requirement is that
 * atom-fusion be deadlock free, so once you hold the atom_lock you may then wait to acquire any jnode or handle lock.
 * This implies that any time you check the atom-pointer of a jnode or handle and then try to lock that atom, you must
 * use trylock() and possibly reverse the order.
 *
 * This code implements the design documented at:
 *
 *   http://namesys.com/txn-doc.html
 */

#include "reiser4.h"

static void   atom_free                       (txn_atom   *atom);

static int    atom_try_commit_locked          (txn_atom   *atom);

static int    commit_txnh                     (txn_handle *txnh);

static void   wakeup_atom_waitfor_list        (txn_atom   *atom);
static void   wakeup_atom_waiting_list        (txn_atom   *atom);

static void   capture_assign_txnh_nolock      (txn_atom   *atom,
					       txn_handle *txnh);

static void   capture_assign_block_nolock     (txn_atom   *atom,
					       jnode      *node);

static int    capture_assign_block            (txn_handle *txnh,
					       jnode      *node);

static int    capture_assign_txnh             (jnode       *node,
					       txn_handle  *txnh,
					       txn_capture  mode);

static int    capture_init_fusion             (jnode       *node,
					       txn_handle  *txnh,
					       txn_capture  mode);

static int    capture_fuse_wait               (jnode       *node,
					       txn_handle  *txnh,
					       txn_atom    *atomf,
					       txn_atom    *atomh,
					       txn_capture  mode);

static void   capture_fuse_into               (txn_atom   *small,
					       txn_atom   *large);

static int    capture_copy                    (jnode      *node,
					       txn_handle *txnh,
					       txn_atom   *atomf,
					       txn_atom   *atomh,
					       txn_capture  mode);

static void   uncapture_block                 (txn_atom   *atom,
					       jnode      *node);

static void   invalidate_clean_list           (txn_atom * atom);


/* Local debugging */
void          print_atom                      (const char *prefix,
					       txn_atom   *atom);

/* Audited by: umka (2002.06.13) */
static inline unsigned jnode_real_level (jnode *node)
{
	assert ("umka-167", node != NULL);
//	assert ("jmacd-1232", jnode_get_level (node) >= 0);
	return jnode_get_level (node);
}

/****************************************************************************************
				    GENERIC STRUCTURES
****************************************************************************************/

typedef struct _txn_wait_links txn_wait_links;

struct _txn_wait_links
{
	lock_stack *_lock_stack;
	fwaitfor_list_link  _fwaitfor_link;
	fwaiting_list_link  _fwaiting_link;
};

TS_LIST_DEFINE(atom,txn_atom,atom_link);
TS_LIST_DEFINE(txnh,txn_handle,txnh_link);

TS_LIST_DEFINE(fwaitfor,txn_wait_links,_fwaitfor_link);
TS_LIST_DEFINE(fwaiting,txn_wait_links,_fwaiting_link);

/* FIXME: In theory, we should be using the slab cache init & destructor
 * methods instead of, e.g., jnode_init, etc. */
static kmem_cache_t *_atom_slab = NULL;
static kmem_cache_t *_txnh_slab = NULL; /* FIXME_LATER_JMACD Will it be used? */

/*****************************************************************************************
				       TXN_INIT
*****************************************************************************************/

/* Initialize static variables in this file. */
/* Audited by: umka (2002.06.13) */
extern atomic_t flush_cnt;
int
txn_init_static (void)
{
	assert ("jmacd-600", _atom_slab == NULL);
	assert ("jmacd-601", _txnh_slab == NULL);

	atomic_set (& flush_cnt, 0);

	_atom_slab = kmem_cache_create ("txn_atom", sizeof (txn_atom),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (_atom_slab == NULL) {
		goto error;
	} 

	_txnh_slab = kmem_cache_create ("txn_handle", sizeof (txn_handle),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if (_txnh_slab == NULL) {
		goto error;
	}
	
	return 0;

 error:

	if (_atom_slab != NULL) { kmem_cache_destroy (_atom_slab); }
	if (_txnh_slab != NULL) { kmem_cache_destroy (_txnh_slab); }
	return -ENOMEM;	
}

/* Un-initialize static variables in this file. */
/* Audited by: umka (2002.06.13) */
int
txn_done_static (void)
{
	int ret1, ret2, ret3;

	ret1 = ret2 = ret3 = 0;

	if (_atom_slab != NULL) {
		ret1 = kmem_cache_destroy (_atom_slab);
		_atom_slab = NULL;
	}

	if (_txnh_slab != NULL) {
		ret2 = kmem_cache_destroy (_txnh_slab);
		_txnh_slab = NULL;
	}

	return ret1 ? : ret2;
}

/* Initialize a new transaction manager.  Called when the super_block is initialized. */
/* Audited by: umka (2002.06.13) */
void
txn_mgr_init (txn_mgr *mgr)
{
	assert("umka-169", mgr != NULL);
	
	mgr->atom_count = 0;
	mgr->id_count = 1;

	atom_list_init (& mgr->atoms_list);
	spin_lock_init (& mgr->tmgr_lock);

	sema_init (&mgr->commit_semaphore, 1);
}

/* Free a new transaction manager. */
/* Audited by: umka (2002.06.13) */
int
txn_mgr_done (txn_mgr* mgr UNUSED_ARG)
{
	assert("umka-170", mgr != NULL);
	
	return 0;
}

/* Initialize a transaction handle. */
/* Audited by: umka (2002.06.13) */
static void
txnh_init (txn_handle *txnh,
	   txn_mode    mode)
{
	assert("umka-171", txnh != NULL);
	
	txnh->mode = mode;
	txnh->atom = NULL;

	spin_lock_init (& txnh->hlock);

	txnh_list_clean (txnh);
}

/* Check if a transaction handle is clean. */
/* Audited by: umka (2002.06.13) */
static int
txnh_isclean (txn_handle *txnh)
{
	assert("umka-172", txnh != NULL);
	return ((txnh->atom == NULL) && spin_txnh_is_not_locked (txnh));
}

/* Initialize an atom. */
/* Audited by: umka (2002.06.13) */
static void
atom_init (txn_atom     *atom)
{
	int level;

	assert("umka-173", atom != NULL);
	
	xmemset (atom, 0, sizeof (txn_atom));
	
	atom->stage         = ASTAGE_FREE;
	atom->start_time    = jiffies;

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		capture_list_init (& atom->dirty_nodes[level]);
	}

	capture_list_init  (& atom->clean_nodes);
	spin_lock_init     (& atom->alock);
	txnh_list_init     (& atom->txnh_list);
	atom_list_clean    (atom);
	fwaitfor_list_init (& atom->fwaitfor_list);
	fwaiting_list_init (& atom->fwaiting_list);
	blocknr_set_init   (& atom->delete_set);
	blocknr_set_init   (& atom->wandered_map);

	flush_init_atom (atom);
}

#if REISER4_DEBUG
/* Check if an atom is clean. */
/* Audited by: umka (2002.06.13) */
static int
atom_isclean (txn_atom *atom)
{
	int level;

	assert("umka-174", atom != NULL);
	
	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		if (! capture_list_empty (& atom->dirty_nodes[level])) {
			return 0;
		}
	}

	return ((atom->stage               == ASTAGE_FREE) &&
		(atom->txnh_count          == 0) &&
		(atom->capture_count       == 0) &&
		(atom->refcount            == 0) &&
		atom_list_is_clean       (atom) &&
		txnh_list_empty          (& atom->txnh_list) &&
		capture_list_empty       (& atom->clean_nodes) &&
		fwaitfor_list_empty      (& atom->fwaitfor_list) &&
		fwaiting_list_empty      (& atom->fwaiting_list));
}
#endif

/* FIXME_LATER_JMACD Not sure how this is used yet.  The idea is to reserve a number of
 * blocks for use by the current transaction handle. */
/* Audited by: umka (2002.06.13) */
int
txn_reserve (int reserved UNUSED_ARG)
{
	return 0;
}

/* Begin a transaction in this context.  Currently this uses the reiser4_context's
 * trans_in_ctx, which means that transaction handles are stack-allocated.  Eventually
 * this will be extended to allow transaction handles to span several contexts. */
/* Audited by: umka (2002.06.13) */
void
txn_begin (reiser4_context *context)
{
	assert ("jmacd-544", context->trans == NULL);
	
	context->trans = & context->trans_in_ctx;

	/* FIXME_LATER_JMACD Currently there's no way to begin a TXN_READ_FUSING
	 * transcrash.  Default should be TXN_WRITE_FUSING.  Also, the _trans variable is
	 * stack allocated right now, but we would like to allow for dynamically allocated
	 * transcrashes that span multiple system calls.
	 */
	txnh_init (context->trans, TXN_WRITE_FUSING);
}

/* Finish a transaction handle context. */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
int
txn_end (reiser4_context *context)
{
	int ret = 0;
	txn_handle *txnh;
	
	assert("umka-283", context != NULL);
	
	/* closing non top-level context---nothing to do */
	if (context != context -> parent)
		return 0;

	txnh = context->trans;
	
	if (txnh != NULL) { 

		/* The txnh's field "atom" can be checked for NULL w/o holding a lock because
		 * only this thread's call to try_capture will set it. */
		if (txnh->atom != NULL) {
			ret = commit_txnh (txnh);
		}
		
		assert ("jmacd-633", txnh_isclean (txnh));

		context->trans = NULL;
	}
	
	return ret;
}

/*****************************************************************************************
				      TXN_ATOM
*****************************************************************************************/

/* Get the atom belonging to a txnh, which is not locked.  Return with both
 * txnh and atom locked.  This performs the necessary spin_trylock to break
 * the lock-ordering cycle.  May not return NULL. */
/* Audited by: umka (2002.06.13) */
txn_atom*
atom_get_locked_with_txnh_locked (txn_handle *txnh)
{
	txn_atom *atom;
	
	assert ("umka-180", txnh != NULL);
	assert ("jmacd-5108", spin_txnh_is_not_locked (txnh));

 try_again:

	spin_lock_txnh (txnh);

	atom = txnh->atom;

	assert ("jmacd-309", atom != NULL);

	if (! spin_trylock_atom (atom)) {
		/* If the atom lock fails then it could be in the middle of fusion, which
		 * means that txnh->atom pointer might be updated. */
		spin_unlock_txnh (txnh);

		/* Busy loop. */
		goto try_again;
	}

	return atom;
}

/* Get the current atom and spinlock it. */
txn_atom * get_current_atom_locked (void)
{
	reiser4_context * cx;
	txn_atom * atom;
	txn_handle * txnh; 

	cx = get_current_context();
	assert ("zam-437", cx != NULL);

	txnh = cx -> trans;
	assert ("zam-435", txnh != NULL);
	
	atom = atom_get_locked_with_txnh_locked (txnh);
	assert ("zam-436", atom != NULL);

	spin_unlock_txnh (txnh);
	return atom;
}


/* Get the atom belonging to a jnode, which is initially locked.  Return with
 * both jnode and atom locked.  This performs the necessary spin_trylock to
 * break the lock-ordering cycle.  Assumes the jnode is already locked, and
 * returns NULL if atom is not set. */
/* Audited by: umka (2002.06.13) */
txn_atom*
atom_get_locked_by_jnode (jnode *node)
{
	txn_atom *atom;

	assert ("umka-181", node != NULL);
	assert ("jmacd-5108", spin_jnode_is_locked (node));

 try_again:

	atom = node->atom;

	if (atom == NULL) {
		return NULL;
	}

	if (! spin_trylock_atom (atom)) {
		/* If the atom lock fails then it could be in the middle of fusion, which
		 * means that node->atom pointer might be updated. */
		spin_unlock_jnode (node);

		/* Busy loop. */
		spin_lock_jnode (node);
		goto try_again;
	}

	return atom;
}

/* Returns true if @node is dirty and part of the same atom as one of its
 * neighbors. */
/* Audited by: umka (2002.06.13) */
int
txn_same_atom_dirty (jnode *node, jnode *check, int alloc_check, int alloc_value)
{
	int compat;
	txn_atom *atom;
	
	assert("umka-182", node != NULL);
	assert("umka-183", check != NULL);
	
	/*
	 * FIXME:NIKITA->JMACD Not sure what this function is supposed to do
	 * if supplied with @check that is neither formatted nor unformatted
	 * (bitmap or so). Hence adding assertion.
	 */
	assert ("nikita-2373", 
		jnode_is_znode (check) || jnode_is_unformatted (check));

	/* Need a lock on CHECK to get its atom and to check various state
	 * bits.  Don't need a lock on NODE once we get the atom lock. */
	spin_lock_jnode (check);

	atom = atom_get_locked_by_jnode (check);

	if (atom == NULL) {
		compat = 0;
	} else {
		compat = (node->atom == atom && jnode_is_dirty (check));

		if (compat && jnode_is_znode (check)) {
			compat &= znode_is_connected (JZNODE (check));
		}

		if (compat && alloc_check) {
			compat &= (alloc_value == jnode_is_allocated (check));
		}

		spin_unlock_atom (atom);
	}

	spin_unlock_jnode (check);

	return compat;
}

/* Return true if an atom is currently "open". */
/* Audited by: umka (2002.06.13) */
static int
atom_isopen (txn_atom *atom)
{
	assert("umka-185", atom != NULL);
	
	return atom->stage & (ASTAGE_CAPTURE_FUSE | ASTAGE_CAPTURE_WAIT);
}

/* Decrement the atom's reference count and if it falls to zero, free it. */ 
/* Audited by: umka (2002.06.13) */
static void
atom_dec_and_unlock (txn_atom *atom)
{
	assert ("umka-186", atom != NULL);
	assert ("jmacd-1071", spin_atom_is_locked (atom));
	
	if (--atom->refcount == 0) {
		atom_free (atom);
	} else {
		spin_unlock_atom (atom);
	}
}

/* Return an new atom, locked.  This adds the atom to the transaction manager's list and
 * sets its reference count to 1, an artificial reference which is kept until it
 * commits.  We play strange games to avoid allocation under jnode & txnh spinlocks. */
static txn_atom*
atom_begin_andlock (txn_atom **atom_alloc, jnode *node, txn_handle *txnh)
{
	txn_atom *atom;
	txn_mgr  *mgr;

	assert ("jmacd-43228", spin_jnode_is_locked (node));
	assert ("jmacd-43227", spin_txnh_is_locked (txnh));
	assert ("jmacd-43226", node->atom == NULL);
	assert ("jmacd-43225", txnh->atom == NULL);

	if (*atom_alloc == NULL) {
		/* Cannot allocate under those spinlocks. */
		spin_unlock_jnode (node);
		spin_unlock_txnh (txnh);
		(*atom_alloc) = kmem_cache_alloc (_atom_slab, GFP_KERNEL);
		spin_lock_jnode (node);
		spin_lock_txnh (txnh);

		if (*atom_alloc == NULL) {
			return ERR_PTR (-ENOMEM);
		}

		/* Check if both atom pointers are still NULL... */
		if (node->atom != NULL || txnh->atom != NULL) {
			trace_on (TRACE_TXN, "alloc atom race\n");
			return ERR_PTR (-EAGAIN);
		}
	}

	atom = *atom_alloc;
	*atom_alloc = NULL;

	mgr = &get_super_private (reiser4_get_current_sb ())->tmgr;

	atom_init (atom);

	assert ("jmacd-17", atom_isclean (atom));

	/* Take the atom and txnmgr lock. */
	spin_lock_atom   (atom);
	spin_lock_txnmgr (mgr);

	atom_list_push_back (& mgr->atoms_list, atom);

	atom->atom_id = mgr->id_count ++;
	mgr->atom_count += 1;

	/* Release txnmgr lock */
	spin_unlock_txnmgr (mgr);

	/* One reference until it commits. */
	atom->refcount += 1;
	
	atom->stage = ASTAGE_CAPTURE_FUSE;

	trace_on (TRACE_TXN, "begin atom %u\n", atom->atom_id);
	
	return atom;
}

/* Return the number of pointers to this atom that must be updated during fusion.  This
 * approximates the amount of work to be done.  Fusion chooses the atom with fewer
 * pointers to fuse into the atom with more pointers. */
/* Audited by: umka (2002.06.13), umka (2002.16.15) */
static int
atom_pointer_count (txn_atom *atom)
{
	assert("umka-187", atom != NULL);

	/* This is a measure of the amount of work needed to fuse this atom into another. */
	assert ("jmacd-28", atom_isopen (atom));

	return atom->txnh_count + atom->capture_count;
}

/* Called holding the atom lock, this removes the atom from the transaction manager list
 * and frees it. */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
static void
atom_free (txn_atom *atom)
{
	txn_mgr  *mgr = &get_super_private (reiser4_get_current_sb ())->tmgr;

	assert("umka-188", atom != NULL);
	
	trace_on (TRACE_TXN, "free atom %u\n", atom->atom_id);	

	assert ("jmacd-18", spin_atom_is_locked (atom));

	/* Remove from the txn_mgr's atom list */
	spin_lock_txnmgr (mgr);
	mgr->atom_count -= 1;
	atom_list_remove_clean (atom);
	spin_unlock_txnmgr (mgr);

	/* Clean the atom */
	assert ("jmacd-16", (atom->stage == ASTAGE_FUSED ||
			     atom->stage == ASTAGE_PRE_COMMIT));
	atom->stage = ASTAGE_FREE;
	
	blocknr_set_destroy (& atom->delete_set);
	blocknr_set_destroy (& atom->wandered_map);

	assert ("jmacd-16", atom_isclean (atom));

	spin_unlock_atom (atom);

	kmem_cache_free (_atom_slab, atom);
}

/* Return true if an atom should commit now.  This will be determined by aging.  For now
 * this says to commit after the atom has 20 captured nodes.  The routine is only called
 * when the txnh_count drops to 0.
 */
/* Audited by: umka (2002.06.13) */
static int
atom_should_commit (txn_atom *atom)
{
	assert("umka-189", atom != NULL);
	return 
		(atom_pointer_count (atom) > get_current_super_private ()->txnmgr.atom_max_size) || 
		(atom->flags & ATOM_FORCE_COMMIT);
}

/* FIXME: temporary */
static void txn_wait_on_io (txn_atom *atom)
{
	jnode *scan;

	for (scan = capture_list_front (& atom->clean_nodes);
	     /**/ ! capture_list_end   (& atom->clean_nodes, scan);
	     scan = capture_list_next  (scan)) {

		if (scan->pg && PageWriteback (scan->pg)) {
			wait_on_page_writeback (scan->pg);
		}
	}
}

/* Called with the atom locked and no open txnhs, this function determines
 * whether the atom can commit and if so, initiates commit processing.
 * However, the atom may not be able to commit due to un-allocated nodes.  As
 * it finds such nodes, it calls the appropriate allocate/balancing routines.
 *
 * Called by the single remaining open txnh, which is closing.  Therefore as
 * long as we hold the atom lock none of the jnodes can be captured and/or
 * locked.
 */
/* Audited by: umka (2002.06.13) */
static int
atom_try_commit_locked (txn_atom *atom)
{
	int level;
	int ret = 0;
	jnode *scan;

	assert ("umka-190", atom != NULL);	
	assert ("jmacd-150", atom->txnh_count == 1);
	assert ("jmacd-151", atom_isopen (atom));

	trace_on (TRACE_TXN, "atom %u trying to commit %u: CAPTURE_WAIT\n", atom->atom_id, current_pid);

	/* When trying to commit, make sure we keep trying, also prevent new txnhs. */
	atom->flags |= ATOM_FORCE_COMMIT;
	atom->stage = ASTAGE_CAPTURE_WAIT;

	/* From the leaf level up, find dirty nodes in this transaction that need balancing/flushing. */
	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {

		if (capture_list_empty (& atom->dirty_nodes[level])) {
			continue;
		}

		scan = capture_list_front (& atom->dirty_nodes[level]);

		/* jnode_flush requires node locks, which require the atom
		 * lock and so on.  We begin this processing with the atom in
		 * the CAPTURE_WAIT state, unlocked. */
		spin_unlock_atom (atom);

		/* Call jnode_flush() without tree_lock held. */
		if ((ret = jnode_flush (scan, NULL, JNODE_FLUSH_COMMIT)) != 0) {
			warning ("nikita-2420", "jnode flush failed: %i", ret);
			return ret;
		}

		/* Atom may be deleted at this point -- don't use it. */
		return -EAGAIN;
	}

	/* Up to this point we have been flushing and after flush is called we return
	 * -EAGAIN.  Now we can commit.  We cannot return -EAGAIN at this point, commit
	 * should be successful. */
	atom->stage = ASTAGE_PRE_COMMIT;

	trace_on (TRACE_TXN, "commit atom %u: PRE_COMMIT\n", atom->atom_id);
	trace_on (TRACE_FLUSH, "everything flushed atom %u: PRE_COMMIT\n", atom->atom_id);
	
	if (REISER4_DEBUG) {
		int level;
		for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level ++) {
			assert ("zam-542", capture_list_empty(&atom->dirty_nodes[level]));
		}
	}

	/* FIXME: temporary */
	txn_wait_on_io (atom);

	trace_on (TRACE_FLUSH, "everything written back atom %u\n", atom->atom_id);

	if (WRITE_LOG) {
		/* We unlock atom to allow journal writer and others (block allocator
		 * hooks) to do things which may schedule, like memory allocation or
		 * disk i/o.  ASTAGE_PRE_COMMIT should guarantee that current atom
		 * can't be fused */
		spin_unlock_atom (atom);

		ret = reiser4_write_logs();
		if (ret) {
			warning ("zam-597", "write log failed"); 
			return ret;
		}

		/* Now close this txnh's reference to the atom. */
		spin_lock_atom (atom);
	}

	invalidate_clean_list (atom);

	/* FIXME: Josh needs to clarify this. */
	wakeup_atom_waitfor_list (atom);
	wakeup_atom_waiting_list (atom);

	/* Decrement the "until commit" reference, at least one txnh (the caller) is
	 * still open. */
	atom->refcount -= 1;

	assert ("jmacd-1070", atom->refcount > 0);
	assert ("jmacd-1062", atom->capture_count == 0);
	assert ("jmacd-1071", spin_atom_is_locked (atom));

	trace_on (TRACE_TXN, "commit atom finished %u refcount %d\n", atom->atom_id, atom->refcount);
	
	return 0;
}

/*****************************************************************************************
				      TXN_TXNH
*****************************************************************************************/

/* Called to force commit of any outstanding atoms.  Later this should be improved to: (1)
 * wait for atoms with open txnhs to commit and (2) not wait indefinitely if new atoms are
 * created. */
/* Audited by: umka (2002.06.13) umka (2002.06.15) */
int
txn_mgr_force_commit (struct super_block *super)
{
	int ret;
	txn_atom *atom;
	txn_mgr *mgr;
	txn_handle  *txnh;
	reiser4_context *host_context;
	reiser4_context local_context;

	host_context = get_current_context();
	/*
	 * FIXME:NIKITA->NIKITA this only works for top-level contexts.
	 */
	check_me ("nikita-2094", __REISER4_EXIT (host_context) == 0);
	assert ("nikita-2095", get_current_context() == NULL);

	ret = init_context (& local_context, super);
	if (ret != 0)
		return ret;
	mgr = & get_super_private (super)->tmgr;
	
	txnh = get_current_context ()->trans;

 again:

	spin_lock_txnmgr (mgr);

	for (atom = atom_list_front (& mgr->atoms_list);
	     /**/ ! atom_list_end   (& mgr->atoms_list, atom);
	     atom = atom_list_next  (atom)) {

		spin_lock_atom (atom);

		if (atom->stage < ASTAGE_PRE_COMMIT) {

			spin_unlock_txnmgr (mgr);
			spin_lock_txnh (txnh);

			/* Set the atom to force committing */
			atom->flags = ATOM_FORCE_COMMIT;

			/* Add force-context txnh */
			capture_assign_txnh_nolock (atom, txnh);

			spin_unlock_txnh (txnh);
			spin_unlock_atom (atom);

 			if ((ret = txn_end (& local_context)) != 0) {
				__REISER4_EXIT (& local_context);
				init_context (host_context, super);
				return ret;
			}

			schedule(); /* FIXME-ZAM: just a hack; proper synchronization should be added here */

			txn_begin (& local_context);

			goto again;
		}

		spin_unlock_atom (atom);
	}

	spin_unlock_txnmgr (mgr);

	__REISER4_EXIT (& local_context);
	init_context (host_context, super);
	return 0;
}

/* FIXME: comment */
static void invalidate_clean_list (txn_atom * atom)
{

	while (! capture_list_empty (& atom->clean_nodes)) {
		jnode * scan;

		scan = capture_list_front (& atom->clean_nodes);
		
		assert ("jmacd-1063", scan != NULL);
		assert ("jmacd-1061", scan->atom == atom);
		
		uncapture_block (atom, scan);
	}
}

/* Called to commit a transaction handle.  This decrements the atom's number of open
 * handles and if it is the last handle to commit and the atom should commit, initiates
 * atom commit. */
static int
commit_txnh (txn_handle *txnh)
{
	int ret = 0;
	txn_atom *atom;
	int failed = 0;

	assert("umka-192", txnh != NULL);
	
 again:
	/* Get the atom and txnh locked. */
	atom = atom_get_locked_with_txnh_locked (txnh);

	/* The txnh stays open while we try to commit, since it is still being used, but
	 * we don't need the txnh lock while trying to commit. */
	spin_unlock_txnh (txnh);

	trace_on (TRACE_TXN, "commit_txnh: atom %u failed %u; txnh_count %u; should_commit %u\n", atom->atom_id, failed, atom->txnh_count, atom_should_commit (atom));

	/* Only the atom is still locked. */
	if (! failed && (atom->txnh_count == 1) && atom_should_commit (atom)) {

		ret = atom_try_commit_locked (atom);

		if (ret != 0) {
			assert ("jmacd-1027", spin_atom_is_not_locked (atom));
			if (ret != -EAGAIN) {
				warning ("jmacd-7881", "transaction commit failed: %d", ret);
				failed = 1;
			} else {
				trace_on (TRACE_TXN, "try_commit atom repeat\n");
				ret = 0;
			}
			goto again;
		}
	}

	assert ("jmacd-1027", spin_atom_is_locked (atom));
	spin_lock_txnh (txnh);

	atom->txnh_count -= 1;
	txnh->atom = NULL;

	txnh_list_remove (txnh);

	trace_on (TRACE_TXN, "close txnh atom %u refcount %d\n", atom->atom_id, atom->refcount-1);

	spin_unlock_txnh (txnh);
	atom_dec_and_unlock (atom);

	/* Note: We are ignoring the failure code.  Can't change the result of the caller.
	 * E.g., in write():
	 *
	 *   result = 512;
	 *   REISER4_EXIT (result);
	 *
	 * It cannot "forget" that 512 bytes were written, even if commit fails.  This
	 * means force_txn_commit will retry forever.  Is there a better solution?
	 */
	return 0;
}

#if REISER4_USER_LEVEL_SIMULATION
/* This function is provided for user-level testing only.  It scans a list of open
 * transactions, finds the first dirty node it can, and then calls jnode_flush() on that
 * node.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
int memory_pressure (struct super_block *super, int *nr_to_flush)
{
	txn_mgr *mgr = & get_super_private (super)->tmgr;
	txn_atom *atom;
	jnode *node = NULL;
	txn_handle  *txnh;
	int ret = 0, level;
	reiser4_context *ctx = get_current_context ();

	assert("umka-193", super != NULL);

	txnh = ctx->trans;

	assert("umka-288", txnh != NULL);
	assert("jmacd-289", txnh->atom == NULL);

	spin_lock_txnh (txnh);
	spin_lock_txnmgr (mgr);

	/* FIXME: There may be a race causing a committing atom to be flushed here... */
	for (atom = atom_list_front (& mgr->atoms_list);
	     /**/ ! atom_list_end   (& mgr->atoms_list, atom) && node == NULL;
	     atom = atom_list_next  (atom)) {

		if (! spin_trylock_atom (atom)) {
			continue;
		}

		for (level = 0; atom->stage < ASTAGE_CAPTURE_WAIT && level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
			if (! capture_list_empty (& atom->dirty_nodes [level])) {
				/* Found a dirty node to flush. */
				node = jref (capture_list_back (& atom->dirty_nodes [level]));

				/* Add this context to the same atom.  Same as txn_attach_txnh_to_node does. */
				capture_assign_txnh_nolock (atom, txnh);

				/* By setting node != NULL, this will break the outer loop
				 * as well, so only one jput() is needed below. */
				break;
			}				
		}

		spin_unlock_atom (atom);
	}

	spin_unlock_txnmgr (mgr);
	spin_unlock_txnh  (txnh);

	if (node != NULL) {

		trace_on (TRACE_TXN, "memory pressure atom %u node %p\n", atom->atom_id, node);

		ret = jnode_flush (node, nr_to_flush, jnode_is_znode (node) ? JNODE_FLUSH_MEMORY_FORMATTED : JNODE_FLUSH_MEMORY_UNFORMATTED);

		jput (node);

		if (ret == 0) {
			ret = commit_txnh (txnh);

			assert ("jmacd-635", txnh_isclean (txnh));
		} else {
			warning ("jmacd-81726", "flush failed: %d", ret);
		}
	} else {
		(*nr_to_flush) = 0;
	}

	return ret;
}
#endif

/*****************************************************************************************
				   TXN_TRY_CAPTURE
*****************************************************************************************/

/* This routine attempts a single block-capture request.  It may return -EAGAIN if some
 * condition indicates that the request should be retried, and it may block if the
 * txn_capture mode does not include the TXN_CAPTURE_NONBLOCKING request flag.
 *
 * The txn_try_capture() function (below) is the external interface, which calls this
 * function repeatedly as long as -EAGAIN is returned.
 *
 * This routine encodes the basic logic of block capturing described by:
 *
 *   http://namesys.com/txn-doc.html
 *
 * Our goal here is to ensure that any two blocks that contain dependent modifications
 * should commit at the same time.  This function enforces this discipline by initiating
 * fusion whenever a transaction handle belonging to one atom requests to read or write a
 * block belonging to another atom (TXN_CAPTURE_WRITE or TXN_CAPTURE_READ_ATOMIC).
 *
 * In addition, this routine handles the initial assignment of atoms to blocks and
 * transaction handles.  These are possible outcomes of this function:
 *
 * 1. The block and handle are already part of the same atom: return immediate success
 *
 * 2. The block is assigned but the handle is not: call capture_assign_txnh to assign
 *    the handle to the block's atom.
 *
 * 3. The handle is assigned but the block is not: call capture_assign_block to assign
 *    the block to the handle's atom.
 *
 * 4. Both handle and block are assigned, but to different atoms: call capture_init_fusion
 *    to fuse atoms.
 *
 * 5. Neither block nor handle are assigned: create a new atom and assign them both.
 *
 * 6. A read request for a non-captured block: return immediate success.
 *
 * This function acquires and releases the handle's spinlock.  This function is called
 * under the jnode lock and if the return value is 0, it returns with the jnode lock still
 * held.  If the return is -EAGAIN or some other error condition, the jnode lock is
 * released.  The external interface (txn_try_capture) manages re-aquiring the jnode lock
 * in the failure case.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
static int
try_capture_block (txn_handle  *txnh,
		   jnode       *node,
		   txn_capture  mode,
		   txn_atom   **atom_alloc)
{
	int ret;
	txn_atom *block_atom;
	txn_atom *txnh_atom;

	/* Should not call capture for READ_NONCOM requests, handled in txn_try_capture. */
	assert ("jmacd-567", CAPTURE_TYPE (mode) != TXN_CAPTURE_READ_NONCOM);

	/* FIXME_LATER_JMACD Should assert that atom->tree == node->tree somewhere. */
	
	assert("umka-194", txnh != NULL);
	assert("umka-195", node != NULL);

	/* The jnode is already locked!  Being called from txn_try_capture(). */
	assert ("jmacd-567", spin_jnode_is_locked (node));

	/* Get txnh spinlock, this allows us to compare txn_atom pointers but it doesn't
	 * let us touch the atoms themselves. */
	spin_lock_txnh (txnh);

	block_atom = node->atom;
	txnh_atom  = txnh->atom;

	if (block_atom != NULL) {
		/* The block has already been assigned to an atom. */

		if (block_atom == txnh_atom) {
			/* No extra capturing work required. */
		} else if (txnh_atom == NULL) {

			/* The txnh is unassigned, try to assign it. */
			if ((ret = capture_assign_txnh (node, txnh, mode)) != 0) {
				/* EAGAIN or otherwise */
				assert ("jmacd-6129", spin_txnh_is_not_locked (txnh));
				assert ("jmacd-6130", spin_jnode_is_not_locked (node));
				return ret;
			}

			/* Either the txnh is now assigned to the block's atom or the read-request was
			 * granted because the block is committing.  Locks still held. */
		} else {
			/* In this case, both txnh and node belong to different atoms.  This function
			 * returns -EAGAIN on successful fusion, 0 on the fall-through case. */
			if ((ret = capture_init_fusion (node, txnh, mode)) != 0) {
				assert ("jmacd-6131", spin_txnh_is_not_locked (txnh));
				assert ("jmacd-6132", spin_jnode_is_not_locked (node));
				return ret;
			}

			/* The fall-through case is read request for committing block.  Locks still
			 * held. */
		}

	} else if ((mode & TXN_CAPTURE_WTYPES) != 0) {

		/* In this case, the page is unlocked and the txnh wishes exclusive access. */

		if (txnh_atom != NULL) {
			/* The txnh is already assigned: add the page to its atom. */
			if ((ret = capture_assign_block (txnh, node)) != 0) {
				/* EAGAIN or otherwise */
				assert ("jmacd-6133", spin_txnh_is_not_locked (txnh));
				assert ("jmacd-6134", spin_jnode_is_not_locked (node));
				return ret;
			}

			/* Success: Locks are still held. */

		} else {

			/* In this case, neither txnh nor page are assigned to an atom. */
			block_atom = atom_begin_andlock (atom_alloc, node, txnh);

			if (! IS_ERR (block_atom)) {
				/* Assign both, release atom lock. */
				assert ("jmacd-125", block_atom->stage == ASTAGE_CAPTURE_FUSE);

				capture_assign_txnh_nolock (block_atom, txnh);
				capture_assign_block_nolock  (block_atom, node);

				spin_unlock_atom (block_atom);
			} else {
				/* Release locks and fail */
				spin_unlock_jnode  (node);
				spin_unlock_txnh (txnh);
				return PTR_ERR (block_atom);
			}

			/* Success: Locks are still held. */
		}

	} else {
		/* The jnode is uncaptured and its a read request -- fine. */
		assert ("jmacd-411", CAPTURE_TYPE (mode) == TXN_CAPTURE_READ_ATOMIC);
	}

	/* Successful case: both jnode and txnh are still locked. */
	assert ("jmacd-740", spin_txnh_is_locked (txnh));
	assert ("jmacd-741", spin_jnode_is_locked (node));

	/* Release txnh lock, return with the jnode still locked. */
	spin_unlock_txnh (txnh);

	return 0;
}

/* This function sets up a call to try_capture_block and repeats as long as -EAGAIN is
 * returned by that routine.  The txn_capture request mode is computed here depending on
 * the transaction handle's type and the lock request.  This is called from the depths of
 * the lock manager with the jnode lock held and it always returns with the jnode lock
 * held.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
/* AUDIT: node locking policy is very difficult because locking/unlocking is distributed through few functions */
int
txn_try_capture (jnode           *node,
		 znode_lock_mode  lock_mode,
		 int              non_blocking)
{
	int ret;
	txn_handle  *txnh;
	txn_capture  cap_mode;
	txn_atom    *atom_alloc = NULL;

	if ((txnh = get_current_context ()->trans) == NULL) {
		rpanic ("jmacd-492", "invalid transaction txnh");
	}
	
	/* FIXME_JMACD No way to set TXN_CAPTURE_READ_MODIFY yet. */

	if (lock_mode == ZNODE_WRITE_LOCK) {
		cap_mode = TXN_CAPTURE_WRITE;
	} else if ((txnh->mode == TXN_READ_FUSING) && (jnode_get_level( node ) == LEAF_LEVEL)) {
		/* We only need a READ_FUSING capture at the leaf level.  This is because
		 * the internal levels of the tree (twigs included) are redundent from the
		 * point of the user that asked for a read-fusing transcrash.  The user
		 * only wants to read-fuse atoms due to reading uncommitted data that
		 * another user has written.  It is the file system that reads/writes the
		 * internal tree levels, the user only reads/writes leaves. */
		cap_mode = TXN_CAPTURE_READ_ATOMIC;
	} else {
		/* In this case (read lock at a non-leaf) there's no reason to capture. */
		/* cap_mode = TXN_CAPTURE_READ_NONCOM; */
		return 0;
	}

	if (non_blocking) {
		cap_mode |= TXN_CAPTURE_NONBLOCKING;
	}

	assert ("jmacd-604", spin_jnode_is_locked (node));

 repeat:
	/* Repeat try_capture as long as -EAGAIN is returned. */
	ret = try_capture_block (txnh, node, cap_mode, & atom_alloc);

	/* Regardless of non_blocking:
	 *
	 * If ret == 0 then jnode is still locked.
	 * If ret != 0 then jnode is unlocked.
	 */

	if (ret == -EAGAIN && ! non_blocking) {
		/* EAGAIN implies all locks were released, therefore we need to take the
		 * jnode's lock again. */
		spin_lock_jnode (node);

		/* Although this may appear to be a busy loop, it is not.  There are
		 * several conditions that cause EAGAIN to be returned by the call to
		 * txn_block_try_capture, all cases indicating some kind of state
		 * change that means you should retry the request and will get a different
		 * result.  In some cases this could be avoided with some extra code, but
		 * generally it is done because the necessary locks were released as a
		 * result of the operation and repeating is the simplest thing to do (less
		 * bug potential).  The cases are: atom fusion returns EAGAIN after it
		 * completes (jnode and txnh were unlocked); race conditions in
		 * assign_block, assign_txnh, and init_fusion return EAGAIN (trylock
		 * failure); after going to sleep in capture_fuse_wait (request was
		 * blocked but may now succeed).  I'm not quite sure how capture_copy
		 * works yet, but it may also return EAGAIN.  When the request is
		 * legitimately blocked, the requestor goes to sleep in fuse_wait, so this
		 * is not a busy loop. */
		goto repeat;
	}

	if (ret != 0) {
		/* Failure means jnode is not locked.  FIXME_LATER_JMACD May want to fix
		 * the above code to avoid releasing the lock and reaquiring it, but there
		 * are cases were failure occurs when the lock is not held, and those
		 * cases would need to be modified to re-take the lock. */
		spin_lock_jnode (node);
	}

	/* Jnode is still locked. */
	assert ("jmacd-760", spin_jnode_is_locked (node));

	if (atom_alloc != NULL) {
		kmem_cache_free (_atom_slab, atom_alloc);
	}

	return ret;
}

/* This is the interface to capture unformatted nodes via their struct page
 * reference. */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
int
txn_try_capture_page  (struct page        *pg,
		       znode_lock_mode     lock_mode,
		       int                 non_blocking)
{
	int ret;

	jnode *node;
       
	assert("umka-292", pg != NULL);
	
	if ( IS_ERR(node = jnode_of_page (pg))) {
		return PTR_ERR(node);
	}
	
	spin_lock_jnode (node);
	
	ret = txn_try_capture (node, lock_mode, non_blocking);
	if (ret == 0) {
		spin_unlock_jnode (node);
	}
	jput (node);
	return ret;
}

/* This interface is used by flush routines when they need to prevent an atom from
 * committing while they perform early flushing.  The node is already captured but the
 * txnh is not.
 */
int txn_attach_txnh_to_node (txn_handle *txnh, jnode *node, txn_flags flags)
{
	txn_atom *atom;
	int ret = 0;

	assert ("jmacd-77917", spin_txnh_is_locked (txnh));
	assert ("jmacd-77918", txnh->atom == NULL);

	spin_lock_jnode (node);

	atom = atom_get_locked_by_jnode (node);

	/* Atom can commit at this point. */
	if (atom == NULL) {
		ret = -ENOENT;
		goto fail_unlock;
	}

	atom->flags |= flags;

	capture_assign_txnh_nolock (atom, txnh);

	spin_unlock_atom (atom);
 fail_unlock:
	spin_unlock_jnode (node);
	return ret;
}

/* This informs the transaction manager when a node is deleted.  Add the block to the
 * atom's delete set and uncapture the block.  Handles the EAGAIN result from
 * blocknr_set_add_block, which is returned by blocknr_set_add when it releases the atom
 * lock to perform an allocation.  The atom could fuse while this lock is held, which is
 * why the EAGAIN must be handled by repeating the call to atom_get_locked_by_jnode.  The
 * second call is guaranteed to provide a pre-allocated blocknr_entry so it can only
 * "repeat" once.  */
void txn_delete_page (struct page *pg)
{
	int ret;
	jnode *node;
	txn_atom *atom;
	blocknr_set_entry *blocknr_entry = NULL;
	
	assert("umka-199", pg != NULL);

	ClearPageDirty (pg);

	node = (jnode *)(pg->private);

	if (node == NULL)
		return;

	jnode_set_clean (node);

	spin_lock_jnode (node);

 repeat:
	atom = atom_get_locked_by_jnode (node);

	if (atom == NULL) {
		assert ("jmacd-7111", ! jnode_is_dirty (node));
		spin_unlock_jnode (node);
		return;
	}

	if (!blocknr_is_fake(jnode_get_block(node))) {
		if (REISER4_DEBUG) {
			struct super_block * s = reiser4_get_current_sb();

			reiser4_spin_lock_sb(s);
			assert ("zam-561", *jnode_get_block(node) < reiser4_block_count(s));
			reiser4_spin_unlock_sb(s);
		}

		ret = blocknr_set_add_block (atom, & atom->delete_set, & blocknr_entry, jnode_get_block (node));

		if (ret == -EAGAIN) {
			/* Jnode is still locked, which atom_get_locked_by_jnode expects. */
			goto repeat;
		}
	} else {
		reiser4_count_fake_deallocation ((__u64)1);
		reiser4_release_grabbed_space ((__u64)1);
	}

	assert ("jmacd-5177", blocknr_entry == NULL);

	spin_unlock_jnode (node);

	uncapture_block (atom, node);

	spin_unlock_atom (atom);
}

/* No-locking version of assign_txnh.  Sets the transaction handle's atom pointer,
 * increases atom refcount, adds to txnh_list. */
/* Audited by: umka (2002.06.13) */
static void
capture_assign_txnh_nolock (txn_atom   *atom,
			    txn_handle *txnh)
{
	assert ("umka-200", atom != NULL);
	assert ("umka-201", txnh != NULL);
	
	assert ("jmacd-822", spin_txnh_is_locked (txnh));
	assert ("jmacd-823", spin_atom_is_locked (atom));
	assert ("jmacd-824", txnh->atom == NULL);

	atom->refcount += 1;

	trace_on (TRACE_TXN, "assign txnh atom %u refcount %d\n", atom->atom_id, atom->refcount);
	
	txnh->atom = atom;
	txnh_list_push_back (& atom->txnh_list, txnh);
	atom->txnh_count += 1;
}

/* No-locking version of assign_block.  Sets the block's atom pointer, references the
 * block, adds it to the clean or dirty capture_jnode list, increments capture_count. */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
static void
capture_assign_block_nolock (txn_atom *atom,
			     jnode    *node)
{
	assert ("umka-202", atom != NULL);
	assert ("umka-203", node != NULL);
	
	assert ("jmacd-321", spin_jnode_is_locked (node));
	
	/* AUDIT: Check whether atom is locked due to atom modifying protection */
	assert ("umka-295", spin_atom_is_locked (atom));
	
	assert ("jmacd-323", node->atom == NULL);

	node->atom = atom;

	if (jnode_is_dirty (node)) {
		capture_list_push_back (& atom->dirty_nodes[ jnode_real_level (node) ], node);
	} else {
		capture_list_push_back (& atom->clean_nodes, node);
	}

	atom->capture_count += 1;
	/*
	 * reference to jnode is acquired by atom.
	 */
	jref (node);
	ON_DEBUG (++ lock_counters() -> t_refs);

	trace_on (TRACE_TXN, "capture %p for atom %u (captured %u)\n", node, atom->atom_id, atom->capture_count);
}

/* Set the dirty status for this jnode.  If the jnode is not already dirty, this involves locking the atom (for its
 * capture lists), removing from the clean list and pushing in to the dirty list of the appropriate level.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
void jnode_set_dirty( jnode *node )
{
	txn_atom *atom;
	
	assert ("umka-204", node != NULL);	
	assert ("umka-296", current_tree != NULL);	

	spin_lock_jnode (node);

	if (! jnode_is_dirty (node)) {

		JF_SET (node, ZNODE_DIRTY);

		assert ("jmacd-3981", jnode_is_dirty (node));

		/* If the atom is not set yet, it will be added to the appropriate list in
		 * capture_assign_block_nolock. */
		atom = atom_get_locked_by_jnode (node);

		/* Sometimes a node is set dirty before being captured -- the case for new jnodes.  In that case the
		 * jnode will be added to the appropriate list in capture_assign_block_nolock. Another reason not to
		 * re-link jnode is that jnode is on a flush queue (see flush.c for details) */
		if (atom != NULL && !JF_ISSET(node, ZNODE_FLUSH_QUEUED)) {

			int level = jnode_real_level (node);

			assert ("zam-654", !(JF_ISSET(node, ZNODE_WANDER) && atom->stage >= ASTAGE_PRE_COMMIT));

			capture_list_remove     (node);
			capture_list_push_front (& atom->dirty_nodes[level], node);

			spin_unlock_atom (atom);
		}

		/*trace_on (TRACE_FLUSH, "dirty %sformatted node %p\n", 
		  jnode_is_unformatted (node) ? "un" : "", node);*/
	}

	if (jnode_is_znode (node)) {
		/* bump version counter in znode */
		spin_lock_tree (current_tree);
		JZNODE (node)->version = ++ current_tree->znode_epoch;
		spin_unlock_tree (current_tree);
		/* the flush code sets a node dirty even though it is read locked... but
		 * it captures it first.  However, the new assertion (jmacd-9777) seems to
		 * contradict the statement above, that a node is captured before being
		 * captured.  Perhaps that is no longer true. */
		/*assert ("nikita-1900", znode_is_write_locked (JZNODE (node)));*/
		assert ("jmacd-9777", node->atom != NULL && znode_is_any_locked (JZNODE (node)));
	}

	if (jnode_page (node) != NULL)
		set_page_dirty (jnode_page (node));
	else
		/*
		 * FIXME-NIKITA dubious. What if jnode doesn't have page,
		 * because it was early flushed, or ->releasepaged?
		 */
		assert ("zam-596", znode_above_root(JZNODE(node)));

	spin_unlock_jnode (node);
}

/* Unset the dirty status for this jnode.  If the jnode is dirty, this involves locking the atom (for its capture
 * lists), removing from the dirty_nodes list and pushing in to the clean list.
 */
void jnode_set_clean( jnode *node )
{
	txn_atom *atom;

	assert ("umka-205", node != NULL);	
	assert ("jmacd-1083", spin_jnode_is_not_locked (node));

	spin_lock_jnode (node);

	if (jnode_is_dirty (node)) {

		JF_CLR (node, ZNODE_DIRTY);

		assert ("jmacd-9366", ! jnode_is_dirty (node));
		
#if REISER4_DEBUG_MODIFY
		if (jnode_is_znode (node))
			JZNODE (node)->cksum = znode_checksum (JZNODE (node));
#endif

		/*trace_on (TRACE_FLUSH, "clean %sformatted node %p\n", 
		            jnode_is_unformatted (node) ? "un" : "", node);*/
	}

	/* do not steal nodes from flush queue */
	if (!JF_ISSET(node, ZNODE_FLUSH_QUEUED)) {
		/* FIXME-VS: remove jnode from capture list even when jnode is not
		 * dirty.  JMACD says: Is it wrong? */
		atom = atom_get_locked_by_jnode (node);
	
		capture_list_remove (node);

		/* Now it's possible that atom may be NULL, in case this was called
		 * from invalidate page */
		if (atom != NULL) {

			capture_list_push_front (& atom->clean_nodes, node);

			spin_unlock_atom (atom);
		}
	}

	spin_unlock_jnode (node);
}

/* This function assigns a block to an atom, but first it must obtain the atom lock.  If
 * the atom lock is busy, it returns -EAGAIN to avoid deadlock with a fusing atom.  Since
 * the transaction handle is currently open, we know the atom must also be open. */
/* Audited by: umka (2002.06.13) */
static int
capture_assign_block (txn_handle *txnh,
		      jnode      *node)
{
	txn_atom *atom;
       
	assert("umka-206", txnh != NULL);
	assert("umka-207", node != NULL);
	
	atom = txnh->atom;

	assert("umka-297", atom != NULL);

	if (! spin_trylock_atom (atom)) {

		/* EAGAIN releases locks. */
		spin_unlock_txnh (txnh);
		spin_unlock_jnode  (node);

		return -EAGAIN;

	} else {

		assert ("jmacd-19", atom_isopen (atom));

		/* Add page to capture list. */
		capture_assign_block_nolock (atom, node);

		/* Success holds onto jnode & txnh locks.  Unlock atom. */
		spin_unlock_atom (atom);
		return 0;
	}
}

/* This function assigns a handle to an atom, but first it must obtain the atom lock.  If
 * the atom is busy, it returns -EAGAIN to avoid deadlock with a fusing atom.  Unlike
 * capture_assign_block, the atom may be closed but we cannot know this until the atom is
 * locked.  If the atom is closed and the request is to read, it is as if the block is
 * unmodified and the request is satisified without actually assigning the transaction
 * handle.  If the atom is closed and the handle requests to write the block, then
 * initiate copy-on-capture.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
static int
capture_assign_txnh (jnode       *node,
		     txn_handle  *txnh,
		     txn_capture  mode)
{
	txn_atom *atom;
	
	assert("umka-208", node != NULL);
	assert("umka-209", txnh != NULL);
	
	atom = node->atom;

	assert("umka-298", atom != NULL);

	if (! spin_trylock_atom (atom)) {

		/* EAGAIN releases locks. */
		spin_unlock_jnode (node);
		spin_unlock_txnh  (txnh);

		return -EAGAIN;

	} else if (atom->stage == ASTAGE_CAPTURE_WAIT) {

		/* The atom could be blocking requests--this is the first chance we've had
		 * to test it.  Since this txnh is not yet assigned, the fuse_wait logic
		 * is not to avoid deadlock, its just waiting.  Releases all three locks
		 * and returns EAGAIN. */

		return capture_fuse_wait (node, txnh, atom, NULL, mode);

	} else if (atom->stage > ASTAGE_CAPTURE_WAIT) {
		
		/* The block is involved with a committing atom. */
		if (CAPTURE_TYPE (mode) == TXN_CAPTURE_READ_ATOMIC) {

			/* A read request for a committing block can be satisfied w/o
			 * COPY-ON-CAPTURE. */

			/* Success holds onto the jnode & txnh lock.  Continue to unlock
			 * atom below. */

		} else {

			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			 * releases all three locks. */
			return capture_copy (node, txnh, atom, NULL, mode);
		}

	} else {

		assert ("jmacd-160", atom->stage == ASTAGE_CAPTURE_FUSE);

		/* Add txnh to active list. */
		capture_assign_txnh_nolock (atom, txnh);

		/* Success holds onto the jnode & txnh lock.  Continue to unlock atom
		 * below. */
	}

	/* Unlock the atom */
	spin_unlock_atom (atom);
	return 0;
}

/* Wakeup every handle on the atom's WAITFOR list */
/* Audited by: umka (2002.06.13) */
static void
wakeup_atom_waitfor_list (txn_atom *atom)
{
	txn_wait_links *wlinks;
	
	assert("umka-210", atom != NULL);
	
	/* atom is locked */
	for (wlinks = fwaitfor_list_front (& atom->fwaitfor_list);
	     /**/   ! fwaitfor_list_end   (& atom->fwaitfor_list, wlinks);
	     wlinks = fwaitfor_list_next  (wlinks)) {

		/* Wake up. */
		reiser4_wake_up (wlinks->_lock_stack);
	}
}

/* Wakeup every handle on the atom's WAITING list */
/* Audited by: umka (2002.06.13) */
static void
wakeup_atom_waiting_list (txn_atom *atom)
{
	txn_wait_links *wlinks;

	assert("umka-211", atom != NULL);
	
	/* atom is locked */
	for (wlinks = fwaiting_list_front (& atom->fwaiting_list);
	     /**/   ! fwaiting_list_end   (& atom->fwaiting_list, wlinks);
	     wlinks = fwaiting_list_next  (wlinks)) {

		/* Wake up. */
		reiser4_wake_up (wlinks->_lock_stack);
	}
}

/* The general purpose of this function is to wait on the first of two possible events.
 * The situation is that a handle (and its atom atomh) is blocked trying to capture a
 * block (i.e., node) but the node's atom (atomf) is in the CAPTURE_WAIT state.  The
 * handle's atom (atomh) is not in the CAPTURE_WAIT state.  However, atomh could fuse with
 * another atom or, due to age, enter the CAPTURE_WAIT state itself, at which point it
 * needs to unblock the handle to avoid deadlock.  When the txnh is unblocked it will
 * proceed and fuse the two atoms in the CAPTURE_WAIT state.
 *
 * In other words, if either atomh or atomf change state, the handle will be awakened,
 * thus there are two lists per atom: WAITING and WAITFOR.
 * 
 * This is also called by capture_assign_txnh with (atomh == NULL) to wait for atomf to
 * close but it is not assigned to an atom of its own.
 *
 * Lock ordering in this method: all four locks are held: JNODE_LOCK, TXNH_LOCK,
 * BOTH_ATOM_LOCKS.  Result: all four locks are released.
 */
/* Audited by: umka (2002.06.13) */
static int
capture_fuse_wait (jnode *node, txn_handle *txnh, txn_atom *atomf, txn_atom *atomh, txn_capture mode)
{
	int ret;

	/* Initialize the waiting list links. */
	txn_wait_links wlinks;

	assert("umka-212", node != NULL);
	assert("umka-213", txnh != NULL);
	assert("umka-214", atomf != NULL);

	if ((mode & TXN_CAPTURE_NONBLOCKING) != 0) {
		spin_unlock_jnode  (node);
		spin_unlock_txnh (txnh);
		spin_unlock_atom   (atomf);

		if (atomh) {
			spin_unlock_atom (atomh);
		}

		trace_on (TRACE_TXN, "thread %u nonblocking on atom %u\n", current_pid, atomf->atom_id);

		return -EAGAIN;
	}
	
	wlinks._lock_stack = get_current_lock_stack ();

	fwaitfor_list_clean (& wlinks);
	fwaiting_list_clean (& wlinks);

	/* We do not need the node lock. */
	spin_unlock_jnode  (node);

	/* Add txnh to atomf's waitfor list, unlock atomf. */
	fwaitfor_list_push_back (& atomf->fwaitfor_list, & wlinks);
	atomf->refcount += 1;
	spin_unlock_atom (atomf);

	if (atomh) {
		/* Add txnh to atomh's waiting list, unlock atomh. */
		fwaiting_list_push_back (& atomh->fwaiting_list, & wlinks);
		atomh->refcount += 1;
		spin_unlock_atom (atomh);
	}

	trace_on (TRACE_TXN, "thread %u waitfor %u waiting %u\n", current_pid, atomf->atom_id, atomh ? atomh->atom_id : 0);

	/* Go to sleep. */
	spin_unlock_txnh (txnh);
	
	if ((ret = prepare_to_sleep (wlinks._lock_stack)) != 0) {
		trace_on (TRACE_TXN, "thread %u deadlock blocking on atom %u\n", current_pid, atomf->atom_id);
	} else {
		ret = go_to_sleep (wlinks._lock_stack);

		if (ret == 0) {
			ret = -EAGAIN;
		}

		trace_on (TRACE_TXN, "thread %u wakeup %u waiting %u\n", current_pid, atomf->atom_id, atomh ? atomh->atom_id : 0);
	}

	/* Remove from the waitfor list. */
	spin_lock_atom (atomf);
	fwaitfor_list_remove (& wlinks);
	atom_dec_and_unlock (atomf);

	if (atomh) {
		/* Remove from the waiting list. */
		spin_lock_atom (atomh);
		fwaiting_list_remove (& wlinks);
		atom_dec_and_unlock (atomh);
	}

	assert ("nikita-2186", ergo (ret, spin_jnode_is_not_locked (node)));
	return ret;
}

/* Perform the necessary work to prepare for fusing two atoms, which involves aquiring two
 * atom locks in the proper order.  If one of the node's atom is blocking fusion (i.e., it
 * is in the CAPTURE_WAIT stage) and the handle's atom is not then the handle's request is
 * put to sleep.  If the node's atom is committing, then the node can be copy-on-captured.
 * Otherwise, pick the atom with fewer pointers to be fused into the atom with more
 * pointer and call capture_fuse_into.
 */
/* Audited by: umka (2002.06.13) */
static int
capture_init_fusion (jnode       *node,
		     txn_handle  *txnh,
		     txn_capture  mode)
{
	txn_atom  *atomf;
	txn_atom  *atomh;

	assert("umka-216", txnh != NULL);
	assert("umka-217", node != NULL);
	
	atomh = txnh->atom;
	atomf = node->atom;

	/* Have to perform two trylocks here. */
	if (! spin_trylock_atom (atomf)) {
		goto noatomf_out;
	}

	if (! spin_trylock_atom (atomh)) {
		/* Release locks and try again. */
		spin_unlock_atom  (atomf);
	noatomf_out:
		spin_unlock_jnode (node);
		spin_unlock_txnh  (txnh);
		return -EAGAIN;
	}

	/* The txnh atom must still be open (since the txnh is active)...  the node atom may
	 * be in some later stage (checked next). */
	assert ("jmacd-20", atom_isopen (atomh));

	/* If the node atom is in the FUSE_WAIT state then we should wait, except to
	 * avoid deadlock we still must fuse if the txnh atom is also in FUSE_WAIT. */
	if (atomf->stage == ASTAGE_CAPTURE_WAIT && atomh->stage != ASTAGE_CAPTURE_WAIT) {

		/* This unlocks all four locks and returns EAGAIN. */
		return capture_fuse_wait (node, txnh, atomf, atomh, mode);
		
	} else if (atomf->stage > ASTAGE_CAPTURE_WAIT) {

		/* The block is involved with a comitting atom. */
		if (CAPTURE_TYPE (mode) == TXN_CAPTURE_READ_ATOMIC) {
			/* A read request for a committing block can be satisfied w/o
			 * COPY-ON-CAPTURE.  Success holds onto the jnode & txnh
			 * locks. */
			spin_unlock_atom (atomf);
			spin_unlock_atom (atomh);
			return 0;
		} else {
			/* Perform COPY-ON-CAPTURE.  Copy and try again.  This function
			 * releases all four locks. */
			return capture_copy (node, txnh, atomf, atomh, mode);
		}
	} 

	/* Because atomf's stage <= CAPTURE_WAIT */
	assert ("jmacd-175", atom_isopen (atomf));

	/* If we got here its either because the atomh is in CAPTURE_WAIT or because the
	 * atomf is not in CAPTURE_WAIT. */
	assert ("jmacd-176", (atomh->stage == ASTAGE_CAPTURE_WAIT ||
			      atomf->stage != ASTAGE_CAPTURE_WAIT));

	/* Now release the txnh lock: only holding the atoms at this point. */
	spin_unlock_txnh  (txnh);
	spin_unlock_jnode (node);

	/* Decide which should be kept and which should be merged. */
	if (atom_pointer_count (atomf) < atom_pointer_count (atomh)) {
		capture_fuse_into (atomf, atomh);
	} else {
		capture_fuse_into (atomh, atomf);
	}

	/* Atoms are unlocked in capture_fuse_into.  No locks held. */
	return -EAGAIN;
}

/* This function splices together two jnode lists (small and large) and sets all jnodes in
 * the small list to point to the large atom.  Returns the length of the list. */
/* Audited by: umka (2002.06.13) */
static int
capture_fuse_jnode_lists (txn_atom *large, capture_list_head *large_head, capture_list_head *small_head)
{
	int count = 0;
	jnode *node;

	assert("umka-218", large != NULL);	
	assert("umka-219", large_head != NULL);	
	assert("umka-220", small_head != NULL);	
	
	/* For every jnode on small's capture list... */
	for (node = capture_list_front (small_head);
	     /**/ ! capture_list_end   (small_head, node);
	     node = capture_list_next  (node)) {
		
		count += 1;
		
		/* With the jnode lock held, update atom pointer. */
		spin_lock_jnode (node);
		node->atom = large;
		spin_unlock_jnode (node);
	}
	
	/* Splice the lists. */
	capture_list_splice (large_head, small_head);

	return count;
}


/* This function splices together two txnh lists (small and large) and sets all txn handles in
 * the small list to point to the large atom.  Returns the length of the list. */
/* Audited by: umka (2002.06.13) */
static int
capture_fuse_txnh_lists (txn_atom *large, txnh_list_head *large_head, txnh_list_head *small_head)
{
	int count = 0;
	txn_handle *txnh;

	assert("umka-221", large != NULL);	
	assert("umka-222", large_head != NULL);	
	assert("umka-223", small_head != NULL);	
	
	/* Adjust every txnh to the new atom. */
	for (txnh = txnh_list_front (small_head);
	     /**/ ! txnh_list_end   (small_head, txnh);
	     txnh = txnh_list_next  (txnh)) {

		count += 1;

		/* With the txnh lock held, update atom pointer. */
		spin_lock_txnh (txnh);
		txnh->atom = large;
		spin_unlock_txnh (txnh);
	}

	/* Splice the txn_handle list. */
	txnh_list_splice (large_head, small_head);

	return count;
}

/* This function fuses two atoms.  The captured nodes and handles belonging to SMALL are
 * added to LARGE and their ->atom pointers are all updated.  The associated counts are
 * updated as well, and any waiting handles belonging to either are awakened.  Finally the
 * smaller atom's refcount is decremented.
 */
/* Audited by: umka (2002.06.13), umka (2002.06.15) */
static void
capture_fuse_into (txn_atom  *small,
		   txn_atom  *large)
{
	int         level;
	unsigned    zcount = 0;
	unsigned    tcount = 0;

	assert ("umka-224", small != NULL);
	assert ("umka-225", small != NULL);
	
	/* AUDIT: These atoms should be locked by caller in order to be safe modified. */
	assert ("umka-299", check_spin_is_locked(&large->alock));
	assert ("umka-300", check_spin_is_locked(&small->alock));
	
	assert ("jmacd-201", atom_isopen (small));
	assert ("jmacd-202", atom_isopen (large));

	trace_on (TRACE_TXN, "fuse atom %u into %u\n", small->atom_id, large->atom_id);

	/* Splice and update the per-level dirty jnode lists */
	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {
		zcount += capture_fuse_jnode_lists (large, & large->dirty_nodes[level], & small->dirty_nodes[level]);
	}

	/* Splice and update the [clean,dirty] jnode and txnh lists */
	zcount += capture_fuse_jnode_lists (large, & large->clean_nodes, & small->clean_nodes);
	tcount += capture_fuse_txnh_lists  (large, & large->txnh_list,    & small->txnh_list);

	/* Check our accounting. */
	assert ("jmacd-1063", zcount == small->capture_count + small->num_queued);
	assert ("jmacd-1065", tcount == small->txnh_count);

	/* splice flush queues */
	flush_fuse_queues (large, small);
	
	/* Transfer list counts to large. */
	large->txnh_count          += small->txnh_count;
	large->capture_count       += small->capture_count;

	/* Add all txnh references to large. */
	large->refcount += small->txnh_count;
	small->refcount -= small->txnh_count;

	/* Reset small counts */
	small->txnh_count          = 0;
	small->capture_count       = 0;
	
	/* Assign the oldest start_time, merge flags. */
	large->start_time = min (large->start_time, small->start_time);
	large->flags     |= small->flags;

	/* Merge blocknr sets. */
	blocknr_set_merge (& large->delete_set, & small->delete_set);
	blocknr_set_merge (& large->wandered_map, & small->wandered_map);

	/* Merge allocated/deleted file counts */
	large->nr_objects_deleted += small->nr_objects_deleted; 	
	large->nr_objects_created += small->nr_objects_created;

	small->nr_objects_deleted = 0;
	small->nr_objects_created = 0;

	/* Notify any waiters--small needs to unload its wait lists.  Waiters actually remove
	 * themselves from the list before returning from the fuse_wait function. */
	wakeup_atom_waitfor_list (small);
	wakeup_atom_waiting_list (small);

	if (large->stage < small->stage) {
		/* Large only needs to notify if it has changed state. */
		large->stage = small->stage;
		wakeup_atom_waitfor_list (large);
		wakeup_atom_waiting_list (large);
	}

	small->stage = ASTAGE_FUSED;

	/* Unlock atoms */
	spin_unlock_atom    (large);
	atom_dec_and_unlock (small);
}

/*****************************************************************************************
				   TXNMGR STUFF
*****************************************************************************************/

/* Perform copy-on-capture of a block.  INCOMPLETE CODE.
 */
/* Audited by: umka (2002.06.13) */
static int
capture_copy (jnode           *node,
	      txn_handle      *txnh,
	      txn_atom        *atomf,
	      txn_atom        *atomh,
	      txn_capture      mode)
{
	trace_on (TRACE_TXN, "capture_copy: fuse wait\n");
	
	return capture_fuse_wait (node, txnh, atomf, atomh, mode);
#if 0
	/* The txnh and its (possibly NULL) atom's locks are not needed at this
	 * point. */

	
	spin_unlock_txnh (txnh);

	if (atomh != NULL) {
		spin_unlock_atom (atomh);
	}

	uncapture_block (atomf, node);

	/* FIXME_JMACD What happens here?  Changes to: zstate?, buffer, data is copied -josh */

	/* EAGAIN implies all locks are released. */
	spin_unlock_atom  (atomf);
	ON_SMP (assert ("nikita-2187", spin_jnode_is_not_locked (node)));
#endif
	return -EIO;
}

/* Release a block from the atom, reversing the effects of being captured.
 * Currently this is only called when the atom commits. */
/* Audited by: umka (2002.06.13) */
static void
uncapture_block (txn_atom *atom,
		 jnode    *node)
{
	assert ("umka-226", node != NULL);
	assert ("umka-228", atom != NULL);
	
	assert ("jmacd-1021", node->atom == atom);
	assert ("jmacd-1022", spin_jnode_is_not_locked (node));
	assert ("jmacd-1023", spin_atom_is_locked (atom));
	assert ("nikita-2118", !jnode_check_dirty (node));

	/*trace_on (TRACE_TXN, "uncapture %p from atom %u (captured %u)\n", node, atom->atom_id, atom->capture_count);*/

	spin_lock_jnode (node);

	capture_list_remove_clean (node);
	atom->capture_count -= 1;
	node->atom = NULL;

	JF_CLR (node, ZNODE_RELOC);
	JF_CLR (node, ZNODE_WANDER);
	JF_CLR (node, ZNODE_CREATED);

	spin_unlock_jnode (node);

	/*trace_if (TRACE_FLUSH, print_page ("uncapture", node->pg));*/

	jput (node);

	ON_DEBUG (-- lock_counters() -> t_refs);
}

/** 
 * Unconditional insert of jnode into atom's clean list. Currently used in
 * bitmap-based allocator code for adding modified bitmap blocks the
 * transaction. @atom and @node are spin locked */
void txn_insert_into_clean_list (txn_atom * atom, jnode * node) 
{
	assert ("zam-538", spin_atom_is_locked (atom));
	assert ("zam-539", spin_jnode_is_locked (node));
	assert ("zam-540", !jnode_is_dirty (node));
	assert ("zam-543", node -> atom == NULL);

	capture_list_push_front (&atom->clean_nodes, node);
	jref (node);
	node->atom = atom;
	atom->capture_count ++;
}

/*****************************************************************************************
					DEBUG HELP
*****************************************************************************************/

/* Audited by: umka (2002.06.13) */
void
print_atom (const char *prefix, txn_atom *atom)
{
	jnode *scan;
	char list[32];
	int level;
	
	assert("umka-229", atom != NULL);

	info ("%s: refcount: %i id: %i flags: %x txnh_count: %i"
	      " capture_count: %i stage: %x start: %lu\n", prefix,
	      atom->refcount, atom->atom_id, atom->flags, atom->txnh_count,
	      atom->capture_count, atom->stage, atom->start_time);

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; level += 1) {

		sprintf (list, "capture level %d", level);

		for (scan = capture_list_front (& atom->dirty_nodes[level]);
		     /**/ ! capture_list_end   (& atom->dirty_nodes[level], scan);
		     scan = capture_list_next  (scan)) {

			info_jnode (list, scan);
			info ("\n");
		}
	}

	for (scan = capture_list_front (& atom->clean_nodes);
	     /**/ ! capture_list_end   (& atom->clean_nodes, scan);
	     scan = capture_list_next  (scan)) {
		
		info_jnode ("clean", scan);
		info ("\n");
	}
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
