/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* can you write here a conceptual description of the API: what it
   offers, why, what the functions do for you? -Hans @@ new comment */

/* The bufmgr is a set of interfaces that maintain the basic buffer cache.  The principle
 * function of the buffer manager is to maintain a hash-table mapping between buffer
 * frames, called znodes, and the disk locations they represent.  The buffer manager
 * interface is provided in conjunction with the transaction manager interface.  "Clients"
 * of the buffer manager should make calls directly to the transaction manager, for
 * example, to capture a block.  The buffer manager provides routines:
 *
 *   blkget     -- lookup a block by location, get reference (miss case initiate read)
 *   blkremap   -- change the location of a block
 *   blkreplace -- run the INACT list looking for a buffer to eject from memory
 *   getbuf     -- run blkreplace (or wait for others to) until a buffer is available
 *   blkput     -- release a znode reference, return to INACT list
 *   blkcreate  -- create a fresh block mapping
 *   blkcopy    -- copy a block (for capture)
 */

#include "bufmgr.h"

/****************************************************************************************
				    STATIC DECLRATIONS
 ****************************************************************************************/

/* The global buffer manager.
 */
bm_mgr _the_mgr;

const  bm_blockid  __blockid_null  = { 0, 0 };

/* The primes in this table are spaced by approximately a factor of
 * 1.5.  */
#if BM_HASH_PRIME
static const u_int32_t _primes[] =
{
  1, 2, 3, 5, 7,
  11, 19, 37, 73, 109,
  163, 251, 367, 557, 823,
  1237, 1861, 2777, 4177, 6247,
  9371, 14057, 21089, 31627, 47431,
  71143, 106721, 160073, 240101, 360163,
  540217, 810343, 1215497, 1823231, 2734867,
  4102283, 6153409, 9230113, 13845163, 20767711,
  31151543, 46727321, 70090921, 105136301, 157704401,
  236556601, 354834919, 532252367, 798378509, 1197567719,
  1796351503, 2694527203U, 4041790829U
};

static const u_int32_t _nprimes = sizeof (_primes) / sizeof (_primes[0]);
#endif

static void             bufmgr_replace (int        wait_count);
static void             bufmgr_getbuf  (znode     *frame);

/****************************************************************************************
				   FUNCTION DEFINITIONS
 ****************************************************************************************/

static int
bufmgr_frame_isclean (znode const *frame)
{
  /* DEBUG: check that its refcount is zero, blockid is NULL. */
  return (frame->_aunion._atom == NULL &&
	  atomic_read           (& frame->_refcount) == 0 &&
	  bufmgr_blockid_isnull (& frame->_blockid) &&
	  wait_queue_isempty    (& frame->_readwait_queue));
}

znode*
bufmgr_getframe ()
{
  znode *frame = (znode*) kut_slab_new (_the_mgr._frame_slab);

#if BM_DEBUG
  if (frame)
    {
      spin_lock   (& frame->_frame_lock);
      assert      ("jmacd-1", bufmgr_frame_isclean (frame));
      spin_unlock (& frame->_frame_lock);
    }
#endif

  return frame;
}

void
bufmgr_putframe (znode* frame)
{
#if BM_DEBUG
  assert      ("jmacd-2", frame != NULL);
  spin_lock   (& frame->_frame_lock);
  assert      ("jmacd-3", bufmgr_frame_isclean (frame));
  spin_unlock (& frame->_frame_lock);
#endif

  kut_slab_free (_the_mgr._frame_slab, frame);
}

void
bufmgr_putbuf (bm_buf *buffer, int deact)
{
  spin_lock (& _the_mgr._bufwait_lock);

  buffer->_next_in_free  = _the_mgr._buffree_list;
  _the_mgr._buffree_list = buffer;

  /* Decrement the number of released frames. */
  if (deact)
    {
      /* Deactivate--let another process take over buffer replacement. */
      assert ("jmacd-560", _the_mgr._bufwait_active);

      _the_mgr._bufwait_active = 0;
    }

      /* Signal one waiting process. */
      wait_queue_signal (& _the_mgr._bufwait_queue);

      /* Release the bufwait lock. */
      spin_unlock (& _the_mgr._bufwait_lock);
}

static void
bufmgr_frame_ctor (void* vob)
{
  znode *frame = (znode*) vob;

  atomic_set            (& frame->_refcount, 0);

  spin_lock_init        (& frame->_frame_lock);

  bufmgr_blockid_mknull (& frame->_blockid);

  wait_queue_head_init  (& frame->_readwait_queue,
			 & frame->_frame_lock);

  txnmgr_aunion_init    (& frame->_aunion);
}

void
__bufmgr_panic (const char *label, const char *file, int line)
{
  printf ("%s:%d: %s: bufmgr_panic!\n", file, line, label);
  abort ();
}

static int
bufmgr_init_hash (double fill_factor)
{
  int i;
  int ret;

  _the_mgr._bufhash_size     = (u_int32_t) (_the_mgr._frame_count / fill_factor);
  _the_mgr._bufhash_fill_req = fill_factor;

#if BM_HASH_PRIME
  /* The number of hash buckets is computed from the table of primes or the nearest
   * power-of-two, in both cases rounding down in favor of using less memory. */
  for (i = 1; i < _nprimes; i += 1)
    {
      if (_the_mgr._bufhash_size < _primes[i])
	{
	  _the_mgr._bufhash_size = _primes[i-1];
	  break;
	}
    }
#else
  /* Because hash_size is u_int32_t, the largest number of bits is 31, which should be
   * used if the loop below never breaks.  */
  _the_mgr._bufhash_bits = 31;

  for (i = 1; i < 32; i += 1)
    {
      if (_the_mgr._bufhash_size < (1 << i))
	{
	  _the_mgr._bufhash_bits = i-1;
	  break;
	}
    }

  _the_mgr._bufhash_size = (1 << _the_mgr._bufhash_bits);
  _the_mgr._bufhash_mask = (_the_mgr._bufhash_size - 1);
#endif

  /* According to the above constraints, the ratio of actual to requested fill factor
   * should never exceed 2.0.  In the PRIME case it should not exceed 1.5 except that some
   * values in the primes table have slightly more than 1.5 separation.  Compute the
   * actual fill factor and make this assertion. */
  _the_mgr._bufhash_fill_act = 1.0 * _the_mgr._frame_count / _the_mgr._bufhash_size;

  assert ("jmacd-4", _the_mgr._bufhash_fill_act / _the_mgr._bufhash_fill_req <= 2.0);

  /* Now initialize the specific hash types. */
  spin_lock_init (& _the_mgr._bufhash_lock);

  if ((ret = frame_hash_init (& _the_mgr._bufhash, _the_mgr._bufhash_size)))
    {
      return ret;
    }

  return 0;
}

/* The bufmgr_init function computes the number of frames and pages according to the
 * algorithm spec.  @@ can't use FP */
int
bufmgr_init (u_int32_t page_count,
	     double    fill_factor)
{
  int i, ret;

  memset (& _the_mgr, 0, sizeof (_the_mgr));

  _the_mgr._buffer_count  = page_count;

  if (! (_the_mgr._buffers = (bm_buf*) KMALLOC (sizeof (bm_buf) * _the_mgr._buffer_count)))
    {
      return -ENOMEM;
    }

  spin_lock_init (& _the_mgr._bufwait_lock);
  spin_lock_init (& _the_mgr._inact_lock);
  capture_list_init (& _the_mgr._inact_queue);

  wait_queue_head_init (& _the_mgr._bufwait_queue,
			& _the_mgr._bufwait_lock);

  for (i = 0; i < _the_mgr._buffer_count; i += 1)
    {
      _the_mgr._buffers[i]._next_in_free = _the_mgr._buffree_list;
      _the_mgr._buffree_list             = & _the_mgr._buffers[i];
    }

  _the_mgr._frame_count  = BM_EXPECT_ZNODES (page_count);
  _the_mgr._frame_slab   = kut_slab_create ("Buffer Manager Frames",
					    sizeof (znode),
					    KUT_CTOR_ONCE,
					    bufmgr_frame_ctor,
					    NULL);

  bufmgr_init_hash (fill_factor);

  if ((ret = txnmgr_init (& _the_mgr._txn_mgr,
			  TXN_MGR_MAX_HANDLES,
			  TXN_MGR_MAX_ATOMS)))
    {
      return ret;
    }

  if ((ret = logmgr_init (& _the_mgr._log_mgr)))
    {
      return ret;
    }

  return 0;
}

/****************************************************************************************
				      BUFMGR_BLKREF
 ****************************************************************************************/

/* Lock ordering in this routine:
 *
 * BUFHASH_LOCK
 *   |
 * FRAME_LOCK
 *   \
 *    \_ BUFHASH_LOCK
 *    /
 * BUFFREE_LOCK
 *   |
 * BUFWAIT_LOCK
 */
int
bufmgr_blkget (bm_blockid const *blockid,
	       bm_blkref        *blkref)
{
  znode      *the_frame;
  u_int32_t   hash_index = bufmgr_blockid_hash (blockid);

  /* Make sure the hash index is within bounds. */
  assert ("jmacd-5", hash_index < _the_mgr._bufhash_size);

  /* Initial setup of the blkref struct. */
  blkref->_blockid  = *blockid;
  blkref->_frame    = NULL;
  blkref->_flags    = 0;

 retry_hit_race:

  /* Lock the hash chain. */
  spin_lock   (& _the_mgr._bufhash_lock);

  /* Find a matching BLOCKID: Here we are reading the frame's blockid without locking the
   * frame.  We get a consistent result so far as the hash lock is held, but it will be
   * released before locking the frame and the blockid will be tested again.  Still there
   * is a potential problem: we find the frame in the hash chain, release the lock,
   * someone frees that frame before we lock it.  We have to make sure that when we lock
   * it we will detect the wrong blockid, that is, unless it has been reallocated again to
   * the same blockid.  */
  the_frame = frame_hash_find_index (& _the_mgr._bufhash, hash_index, blockid);

  /* @@@ Nikita says: */
  //if (the_frame)
  //  atomic_inc ();

  /* At this point release the hash chain lock, will have to check for races later. */
  spin_unlock (& _the_mgr._bufhash_lock);

  /* FRAME HIT CASE.  No locks are held at this point. */
  if (the_frame)
    {
    retry_miss_race:

      /* The frame was found, set result now. */
      blkref->_frame = the_frame;

      /* Now lock the page and inspect the blockid.  Freeing the frame must reset the
       * blockid with the lock held, and similarly allocation must lock the frame before
       * setting its fields or else this could lead to corruption. */
      spin_lock   (& the_frame->_frame_lock);

      /* To detect a race condition we check both blockid_equal and ZFLAG_INHASH?  When a
       * block is copied (and removed from the hash) it will retain the same blockid but
       * have its INHASH bit cleared. */
      if (unlikely (! bufmgr_blockid_equal (& the_frame->_blockid, blockid) || ! ZF_ISSET (the_frame, ZFLAG_INHASH)))
	{
	  /* See if the blockid still matches.  It could have changed because we've
	   * released the hash chain lock.  Count this event and retry. */
	  spin_unlock  (& the_frame->_frame_lock);
	  BM_STAT_INCR (_the_mgr._blk_hit_races);

	  /* @@@ Nikita says: */
	  //atomic_dec();
	  goto retry_hit_race;
	}

      /* We have found the correct frame: increment its refcount. */
      /* @@@ Nikita says: */
      //atomic_inc (& the_frame->_refcount);

      /* Note: there is nothing about the @@@ INACT queue in blkget.  Explanation: the
       * inactive queue is only used when there is memory pressure and since it is only
       * required to be APPROXIMATE, we can avoid the unnecessary lock contention here.
       *
       * When the reference count falls back to zero, the frame is moved to the back of
       * the inactive queue.  If the inactive-queue scanner encounters a referenced and/or
       * locked frame, the frame is removed from the inactive queue then. */

      /* Check for READIN. */
      if (ZF_ISSET (the_frame, ZFLAG_READIN))
	{
	  /* The page matches, but it is an outstanding request so it must wait. */
	  BM_STAT_INCR (_the_mgr._blk_hit_page_waits);
	  wait_queue_sleep (& the_frame->_readwait_queue, 0);
	  return 0;
	}

      /* The page is found and already in memory.  A hit!  Return locked. */
      BM_STAT_INCR (_the_mgr._blk_hits);
      return 0;
    }

  /* FRAME MISS CASE.  No locks are held at this point. */
  else
    {
      znode *race_frame;

      /* Allocate a frame and queue element. */
      if ((the_frame = bufmgr_getframe ()) == NULL)
	{
	  return -ENOMEM;
	}

      blkref->_frame = the_frame;

      /* NOTE: The frame could be transiently referenced between the hash table lock and
       * frame lock as discussed above, therefore we must lock it before modifying it,
       * even though its free. */
      spin_lock   (& the_frame->_frame_lock);

      /* Initialize the key--has to be done before the hash lock is released. */
      the_frame->_blockid = *blockid;

      /* Now insert into the hash table: since we released the hash lock above there is a
       * chance another process has raced and done the same already. */
      spin_lock   (& _the_mgr._bufhash_lock);

      /* Search the hash chain again to detect a race. */
      if (unlikely ((race_frame = frame_hash_find_index (& _the_mgr._bufhash, hash_index, blockid)) != NULL))
	{
	  /* Release the hash lock. */
	  spin_unlock (& _the_mgr._bufhash_lock);

	  /* Return the frame (reset, unlock, putframe). */
	  znode_return (the_frame);

	  /* Avoid repeating the search by setting the_frame to race_frame.  Take the
	   * race_frame and jump back*/
	  the_frame = race_frame;

	  BM_STAT_INCR (_the_mgr._blk_miss_races);
	  goto retry_miss_race;
	}

      /* Not found in hash chain, ready to insert it. */
      frame_hash_insert_index (& _the_mgr._bufhash, hash_index, the_frame);

      /* Unlock hash chain. */
      spin_unlock (& _the_mgr._bufhash_lock);

      /* Finish initializing the_frame. */
      the_frame->_buffer = NULL;
      the_frame->_zflags = ZFLAG_READIN | ZFLAG_INHASH;

      atomic_set (& the_frame->_refcount, 1);

      /* Release the frame lock: other blkgets will detect READIN (above) and simply enter
       * the _readwait_queue.  This process does not sleep yet, although we're releasing
       * the frame lock. */
      spin_unlock (& the_frame->_frame_lock);

      BM_STAT_INCR (_the_mgr._blk_misses);

      bufmgr_getbuf (the_frame);

      /* We have a buffer ready for reading.  Schedule an I/O.  Need to lock frame again
       * (to avoid I/O completion race) before entering the readwait queue. */
      spin_lock (& the_frame->_frame_lock);

      iosched_read (the_frame, & the_frame->_blockid);

      /* Add this request to the readwait_queue.  Reaquire lock, return locked. */
      wait_queue_sleep (& the_frame->_readwait_queue, 1);
      return 0;
    }
}

/****************************************************************************************
				     BUFMGR_BLKREMAP
 ****************************************************************************************/

/* Lock ordering in this routine:
 *
 * FRAME_LOCK
 *   \
 *    \_ BUFHASH_LOCK
 */
void
bufmgr_blkremap (znode            *frame,
		 bm_blockid const *block)
{
  u_int32_t  old_hash_index = bufmgr_blockid_hash (& frame->_blockid);
  u_int32_t  new_hash_index = bufmgr_blockid_hash (block);
  int check;

  assert ("jmacd-90", bufmgr_frame_isfresh (frame));
  assert ("jmacd-91", spin_is_locked (& frame->_frame_lock));
  assert ("jmacd-95", old_hash_index < _the_mgr._bufhash_size);
  assert ("jmacd-96", new_hash_index < _the_mgr._bufhash_size);

  /* Lock the hash table. */
  spin_lock (& _the_mgr._bufhash_lock);

  /* Remove, update blockid, reinsert. */
  check = frame_hash_remove_index (& _the_mgr._bufhash, old_hash_index, frame);
  frame->_blockid = (*block);
  frame_hash_insert_index (& _the_mgr._bufhash, new_hash_index, frame);

  /* Unlock. */
  spin_unlock (& _the_mgr._bufhash_lock);

  assert ("jmacd-85", check);
}

/****************************************************************************************
				      BUFMGR_REPLACE
 ****************************************************************************************/

/* Only one process can scan the inactive queue at a time.  No locks are held at this
 * point.  Other bufwait requests are queued while this process works on each round of
 * replacement.  The process returns after some NUMBER of replacements, balancing work
 * performed, latency, and negative cache impact.
 *
 * Lock ordering in this method:
 *
 * INACT_LOCK
 *   \
 *    \_ FRAME_LOCK (trylock)
 *    /
 * FRAME_LOCK
 *   \
 *    \_ BUFHASH_LOCK
 *    |
 *    \_ BUFWAIT_LOCK
 */
void
bufmgr_replace (int number)
{
  znode     *frame;
  znode     *next_frame;
  u_int32_t  hash_index;
  int        check;

  BM_STAT_INCR (_the_mgr._blk_miss_buffer_replace);

 again:

  spin_lock (& _the_mgr._inact_lock);

  for (frame = capture_list_front (& _the_mgr._inact_queue);
             ! capture_list_end   (& _the_mgr._inact_queue, frame);
       frame = next_frame)
    {
      /* Try the frame lock.  Cannot wait on this lock due to ordering constraints. */
      if (! spin_trylock (& frame->_frame_lock))
	{
	  next_frame = capture_list_next (frame);
	  continue;
	}

      /* Blocks are removed from the @@@ INACT queue as soon as they are encountered while
       * INACT_LOCK is held.  We now hold the lock: it is a valid, public block location
       * and it has its INACT flag set. */
      assert ("jmacd-660", bufmgr_blockid_isnonnull (& frame->_blockid));
      assert ("jmacd-661", ZF_ISSET                 (frame, ZFLAG_INACT));
      assert ("jmacd-662", ZF_ISSET                 (frame, ZFLAG_INHASH));

      /* Remove it from the INACT queue... */
      next_frame = capture_list_remove_get_next (frame);

      ZF_CLR (frame, ZFLAG_INACT);

      /* The block may have an active reference: blkget does NOT remove frames from the
       * inactive queue. */
      if (atomic_read (& frame->_refcount) != 0)
	{
	  /* ... instead of removing it in blkget, this way there is no INACT_LOCK
	   * contention in blkget. */
	  spin_unlock (& frame->_frame_lock);
	  continue;
	}

      /* Now that we have a locked frame w/ zero references--release INACT_LOCK. */
      spin_unlock (& _the_mgr._inact_lock);

      /* Remove from the hash table (compute hash index, aquire HASH_LOCK, remove...) */
      hash_index = bufmgr_blockid_hash (& frame->_blockid);

      spin_lock (& _the_mgr._bufhash_lock);

      check = frame_hash_remove_index (& _the_mgr._bufhash, hash_index, frame);

      /* Release HASH_LOCK, assert that remove succeeded. */
      spin_unlock (& _the_mgr._bufhash_lock);

      assert ("jmacd-7", check);

      /* Now we have removed it from INACT and HASH.  Return the buffer to the buffer free
       * list, decrement number, and if zero deactivate this bufmgr_replace run. */
      bufmgr_putbuf (frame->_buffer, /*deact=*/ --number == 0);

      /* Return the frame to the free list. */
      znode_return (frame);

      /* Return or repeat. */
      if (number == 0)
	{
	  return;
	}
      else
	{
	  goto again;
	}
    }

  /* If we get here it means we ran through the entire LRU list and found no unreferenced,
   * unlocked pages.  @@ Panic isn't the right thing to do, but I'm not sure what that is
   * yet. */
  rpanic ("jmacd-35", "all buffer frames are currently active");
}

/****************************************************************************************
				      BUFMGR_GETBUF
 ****************************************************************************************/

/* Lock ordering in this method:
 *
 * BUFFREE_LOCK
 *  |
 * BUFWAIT_LOCK
 */
void
bufmgr_getbuf (znode    *frame)
{
 relock_again:

  /* Now looking for an available buffer. */
  spin_lock (& _the_mgr._bufwait_lock);

 nolock_again:

  if (_the_mgr._buffree_list)
    {
      /* The simple case is -- there is an available buffer. */
      frame->_buffer         = _the_mgr._buffree_list;
      _the_mgr._buffree_list = _the_mgr._buffree_list->_next_in_free;

      spin_unlock (& _the_mgr._bufwait_lock);
      return;
    }

  /* If we do not have a buffer, the process needs to wait for a replacement to
   * occur or else run replacement itself. */
  if (_the_mgr._bufwait_active == 0)
    {
      u_int32_t wait_count = _the_mgr._bufwait_count;

      /* Activate replacement case: */
      _the_mgr._bufwait_active = 1;

      /* Release the bufwait lock & run replacement. */
      spin_unlock (& _the_mgr._bufwait_lock);

      bufmgr_replace (wait_count + 1);

      goto relock_again;
    }
  else
    {
      /* Sleep on the bufwait queue. */
      _the_mgr._bufwait_count += 1;

      wait_queue_sleep (& _the_mgr._bufwait_queue, 1);

      _the_mgr._bufwait_count -= 1;

      goto nolock_again;
    }
}

/****************************************************************************************
				     BUFMGR_BLKPUT
 ****************************************************************************************/

/* Lock ordering in this method:
 *
 * FRAME_LOCK
 *   \
 *    \_ INACT_LOCK
 */
static void
bufmgr_blkput_internal (znode* frame, int havelock)
{
  assert ("jmacd-9", frame != NULL && frame->_buffer != NULL);

  if (atomic_read (& frame->_refcount) == 0)
    {
      rpanic ("jmacd-36", "block is not referenced");
    }

  if (atomic_dec_and_test (& frame->_refcount))
    {
      /* Make some assertions: not captured, modified, dirty, writing, busy, etc. */
      bufmgr_frame_can_inactivate (frame);

      /* Aquire the lock if we don't already have it. */
      if (! havelock) spin_lock (& frame->_frame_lock);

      /* Blkput updates position in (possibly returning to) the @@@ INACT queue. */
      spin_lock (& _the_mgr._inact_lock);

      /* See if we're in the INACT queue. */
      if (ZF_MASK (frame, ZFLAG_INACT) == ZFLAG_ZERO)
	{
	  /* Set INACT. */
	  ZF_SET (frame, ZFLAG_INACT);
	}
      else
	{
	  /* Remove from current INACT position. */
	  capture_list_remove (frame);
	}

      /* Return to the end of the inactive list. */
      capture_list_push_back (& _the_mgr._inact_queue, frame);

      /* Release locks. */
      spin_unlock (& _the_mgr._inact_lock);

      if (! havelock) spin_unlock (& frame->_frame_lock);
    }
}

void
bufmgr_blkput (znode* frame)
{
  bufmgr_blkput_internal (frame, 0);
}

void
bufmgr_blkput_locked (znode* frame)
{
  bufmgr_blkput_internal (frame, 1);
}

/****************************************************************************************
				     BUFMGR_BLKCREATE
 ****************************************************************************************/

int
bufmgr_blkcreate (super_block      *super,
		  bm_blkref        *blkref)
{
  u_int32_t  hash_index;
  znode     *frame;

  /* This resembles a trimmed-down vesion of the blkget MISS case. */
  if ((frame = bufmgr_getframe ()) == NULL)
    {
      return -ENOMEM;
    }

  /* Setup blkref */
  blkref->_frame   = frame;
  blkref->_flags   = 0;
  blkref->_blockid._super = super;

  /* Allocated IDs are descending (see "fresh" check) */
  spin_lock (& super->_allocid_lock);
  blkref->_blockid._blkno = --super->_allocid_next;
  spin_unlock (& super->_allocid_lock);

  hash_index = bufmgr_blockid_hash (& blkref->_blockid);

  /* Lock the frame while its being inserted: not because another proc could have this
   * blockid, but because another proc might still have a handle and needs to detect the
   * blockid change. */
  spin_lock (& frame->_frame_lock);

  frame->_blockid  = blkref->_blockid;
  frame->_buffer   = NULL;
  frame->_zflags   = ZFLAG_INHASH | ZFLAG_ALLOC;

  /* Zero refcount: not in @@@ INACT queue yet.  The blkcreate is followed by an immediate
   * capture, which gets the first real reference (similar to blkcopy). */
  atomic_set (& frame->_refcount, 0);

  /* Now insert into the hash table. */
  spin_lock   (& _the_mgr._bufhash_lock);
  frame_hash_insert_index (& _the_mgr._bufhash, hash_index, frame);
  spin_unlock (& _the_mgr._bufhash_lock);

  /* Release the frame lock. */
  spin_unlock (& frame->_frame_lock);

  /* Check that the allocation is right. */
  assert ("jmacd-65", bufmgr_frame_isfresh (frame));

  BM_STAT_INCR (_the_mgr._blk_creates);

  bufmgr_getbuf (frame);

  return 0;
}

/****************************************************************************************
				     BUFMGR_BLKCOPY
 ****************************************************************************************/

/* This function takes an original znode frame and returns a free copy to the buffer pool.
 * The copy may then be captured by another atom.  The original frame is removed from the
 * hash table (~INHASH) and marked copied (COPIED).
 *
 * Lock ordering in this routine:
 *
 * ORIG_AUNION_LOCK
 *  \
 *   \_ ORIG_FRAME_LOCK
 *   /
 * ORIG_FRAME_LOCK
 *  \
 *   \_ COPY_FRAME_LOCK
 *    \
 *     \_ BUFHASH_LOCK
 */
int
bufmgr_blkcopy (znode   *orig,
		znode  **copyp)
{
  znode *copy;
  int    hash_index = bufmgr_blockid_hash (& orig->_blockid);
  int    check;

  if ((copy = bufmgr_getframe ()) == NULL)
    {
      return -ENOMEM;
    }

  (* copyp) = copy;

  /* During a COPY there is no need for a long read-lock because the atom is committing
   * and there are no writers permitted until the block is copied.  */

  spin_lock (& copy->_frame_lock);

  /* These assertions say that the block should have its RELOC & DIRTY flags taken care of
   * by the committing atom.  If the block is not yet copied, then its INHASH is set,
   * otherwise its COPIED is set. */

  assert ("jmacd-400", !ZF_ISSET (orig, ZFLAG_RELOC));
  assert ("jmacd-401", !ZF_ISSET (orig, ZFLAG_DIRTY));

  /* A race to this point could happen.  E.g., two handles in the same atom make the same
   * capture-copy request.  The loser releases its frame_lock in capture_handle, then
   * continues in reverse_lock, checks the atom-not-equal condition and proceeds to call
   * capture_copy. */
  if (ZF_ISSET (orig, ZFLAG_COPIED))
    {
      assert ("jmacd-402", !ZF_ISSET (orig, ZFLAG_INHASH));
      spin_unlock (& orig->_frame_lock);
      znode_return (copy);
      return -EAGAIN;
    }

  if (ZF_ISSET (orig, ZFLAG_COPYING))
    {
      assert ("jmacd-404", !ZF_ISSET (orig, ZFLAG_INHASH));
      znode_return (copy);
      wait_queue_sleep (& copy->_readwait_queue, 0);
      return -EAGAIN;
    }

  assert ("jmacd-405", ZF_ISSET (orig, ZFLAG_INHASH));

  /* Copy fields: holding its frame_lock, aunion is clean. We take the blockid, not
   * relocid, because the block is owned by a stage two+ atom that has already updated the
   * blockid to the relocid. */
  copy->_blockid = orig->_blockid;
  bufmgr_blockid_mknull (& copy->_relocid);

  /* Refcount is held at one until after getbuf, at which point capture_copy returns
   * -EAGAIN with the refcount 0.  The block is not in the LRU list, however, so it cannot
   * be chosen for replacement yet.  Have to worry about blocks that are in the hash but
   * never reach a LRU list.  The solution is to ensure that an EAGAIN return from
   * capture_copy will always retry the blkget.  @@@ This solution is not quite perfect
   * because the retry could fail for some other reason.  Is this atomic_ syntax stupid or
   * what?  An extra reference is needed for ORIG (so that the committing atom can release
   * it during the copy). */

  atomic_set (& copy->_refcount, 1);
  atomic_add (1, & orig->_refcount);

  /* Set flags: The READIN flag is used to block people that attempt to capture while
   * we're copying (incl. waiting for getbuf). */
  copy->_zflags = ZFLAG_INHASH | ZFLAG_READIN;

  ZF_CLR (orig, ZFLAG_INHASH);
  ZF_SET (orig, ZFLAG_COPYING);

  /* Switch in the hash table.  When the following switch occurs, we must unset
   * ZFLAG_INHASH because blkget uses that to detect a race condition. */
  spin_lock (& _the_mgr._bufhash_lock);

  check = frame_hash_remove_index (& _the_mgr._bufhash, hash_index, orig);

  frame_hash_insert_index (& _the_mgr._bufhash, hash_index, copy);

  /* Done with the hash table. */
  spin_unlock (& _the_mgr._bufhash_lock);
  assert ("jmacd-86", check);

  /* Blocking operation ahead: release frame locks. */
  spin_unlock (& orig->_frame_lock);
  spin_unlock (& copy->_frame_lock);

  bufmgr_getbuf (copy);

  memcpy (copy->_buffer->_contents, orig->_buffer->_contents, orig->_blockid._super->s_blocksize);

  spin_lock (& orig->_frame_lock);
  spin_lock (& copy->_frame_lock);

  /* Clear READIN, COPYING, set COPIED */
  ZF_CLR (copy, ZFLAG_READIN);

  ZF_CLR (orig, ZFLAG_COPYING);
  ZF_SET (orig, ZFLAG_COPIED);

  wait_queue_broadcast (& copy->_readwait_queue);

  /* Release the original frame reference, it may drop to zero here. */
  bufmgr_blkput_locked (orig);
  bufmgr_blkput_locked (copy);

  /* The copied frame refcount goes to zero, but it is NOT in the inactive list so it
   * CANNOT be replaced. */
  assert ("jmacd-507", atomic_read (& copy->_refcount) == 0);

  spin_unlock (& orig->_frame_lock);
  spin_unlock (& copy->_frame_lock);

  return 0;
}

/* @@@ Same problem in both blkcopy and blkcreate!  The best solution is to break
 * block_capture and have a capture continuation for copy and created frames.  it should
 * be right now that there is one lock.
 *
 * Another solution is to carry a bufmgr_blkref through to blkcopy(), at which point it
 * can unlock & release the original, swap the copy in and give it a real reference.  But,
 * the EAGAIN logic still breaks this idea.  The return has to finish the capture and
 * return 0...?  And does it also need the capture_mode?  It gets confusing... */
