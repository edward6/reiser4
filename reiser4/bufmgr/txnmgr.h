/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#ifndef __REISER4_TXNMGR_H__
#define __REISER4_TXNMGR_H__



/*****************************************************************************************
				     MACROS AND STUFF
 *****************************************************************************************/

#ifndef TXN_MGR_MAX_HANDLES
#define TXN_MGR_MAX_HANDLES 50
#endif

#ifndef TXN_MGR_MAX_ATOMS
#define TXN_MGR_MAX_ATOMS 20
#endif

/****************************************************************************************
				    TYPE DECLARATIONS
 ****************************************************************************************/

typedef struct _txn_aunion        txn_aunion;
typedef struct _txn_mgr           txn_mgr;

TS_LIST_DECLARE(capture);    /* The transaction's list of captured pages */
TS_LIST_DECLARE(txn_atom);   /* The manager's list of active/free transactions */
TS_LIST_DECLARE(txn_handle); /* The atom/manager's list of active/free handles */

TS_LIST_DECLARE(fwaitfor);   /* Each atom has one of these lists: one for its own handles */
TS_LIST_DECLARE(fwaiting);   /* waiting on another atom and one for reverse mapping.  Used
			      * to prevent deadlock in the ASTAGE_CAPTURE_WAIT state. */

typedef enum
{
  TXN_CAPTURE_READ_ATOMIC   = (1 << 0),
  TXN_CAPTURE_READ_NONCOM   = (1 << 1),
  TXN_CAPTURE_READ_MODIFY   = (1 << 2),
  TXN_CAPTURE_WRITE         = (1 << 3),
} txn_capture;

typedef enum
{
  ASTAGE_FREE            = 0,
  ASTAGE_CAPTURE_FUSE    = (1 << 0), /* Normal aquisition stage. */
  ASTAGE_CAPTURE_WAIT    = (1 << 1), /* Desperate measures! */
  ASTAGE_PRE_COMMIT      = (1 << 2), /* Waiting for I/O before commit.  Copy-on-capture. */
  ASTAGE_POST_COMMIT     = (1 << 3), /* Post-commit overwrite I/O.  Steal-on-capture. */
} txn_stage;

/****************************************************************************************
				     TYPE DEFINITIONS
 ****************************************************************************************/

/* The union-ing record: this encapsulates a reference to txn_atom.  When atom fusion
 * occurs, all of the aunion records referring to the fusing-in atom will be updated, thus
 * forming the union of both atoms.
 */

struct _txn_aunion
{
  txn_atom              *_atom;
};

/* A note on lock ordering: the aunion _lock protects reading of the aunion _atom field,
 * so typically an operation on the atom through an aunion must (1) lock the aunion, (2)
 * read the atom pointer, (3) lock the atom.
 *
 * During atom fusion, the process holds locks on both atoms at once.  Then, it iterates
 * through the list of handles and pages held by the smaller of the two atoms.  For each
 * handle and page referencing the smaller atom, the fusing process must: (1) lock the
 * aunion, and (2) update the atom pointer.
 *
 * You can see that there is a conflict of lock ordering here, so the more-complex
 * procedure should have priority, i.e., the fusing process has priority so that it is
 * guaranteed to make progress and to avoid restarts.
 *
 * This decision, however, means additional complexity for aquiring the atom lock in the
 * first place.  The general procedure followed in the code is:
 *
 * TXN_AUNION *aunion = ...;
 * TXN_ATOM   *atom;
 *
 * spin_lock (& aunion->_lock);
 *
 * atom = aunion->_atom;
 *
 * if (spin_lock_reverse_race (& atom->_lock, & aunion->_lock)) && (atom != aunion->_atom))
 *   {
 *     RESTART OPERATION, THERE WAS A RACE;
 *   }
 *
 * ELSE YOU HAVE BOTH ATOM AND AUNION LOCKED
 *
 * See the txnmgr_getatom_locked() method below for a common case.
 *
 * For a comment regarding the SPIN_LOCK_REVERSE_RACE function, see its definition in
 * bufmgr.h.  */

/* An atomic transaction: this is the underlying system representation
 * of a transaction, not the one seen by clients. */
struct _txn_atom
{
  spinlock_t             _atom_lock;       /* Protects the structure. */

  u_int64_t              _atom_id;         /* The stable TXN identifier. */

  u_int32_t              _active_count;    /* Number of open handles. */
  txn_handle_list_head   _active_handles;  /* List of active handles. */

  u_int32_t              _capture_count;   /* The number of exclusive pages. */
  capture_list_head      _capture_list;    /* The transaction's exclusive pages list. */

  txn_atom_list_link     _txn_link;        /* Transaction list link. */

  txn_stage              _stage;           /* True if this atom is not accepting new
					    * handles; it has reached commit stage. */

  super_block           *_super;           /* An atom is bound to a single superblock. */

  unsigned long          _start_time;      /* Start time. */

  fwaitfor_list_head     _fwaitfor_list;   /* List of handles waiting FOR this atom. */
  fwaiting_list_head     _fwaiting_list;   /* List of this atom's handles that are waiting. */
};

/* A transaction handle: the client obtains and commits this handle which is assigned by
 * the system to a txn_atom. */
struct _txn_handle
{
  spinlock_t             _handle_lock;

  txn_aunion             _aunion;          /* If assigned, the atom it is part of. */

  super_block           *_super;           /* A handle is bound to a single superblock. */

  txn_handle_list_link   _txn_link;        /* Transaction list link. */

  wait_queue_head_t      _fuse_wait;       /* The handle sleeps on its own wait_queue, can
					    * be woken by either atom. */

  fwaitfor_list_link     _fwaitfor_link;
  fwaiting_list_link     _fwaiting_link;

  txn_atom              *_fwaitfor_atom;   /* These are set to indicate membership in one of */
  txn_atom              *_fwaiting_atom;   /* these lists. */
};

/* The transaction manager: */
struct _txn_mgr
{
  u_int64_t              _atom_idgen;      /* The next atom_id to generate. */

  u_int32_t              _max_atoms;       /* Maximum number of atomic txns. */
  txn_atom              *_atoms;           /* Allocation of atoms. */
  txn_atom_list_head     _active_atoms;    /* List of active atoms. */
  txn_atom_list_head     _free_atoms;      /* List of free atoms. */
  spinlock_t             _atoms_lock;

  u_int32_t              _max_handles;     /* Maximum number of txn handles. */
  txn_handle            *_handles;         /* Allocation of handles. */
  txn_handle_list_head   _free_handles;    /* List of free handles. */
  spinlock_t             _handles_lock;
};

/****************************************************************************************
				  FUNCTION DECLARATIONS
 ****************************************************************************************/

extern int          txnmgr_init              (txn_mgr         *mgr,
					      u_int32_t        max_handles,
					      u_int32_t        max_atoms);
extern void         txnmgr_aunion_init       (txn_aunion      *aunion);

/* These two functions begin and commit the user-level transaction handle.  These do not
 * imply a begin/commit of an underlying txn_atom. */
extern int          txnmgr_begin_handle      (super_block     *super,
					      txn_handle     **handle);
extern int          txnmgr_commit_handle     (txn_handle      *handle);

/* The main txn interface: capture a page for exclusive access within the transaction,
 * possibly causing the fusion of two atoms. */
extern int          txnmgr_block_capture     (txn_handle       *handle,
					      bm_blockid const *blockid,
					      txn_capture       mode,
					      bm_blkref        *blkref);

/* The interface for creation and deletion of pages.  Creation returns an unallocated page
 * with negative block number.  @@ For the purposes of efficient extent packing in stage
 * one, these numbers should probably be ascending using a handle-specific or
 * extent-specific offset.  Maybe we should use a skip list to represent modified
 * extents. */
extern int          txnmgr_block_create      (txn_handle       *handle,
					      bm_blkref        *blkref);
extern int          txnmgr_block_delete      (txn_handle       *handle,
					      znode            *frame);

/* This is called when an I/O completes. */
extern void         txnmgr_write_complete    (znode   *frame);

static __inline__ int
txnmgr_atom_isopen (txn_atom *atom)
{
  return atom->_stage & (ASTAGE_CAPTURE_FUSE | ASTAGE_CAPTURE_WAIT);
}

/* This is a general routine for getting a locked atom belonging to an aunion.  Has to
 * aquire locks in reverse order.  */
static __inline__ txn_atom*
txnmgr_getatom_locked (txn_aunion *aunion, spinlock_t *lock)
{
  txn_atom *atom;

  spin_lock (lock);

 try_again:

  atom = aunion->_atom;

  assert ("jmacd-309", atom);

  if ((spin_lock_reverse_race (& atom->_atom_lock, lock)) &&
      (atom != aunion->_atom))
    {
      /* The race condition here is that the atom might be fused by another process.  If
       * another process does fuse this atom, then it must update aunion->_atom. */
      spin_unlock (& atom->_atom_lock);
      goto try_again;
    }

  return atom;
}

/****************************************************************************************
				    GENERIC STRUCTURES
 ****************************************************************************************/

TS_LIST_DEFINE(txn_atom,txn_atom,_txn_link);
TS_LIST_DEFINE(txn_handle,txn_handle,_txn_link);

TS_LIST_DEFINE(fwaitfor,txn_handle,_fwaitfor_link);
TS_LIST_DEFINE(fwaiting,txn_handle,_fwaiting_link);


# endif /* __REISER4_TXNMGR_H__ */
