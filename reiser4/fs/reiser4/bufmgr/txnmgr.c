/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* conceptual description of API goes here. Please write it so that I
   can read the rest of this file efficiently.  -Hans @@ new comment */

/* The txnmgr is a set of interfaces that keep track of atoms and transcrash handles.  The
 * txnmgr processes capture_block requests and manages the relationship between znodes
 * (buffer frames) and atoms through the various stages of a transcrash, and it also
 * oversees the fusion and capture-on-copy processes.  The main difficulty with this task
 * is maintaining a deadlock-free lock ordering between atoms and znodes/handles.  The
 * reason for the difficulty is that znodes, handles, and atoms contain pointer circles,
 * and the cycle must be broken.  The main requirement is that atom-fusion be deadlock
 * free, so once you hold the atom_lock you may then wait to aquire any znode or handle
 * lock.  This implies that any time you check the atom-pointer of a znode or handle and
 * then try to lock that atom, you must use trylock() and possibly reverse the order.
 * This involves releasing a lock, which introduces possible race conditions.  For this
 * reason, you will see calls to spin_try_reverse_lock(), which encapsulates this logic
 * but has no way to detect race conditions--see comments below. */

#include "bufmgr.h"

static void   txnmgr_write_cancel         (txn_atom   *atom,
					   znode      *frame);

static int    txnmgr_block_capture_copy   (znode      *frame,
					   txn_handle *handle,
					   txn_atom   *atomf,
					   txn_atom   *atomh);

static int    txnmgr_deallocate_preserve  (txn_atom   *atomf,
					   bm_blockid *blockid);

static int    txnmgr_deallocate_freespace (txn_atom   *atomf,
					   bm_blockid *blockid);

/*****************************************************************************************
				       TXNMGR_INIT
 *****************************************************************************************/

void
txnmgr_aunion_init (txn_aunion *aunion)
{
  aunion->_atom = NULL;
}

static void
txnmgr_handle_init (txn_handle *handle)
{
  txnmgr_aunion_init (& handle->_aunion);

  spin_lock_init (& handle->_handle_lock);

  wait_queue_head_init (& handle->_fuse_wait, & handle->_handle_lock);
}

static void
txnmgr_atom_init (txn_atom *atom)
{
  atom->_atom_id       = 0;
  atom->_capture_count = 0;
  atom->_active_count  = 0;
  atom->_stage         = ASTAGE_FREE;

  spin_lock_init       (& atom->_atom_lock);
  capture_list_init    (& atom->_capture_list);
  txn_handle_list_init (& atom->_active_handles);
  fwaitfor_list_init   (& atom->_fwaitfor_list);
  fwaiting_list_init   (& atom->_fwaiting_list);
}

int
txnmgr_atom_isclean (txn_atom *atom)
{
  return ((atom->_stage         == ASTAGE_FREE) &&
	  (atom->_active_count  == 0) &&
	  (atom->_capture_count == 0) &&
	  (atom->_atom_id       == 0) &&
	  capture_list_empty    (& atom->_capture_list) &&
	  txn_handle_list_empty (& atom->_active_handles) &&
	  fwaitfor_list_empty   (& atom->_fwaitfor_list) &&
	  fwaiting_list_empty   (& atom->_fwaiting_list));
}

static void
txnmgr_atom_lockorder (txn_atom *one, txn_atom *two, txn_atom **lock1, txn_atom **lock2)
{
  if (one < two)
    {
      (*lock1) = one;
      (*lock2) = two;
    }
  else
    {
      (*lock1) = two;
      (*lock2) = one;
    }
}

int
txnmgr_init (txn_mgr     *mgr,
	     u_int32_t    max_handles,
	     u_int32_t    max_atoms)
{
  int i;

  assert ("jmacd-11", max_handles >= max_atoms);

  mgr->_atom_idgen  = 1;    /* @@ _atom_idgen is recovered from the stable log. */
  mgr->_max_handles = max_handles;
  mgr->_max_atoms   = max_atoms;

  txn_handle_list_init (& mgr->_free_handles);

  txn_atom_list_init   (& mgr->_active_atoms);
  txn_atom_list_init   (& mgr->_free_atoms);

  spin_lock_init       (& mgr->_atoms_lock);
  spin_lock_init       (& mgr->_handles_lock);

  if (! (mgr->_atoms   = (txn_atom*)   KMALLOC (sizeof (txn_atom)   * mgr->_max_atoms)) ||
      ! (mgr->_handles = (txn_handle*) KMALLOC (sizeof (txn_handle) * mgr->_max_handles)))
    {
      /* @@ How concerned about cleanup are we?  The first could fail and the second could
       * succeed. */
      return -ENOMEM;
    }

  for (i = 0; i < mgr->_max_handles; i += 1)
    {
      txn_handle *handle = & mgr->_handles[i];

      txnmgr_handle_init (handle);

      txn_handle_list_push_back (& mgr->_free_handles, handle);
    }

  for (i = 0; i < mgr->_max_atoms; i += 1)
    {
      txn_atom *atom = & mgr->_atoms[i];

      txnmgr_atom_init (atom);

      txn_atom_list_push_back (& mgr->_free_atoms, atom);
    }

  return 0;
}

/*****************************************************************************************
				      TXNMGR_ATOM
 *****************************************************************************************/

/* Lock ordering in this method:
 *
 * ATOMS_LOCK
 */
static int
txnmgr_atom_begin_andlock (txn_atom **atomp, super_block *super)
{
  /* The txn_mgr._atoms_lock is held. */
  txn_atom *atom = NULL;

  if (! txn_atom_list_empty (& _the_mgr._txn_mgr._free_atoms))
    {
      atom = txn_atom_list_pop_front (& _the_mgr._txn_mgr._free_atoms);

      txn_atom_list_push_back (& _the_mgr._txn_mgr._active_atoms, atom);

      spin_lock (& atom->_atom_lock);

      assert ("jmacd-17", txnmgr_atom_isclean (atom));

      atom->_atom_id    = _the_mgr._txn_mgr._atom_idgen ++;
      atom->_super      = super;
      atom->_start_time = 0; /* @@ */

      (* atomp) = atom;

      return 0;
    }

  return -EAGAIN;
}

/* Lock ordering in this method:
 *
 * ATOMS_LOCK
 *  \
 *   \_ ATOM_LOCK
 */
static txn_atom*
txnmgr_atom_pick_locked (super_block *super)
{
  int ret;
  txn_atom *atom;

  spin_lock   (& _the_mgr._txn_mgr._atoms_lock);

  ret = txnmgr_atom_begin_andlock (& atom, super);

  if (ret != 0)
    {
      /* No free atoms, take the most recently started, non-closing atom. */
      for (atom = txn_atom_list_back (& _the_mgr._txn_mgr._active_atoms);
    	        ! txn_atom_list_end  (& _the_mgr._txn_mgr._active_atoms, atom);
	   atom = txn_atom_list_prev (atom))
	{
	  /* This will prevent the atom from closing immediately because the lock is held
	   * until the handle is assigned. */
	  spin_lock (& atom->_atom_lock);

	  /* Prevent picking closed atoms of those of a different file system. */
	  if (atom->_stage == ASTAGE_CAPTURE_FUSE && atom->_super == super)
	    {
	      /* Note: Break with atom_lock held! */
	      goto breakout;
	    }

	  spin_unlock (& atom->_atom_lock);
	}

      /* Failure */
      return NULL;
    }

 breakout:

  spin_unlock (& _the_mgr._txn_mgr._atoms_lock);

  return atom;
}

static __inline__ int
txnmgr_atom_pointer_count (txn_atom *atom)
{
  /* This is a measure of the amount of work needed to fuse this atom into another. */
  assert ("jmacd-28", txnmgr_atom_isopen (atom));

  return atom->_active_count + atom->_capture_count;
}

/* Lock ordering in this method:
 *
 * ATOMS_LOCK
 */
static void
txnmgr_atom_free (txn_atom *atom)
{
  /* The atom is not locked, we have already updated any heap pointers to it.  The stack
   * references are contained to this file and are always checked for race conditions
   * using atom-pointer equality. */
  atom->_atom_id       = 0;
  atom->_active_count  = 0;
  atom->_capture_count = 0;
  atom->_stage         = ASTAGE_FREE;

  assert ("jmacd-16",
	  capture_list_empty    (& atom->_capture_list) &&
	  txn_handle_list_empty (& atom->_active_handles) &&
	  ! spin_is_locked      (& atom->_atom_lock));

  spin_lock   (& _the_mgr._txn_mgr._atoms_lock);

  txn_atom_list_push_back (& _the_mgr._txn_mgr._free_atoms, atom);

  spin_unlock (& _the_mgr._txn_mgr._atoms_lock);
}

static int
txnmgr_atom_should_commit (txn_atom *atom)
{
  /* This will be determined by aging.  For now this says to commit ASAP.  The routine is
   * only called when the active_count drops to 0. */
  return 1;
}

static int
txnmgr_atom_begin_commit_locked (txn_atom *atom)
{
  znode *frame;
  znode *next_frame;

  assert ("jmacd-150", txnmgr_atom_isopen (atom));

  /* At this point the atom is locked for entering stage two.  Other atoms will detect
   * that this atom is busy and cannot be fused.  We have no open handles. */
  atom->_stage = ASTAGE_PRE_COMMIT;

  for (frame = capture_list_front (& atom->_capture_list);
             ! capture_list_end   (& atom->_capture_list, frame);
       frame = next_frame)
    {
      spin_lock (& frame->_frame_lock);

      bufmgr_frame_can_commit (frame);

      /* see if any modified bits are set */
      if (ZF_MASK (frame, ZFLAG_MODIFIED) == ZFLAG_ZERO)
	{
	  /* the block is considered "unmodified" and can be released right away. */
	  assert ("jmacd-511", (ZF_ISSET (frame, ZFLAG_INHASH) &&
				ZF_MASK  (frame, ZFLAG_DIRTY | ZFLAG_WRITEOUT) == ZFLAG_ZERO));
	}
      else
	{
	  /* the block has been allocated, relocated, or wandered.  it is not deleted. */

	  /* if the block was relocated and not allocated in this atom... */
	  if (ZF_ISSET (frame, ZFLAG_RELOC))
	    {
	      /* add its pre-existing location to the deallocate set. */
	      txnmgr_deallocate_preserve (atom, & frame->_blockid);
	    }

	  /* test its pending state */
	  if (ZF_ISSET (frame, ZFLAG_DIRTY))
	    {
	      /* if the block is dirty, it must be flushed, and therefore it cannot be
	       * released (until the write-complete callback). */
	      ZF_SET (frame, ZFLAG_WRITEOUT);
	      ZF_CLR (frame, ZFLAG_DIRTY);

	      iosched_write (frame, & frame->_relocid);
	    }

	  /* map this block to the relocate position in the buffer cache. */
	  bufmgr_blkremap (frame, & frame->_relocid);
	}

      /* release znode from CAPTURE list */
      bufmgr_blkput_locked (frame);

      next_frame = capture_list_remove_get_next (frame);

      spin_unlock (& frame->_frame_lock);
    }

  /* @@@ HERE: format commit record */

  return 0;
}

int
txnmgr_block_delete (txn_handle  *handle,
		     znode       *frame)
{
  txn_atom *atom = /* @@@ HERE: not sure about the call to delete... */ NULL;
  int check;

  assert ("jmacd-750", atom != NULL);
  assert ("jmacd-751", spin_is_locked (& frame->_frame_lock));
  assert ("jmacd-752", spin_is_locked (& atom->_atom_lock));
  assert ("jmacd-753", atomic_read (& frame->_refcount) == 1);

  /* dirty status doesn't matter */
  if (ZF_ISSET (frame, ZFLAG_WRITEOUT))
    {
      /* but cancel any outstanding write */
      txnmgr_write_cancel (atom, frame);
    }

  /* if the block was not allocated in this atom... */
  if (! ZF_ISSET (frame, ZFLAG_ALLOC))
    {
      /* add its pre-existing location to the deallocate set. */
      txnmgr_deallocate_preserve (atom, & frame->_blockid);
    }

  /* if the reloc bit is already set... */
  if (ZF_ISSET (frame, ZFLAG_RELOC))
    {
      /* it means the block was chosen for early flushing to a relocate position, then
       * deleted.  release the relocated position, which is again freespace */
      txnmgr_deallocate_freespace (atom, & frame->_relocid);
    }

  /* release this frame/buffer */
  capture_list_remove (frame);

  check = atomic_dec_and_test (& frame->_refcount);

  assert ("jmacd-553", check);

  bufmgr_putbuf (frame->_buffer, 0);

  znode_return (frame);

  return 0;
}

int
txnmgr_block_create (txn_handle  *handle,
		     bm_blkref   *blkref)
{
  int ret;
  bm_blkref cref;

  /* Blkcreate does not return the lock, but the block is private. */
  if ((ret = bufmgr_blkcreate (handle->_super, blkref)))
    {
      return ret;
    }

  /* Capture the block */
  if ((ret = txnmgr_block_capture (handle, & blkref->_frame->_blockid, TXN_CAPTURE_WRITE, & cref)))
    {
      return ret;
    }

  assert ("jmacd-412", blkref->_frame == cref._frame);

  return 0;
}

/*****************************************************************************************
				      TXNMGR_HANDLE
 *****************************************************************************************/

int
txnmgr_begin_handle (super_block     *super,
		     txn_handle     **handlep)
{
  int ret = -EBUSY;
  txn_handle *handle = NULL;

  spin_lock   (& _the_mgr._txn_mgr._handles_lock);

  if (! txn_handle_list_empty (& _the_mgr._txn_mgr._free_handles))
    {
      handle = txn_handle_list_pop_front (& _the_mgr._txn_mgr._free_handles);

      /* Note: the handle should be added to the active_handles list of the atom when
       * assigned.  This means there is a period of time when the handle is unaccounted
       * for.  However, it doesn't hold any resources except the handle itself. */
      ret = 0;
    }

  handle->_super = super;

  spin_unlock (& _the_mgr._txn_mgr._handles_lock);

  (* handlep) = handle;

  return ret;
}

/* Lock ordering in this method:
 *
 * UNION_LOCKS
 *  \
 *   \_ ATOM_LOCK (try_reverse_race)
 */
int
txnmgr_commit_handle (txn_handle *handle)
{
  int ret = 0;
  txn_atom *atom;

  atom = txnmgr_getatom_locked (& handle->_aunion, & handle->_handle_lock);

  assert ("jmacd-21", txnmgr_atom_isopen (atom));

  handle->_aunion._atom = NULL;

  spin_lock   (& _the_mgr._txn_mgr._handles_lock);
  txn_handle_list_push_back (& _the_mgr._txn_mgr._free_handles, handle);
  spin_unlock (& _the_mgr._txn_mgr._handles_lock);
  spin_unlock (& handle->_handle_lock);

  /* Only the atom is still locked. */
  atom->_active_count -= 1;

  if ((atom->_active_count == 0) && txnmgr_atom_should_commit (atom))
    {
      /* How will we handle failures of this function? */
      ret = txnmgr_atom_begin_commit_locked (atom);
    }

  spin_unlock (& atom->_atom_lock);

  return ret;
}

/*****************************************************************************************
				   TXNMGR_BLOCK_CAPTURE
 *****************************************************************************************/

static void
txnmgr_block_capture_wakeup_waitfor_list (txn_atom *atom)
{
  txn_handle *handle;

  for (handle = fwaitfor_list_front (& atom->_fwaitfor_list);
              ! fwaitfor_list_end   (& atom->_fwaitfor_list, handle);
       handle = fwaitfor_list_next  (handle))
    {
      wait_queue_broadcast (& handle->_fuse_wait);
    }
}

static void
txnmgr_block_capture_wakeup_waiting_list (txn_atom *atom)
{
  txn_handle *handle;

  for (handle = fwaiting_list_front (& atom->_fwaiting_list);
              ! fwaiting_list_end   (& atom->_fwaiting_list, handle);
       handle = fwaiting_list_next  (handle))
    {
      wait_queue_broadcast (& handle->_fuse_wait);
    }
}

/* Lock ordering in this method:
 *
 * BOTH_AUNION_LOCKS, BOTH_ATOM_LOCKS (atom locks are released, aunion locks are released,
 * atom locks reaquired in series) */
static int
txnmgr_block_capture_fuse_wait (znode *frame, txn_handle *handle, txn_atom *atomf, txn_atom *atomh)
{
  /* The general purpose of this function is to wait on the first of two possible events.
   * The situation is that handle (atomh) is blocked trying to capture a frame belonging
   * to atomf which is in the CAPTURE_WAIT state.  atomh is not in the CAPTURE_WAIT state.
   * However, atomh could fuse with another atom or somehow enter the CAPTURE_WAIT state
   * itself, at which point it needs to unblock the handle to avoid deadlock.  When the
   * handle is unblocked it will proceed and fuse the two atoms in the CAPTURE_WAIT
   * state. */

  /* This is also called by txnmgr_block_capture_assign_handle with (atomh == NULL) to
   * wait for atomf to close but it is not assigned to an atom of its own. */

  handle->_fwaitfor_atom = atomf;
  handle->_fwaiting_atom = atomh;

  /* We do not need the frame aunion lock. */
  spin_unlock (& frame->_frame_lock);

  /* Add to waitfor list, unlock atomf. */
  fwaitfor_list_push_back (& atomf->_fwaitfor_list, handle);
  spin_unlock (& atomf->_atom_lock);

  if (atomh)
    {
      /* Add to waiting list, unlock atomh. */
      fwaiting_list_push_back (& atomh->_fwaiting_list, handle);
      spin_unlock (& atomh->_atom_lock);
    }

  /* Go to sleep, releasing the handle->_aunion lock. */
  wait_queue_sleep (& handle->_fuse_wait, 0);

  /* Remove from the waitfor list. */
  spin_lock (& atomf->_atom_lock);
  assert ("jmacd-206", handle->_fwaitfor_atom == atomf);
  fwaitfor_list_remove (handle);
  spin_unlock (& atomf->_atom_lock);

  if (atomh)
    {
      /* Remove from the waiting list. */
      spin_lock (& atomh->_atom_lock);
      assert ("jmacd-207", handle->_fwaiting_atom == atomh);
      fwaiting_list_remove (handle);
      spin_unlock (& atomh->_atom_lock);
    }

  return -EAGAIN;
}

/* No-locking version of assign_block.
 */
static void
txnmgr_block_capture_assign_block_nolock (txn_atom *atom,
					  znode    *frame)
{
  assert ("jmacd-321", spin_is_locked (& frame->_frame_lock));
  assert ("jmacd-322", ! ZF_ISSET (frame, ZFLAG_CAPTIVE));
  assert ("jmacd-323", frame->_aunion._atom == NULL);

  frame->_aunion._atom = atom;
  capture_list_push_back (& atom->_capture_list, frame);
  atom->_capture_count += 1;
  atomic_inc (& frame->_refcount);

  ZF_SET (frame, ZFLAG_CAPTIVE);
}

/* No-locking version of assign_handle.
 */
static void
txnmgr_block_capture_assign_handle_nolock (txn_atom   *atom,
					   txn_handle *handle)
{
  assert ("jmacd-822", (spin_is_locked (& handle->_handle_lock) &&
			handle->_aunion._atom == NULL));

  handle->_aunion._atom = atom;
  txn_handle_list_push_back (& atom->_active_handles, handle);
  atom->_active_count += 1;
}

/* Lock ordering in this method:
 *
 * BOTH_UNION_LOCKS
 *  \
 *   \_ ATOM_LOCK (try_reverse_race)
 */
static int
txnmgr_block_capture_assign_block (txn_handle *handle,
				   znode      *frame)
{
  /* Note: Similar to assign_handle */
  int ret;
  txn_aunion *handu = & handle->_aunion;
  txn_atom    *atom = handu->_atom;

  /* This may require a lock upgrade.  The frame is not linked yet, so its lock doesn't
   * matter, but the handle may be part of a joining atom, so we have to trylock and check
   * for a race condition. */
  if ((spin_lock_reverse_race (& atom->_atom_lock, & handle->_handle_lock)) &&
      (atom != handu->_atom))
    {
      /* The race condition here is that the atom might be fused by another process.  If
       * another process does fuse this atom it must update handu->_atom.  The atom cannot
       * close during this period because the handle is still active.  */
      spin_unlock (& handle->_handle_lock);
      spin_unlock (& frame->_frame_lock);
      ret = -EAGAIN;
    }
  else
    {
      assert ("jmacd-19", txnmgr_atom_isopen (atom));

      /* Add page to capture list. */
      txnmgr_block_capture_assign_block_nolock (atom, frame);
      ret = 0;
    }

  /* Unlock the atom */
  spin_unlock (& atom->_atom_lock);
  return ret;
}

/* Lock ordering in this method:
 *
 * BOTH_UNION_LOCKS
 *  \
 *   \_ ATOM_LOCK (try_reverse_race)
 */
static int
txnmgr_block_capture_assign_handle (znode       *frame,
				    txn_handle  *handle,
				    txn_capture  mode)
{
  int ret;
  txn_aunion *frameu = & frame->_aunion;
  txn_atom     *atom = frameu->_atom;

  assert ("jmacd-510", mode != TXN_CAPTURE_READ_NONCOM);

  /* This may require a lock upgrade.  The handle is not linked yet, so its lock doesn't
   * matter, but the frame may be part of a fusing atom, so we have to trylock and check
   * for a race condition.  The "race condition" is detected by checking for an atom
   * change on the frame. */
  if ((spin_lock_reverse_race (& atom->_atom_lock, & frame->_frame_lock)) &&
      (atom != frameu->_atom))
    {
      /* This race is like the two before (with remarkably similar comments). */
      spin_unlock (& handle->_handle_lock);
      spin_unlock (& frame->_frame_lock);
      ret = -EAGAIN;
    }
  else if (atom->_stage == ASTAGE_CAPTURE_WAIT)
    {
      /* In addition to the possible race, the atom could be blocking requests--this is
       * the first chance we've had to test it.  Since this handle is not yet assigned,
       * the fuse_wait logic is not to avoid deadlock, its just waiting. */
      return txnmgr_block_capture_fuse_wait (frame, handle, atom, NULL);
    }
  else if (atom->_stage > ASTAGE_CAPTURE_WAIT)
    {
      /* The block is involved with a committing atom. */
      if (mode == TXN_CAPTURE_READ_ATOMIC)
	{
	  /* A read request for a committing block can be satisfied w/o COPY-ON-CAPTURE. */
	  ret = 0;
	}
      else
	{
	  /* Perform COPY-ON-CAPTURE.  Copy and try again.  This function releases all three
	   * locks. */
	  return txnmgr_block_capture_copy (frame, handle, atom, NULL);
	}
    }
  else
    {
      /* Add handle to active list.  In this successful case, locks are still held. */
      assert ("jmacd-160", atom->_stage == ASTAGE_CAPTURE_FUSE);

      txnmgr_block_capture_assign_handle_nolock (atom, handle);
      ret = 0;
    }

  /* Unlock the atom */
  spin_unlock (& atom->_atom_lock);
  return ret;
}

/* Lock ordering in this method:
 *
 * BOTH_ATOM_LOCKS
 *  \
 *   \_ FRAME/HANDLE AUNION LOCKS
 */
static void
txnmgr_block_capture_fuse_into (txn_atom  *small,
				txn_atom  *large)
{
  znode      *frame;
  txn_handle *handle;

  assert ("jmacd-201", txnmgr_atom_isopen (small));
  assert ("jmacd-202", txnmgr_atom_isopen (large));

  for (frame = capture_list_front (& small->_capture_list);
             ! capture_list_end   (& small->_capture_list, frame);
       frame = capture_list_next  (frame))
    {
      spin_lock   (& frame->_frame_lock);
      frame->_aunion._atom = large;
      spin_unlock (& frame->_frame_lock);
    }

  for (handle = txn_handle_list_front (& small->_active_handles);
              ! txn_handle_list_end   (& small->_active_handles, handle);
       handle = txn_handle_list_next  (handle))
    {
      spin_lock   (& handle->_handle_lock);
      handle->_aunion._atom = large;
      spin_unlock (& handle->_handle_lock);
    }

  /* Splice the two aunion lists. */
  capture_list_splice    (& large->_capture_list,   & small->_capture_list);
  txn_handle_list_splice (& large->_active_handles, & small->_active_handles);

  /* Assign the oldest start_time. */
  large->_start_time = min (large->_start_time, small->_start_time);

  /* Notify any waiters--small needs to unload its wait lists.  Waiters actually remove
   * themselves from the list before returning from the fuse_wait function. */
  txnmgr_block_capture_wakeup_waitfor_list (small);
  txnmgr_block_capture_wakeup_waiting_list (small);

  if (large->_stage < small->_stage)
    {
      /* Large only needs to notify if it has changed state. */
      large->_stage = small->_stage;
      txnmgr_block_capture_wakeup_waitfor_list (large);
      txnmgr_block_capture_wakeup_waiting_list (large);
    }

  /* Unlock atoms, free small atom */
  spin_unlock (& large->_atom_lock);
  spin_unlock (& small->_atom_lock);

  txnmgr_atom_free (small);
}

/* Lock ordering in this method:
 *
 * BOTH_UNION_LOCKS
 *   /
 *  /
 * BOTH_ATOM_LOCKS (ordered)
 *  \
 *   \_ BOTH_UNION_LOCKS
 *   /
 *  /
 * BOTH_ATOM_LOCKS
 *  \
 *   \_ LOOP OVER HANDLE AND FRAME AUNION LOCKS
 */
static int
txnmgr_block_capture_init_fusion (znode       *frame,
				  txn_handle  *handle,
				  txn_capture  mode)
{
  txn_aunion  *unf = & frame->_aunion;
  txn_aunion  *unh = & handle->_aunion;
  txn_atom  *atomf = unf->_atom;
  txn_atom  *atomh = unh->_atom;
  txn_atom  *lock1;
  txn_atom  *lock2;

  /* Note: Scary lock ordering issues ahead! */
  spin_unlock (& frame->_frame_lock);
  spin_unlock (& handle->_handle_lock);

  /* Need to obtain two atom locks, so determine an ordering function: */
  txnmgr_atom_lockorder (atomf, atomh, & lock1, & lock2);

  /* Aquire locks in proper order. */
  spin_lock (& lock1->_atom_lock);
  spin_lock (& lock2->_atom_lock);

  /* Reaquire both union locks: have to check for races. */
  spin_lock (& frame->_frame_lock);
  spin_lock (& handle->_handle_lock);

  if ((atomf != unf->_atom) || (atomh != unh->_atom))
    {
      /* The race condition is that another process might fuse either of these atoms in a
       * race. */
      spin_unlock (& lock1->_atom_lock);
      spin_unlock (& lock2->_atom_lock);
      spin_unlock (& frame->_frame_lock);
      spin_unlock (& handle->_handle_lock);
      return -EAGAIN;
    }

  /* In addition to the possible race, it is also possible that the frame atom is closed,
   * but not the handle atom (since the handle is active).  */
  assert ("jmacd-20", txnmgr_atom_isopen (atomh));

  /* If the frame atom is in the FUSE_WAIT state then we should wait, except to avoid
   * deadlock we still must fuse if the handle atom is also in FUSE_WAIT. */
  if (atomf->_stage == ASTAGE_CAPTURE_WAIT && atomh->_stage != ASTAGE_CAPTURE_WAIT)
    {
      /* This unlocks both atoms and both aunions. */
      return txnmgr_block_capture_fuse_wait (frame, handle, atomf, atomh);
    }
  else if (atomf->_stage > ASTAGE_CAPTURE_WAIT)
    {
      /* The block is involved with a comitting atom. */
      if (mode == TXN_CAPTURE_READ_ATOMIC)
	{
	  /* A read request for a committing block can be satisfied w/o COPY-ON-CAPTURE. */
	  spin_unlock (& lock1->_atom_lock);
	  spin_unlock (& lock2->_atom_lock);
	  return 0;
	}
      else
	{
	  /* Perform COPY-ON-CAPTURE.  Copy and try again.  This function releases all four
	   * locks. */
	  return txnmgr_block_capture_copy (frame, handle, atomf, atomh);
	}
    }
  else
    {
      assert ("jmacd-175", txnmgr_atom_isopen (atomf));
      assert ("jmacd-176", (atomh->_stage == ASTAGE_CAPTURE_WAIT ||
			    atomf->_stage != ASTAGE_CAPTURE_WAIT));
    }

  /* Now release the handle lock: only holding the atoms at this point. */
  spin_unlock (& handle->_handle_lock);
  spin_unlock (& frame->_frame_lock);

  /* Decide which should be kept and which should be merged. */
  if (txnmgr_atom_pointer_count (atomf) < txnmgr_atom_pointer_count (atomh))
    {
      txnmgr_block_capture_fuse_into (atomf, atomh);
    }
  else
    {
      txnmgr_block_capture_fuse_into (atomh, atomf);
    }

  /* Atoms are unlocked in capture_fuse_into. */
  return -EAGAIN;
}

/* Lock ordering in this method:
 *
 * FRAME_AUNION_LOCK
 *  \
 *   \_ HANDLE_AUNION_LOCK
 *    \
 *     \_ ATOM_LOCK (only if neither is assigned, can't deadlock)
 */
static int
txnmgr_block_try_capture (txn_handle       *handle,
			  bm_blockid const *blockid,
			  txn_capture       mode,
			  bm_blkref        *blkref)
{
  int ret;
  txn_atom *block_atom, *handle_atom;
  znode *frame;

  /* This gets the frame with frame_lock held. */
  if ((ret = bufmgr_blkget (blockid, blkref)) != 0)
    {
      return ret;
    }

  frame = blkref->_frame;

  /* Get second (handle) spinlock, this allows us to compare txn_atom pointers but it
   * doesn't let us touch the atoms themselves. */
  spin_lock (& handle->_handle_lock);

  block_atom  = frame->_aunion._atom;
  handle_atom = handle->_aunion._atom;

  if (block_atom != NULL)
    {
      /* The block has already been exclusively assigned to an atom. */

      if (block_atom == handle_atom || mode == TXN_CAPTURE_READ_NONCOM)
	{
	  /* No extra capturing work required. */
	}
      else if (handle_atom == NULL)
	{
	  /* The handle is unassigned, try to assign it.
	   */
	  if ((ret = txnmgr_block_capture_assign_handle (frame, handle, mode)) != 0)
	    {
	      /* EAGAIN or otherwise */
	      return ret;
	    }

	  /* Either the handle is now assigned to the block's atom or the read-request was
	   * granted because the block is committing.  Locks still held. */
	}
      else
	{
	  /* In this case, both handle and frame belong to different atoms.  This function
	   * returns -EAGAIN on successful fusion, 0 on the fall-through case. */
	  if ((ret = txnmgr_block_capture_init_fusion (frame, handle, mode)) != 0)
	    {
	      return ret;
	    }

	  /* The fall-through case is read request for committing block.  Locks still
	   * held. */
	}
    }
  else if (mode == TXN_CAPTURE_WRITE || mode == TXN_CAPTURE_READ_MODIFY)
    {
      /* The page is unlocked and the handle wishes exclusive access. */
      if (handle_atom != NULL)
	{
	  /* The handle is already assigned: add the page to its atom. */
	  if ((ret = txnmgr_block_capture_assign_block (handle, frame)) != 0)
	    {
	      /* EAGAIN or otherwise */
	      return ret;
	    }

	  /* Locks are still held, success. */
	}
      else
	{
	  /* Neither handle nor page are assigned to an atom. */
	  block_atom = txnmgr_atom_pick_locked (handle->_super);

	  if (block_atom != NULL)
	    {
	      /* Assign both, release atom lock. */
	      assert ("jmacd-18", block_atom->_stage == ASTAGE_CAPTURE_FUSE);

	      txnmgr_block_capture_assign_handle_nolock (block_atom, handle);
	      txnmgr_block_capture_assign_block_nolock  (block_atom, frame);

	      spin_unlock (& block_atom->_atom_lock);
	    }
	  else
	    {
	      /* Release locks and fail */
	      spin_unlock (& frame->_frame_lock);
	      spin_unlock (& handle->_handle_lock);
	      return -ENOMEM;
	    }

	  /* Locks are still held, success. */
	}
    }
  else
    {
      assert ("jmacd-411", mode == TXN_CAPTURE_READ_NONCOM ||
	                   mode == TXN_CAPTURE_READ_ATOMIC);
    }

  spin_unlock (& handle->_handle_lock);

  /* @@@ HERE: Before releasing the frame lock, aquire the long-term lock on this block.
   * Have to make sure the lock queue ordering doesn't hurt us.  Zam gets involved
   * here. */

  spin_unlock (& frame->_frame_lock);

  return 0;
}

/* This function calls txnmgr_block_try_capture repeatedly as long as -EAGAIN is
 * returned. */
int
txnmgr_block_capture (txn_handle       *handle,
		      bm_blockid const *blockid,
		      txn_capture       mode,
		      bm_blkref        *blkref)
{
  int ret;

  if (handle->_super != blockid->_super)
    {
      warning ("jmacd-55", "txnmgr_block_capture: super blocks do not match");
      return -EINVAL;
    }

  do
    {
      blkref->_frame = NULL;

      /* Repeat try_capture as long as -EAGAIN is returned. */
      if ((ret = txnmgr_block_try_capture (handle, blockid, mode, blkref)) != 0)
	{
	  if (blkref->_frame != NULL)
	    {
	      bufmgr_blkput (blkref->_frame);
	    }
	}
    }
  while (ret == -EAGAIN);

  return ret;
}

/*****************************************************************************************
				   TXNMGR STUFF ???
 *****************************************************************************************/

static void
txnmgr_block_relocate (txn_handle  *handle,
		       znode       *frame)
{
  /* Allocate a relocate block if it has not already been done.  In reality, this should
   * include some "optimality" concern. */
  if (! ZF_ISSET (frame, ZFLAG_RELOC))
    {
      ZF_SET (frame, ZFLAG_RELOC);

      freemap_allocate (handle, & frame->_relocid);
    }
}

void
txnmgr_atom_commit_write_complete (znode *frame)
{
  /* The write I/O completion. */
  spin_lock (& frame->_frame_lock);

  assert ("jmacd-312", ZF_ISSET (frame, ZFLAG_WRITEOUT));

  ZF_CLR (frame, ZFLAG_WRITEOUT);

  /* Release blk reference */
  bufmgr_blkput_locked (frame);

  spin_unlock (& frame->_frame_lock);
}

/* Lock ordering in this method:
 *
 * FRAME_AUNION, HANDLE_AUNION, ATOM_LOCK
 *
 */
int
txnmgr_block_capture_copy (znode        *frame,
			   txn_handle   *handle,
			   txn_atom     *atomf,
			   txn_atom     *atomh)
{
  int ret;
  znode *copy;

  /* The handle and atom locks are not needed at this point. */
  spin_unlock (& handle->_handle_lock);
  spin_unlock (& atomf->_atom_lock);

  if (atomh != NULL)
    {
      spin_unlock (& atomh->_atom_lock);
    }

  /* blkcopy releases frame_aunion_lock */
  ret = bufmgr_blkcopy (frame, & copy);

  if (ret != 0)
    {
      return ret;
    }

  return -EAGAIN;
}
