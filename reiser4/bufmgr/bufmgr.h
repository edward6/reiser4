/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* This file contains headers related to the Reiser4 buffer manager.
 * For the time being this is being designed for user-level simulation
 * testing. A number of details will need to be modified before
 * integrating into the kernel, but the ability to simulate and test
 * at user level is a critical first step.
 */

#ifndef __REISER4_BUFMGR_H__
#define __REISER4_BUFMGR_H__

#define KUT_LOCK_POSIX 1
#include <libkut/kutlock.h>

#define KUT_USE_LOCKS 1
#include <libkut/kutslab.h>

#include <libkut/kutmem.h>

#include <asm/atomic.h>

#include <sys/types.h>

/****************************************************************************************
				     MACROS AND STUFF
 ****************************************************************************************/

#ifndef BM_DEBUG
#define BM_DEBUG 0
#endif

/* I am experimenting with this de-coupling of LRU-scanning and buffer waiting.  I
 * observed a 15% performance drop once LRU began in PartHTTP.  Adding a SandStorm stage
 * helped throughput significantly. */
/* ??? -Hans @@ The comment was related to a research project at school called Sandstorm,
 * but the code it was related to has disappeared.  If I told you about Sandstorm you'd
 * probably reply "Didn't work for Squid". */

/* I would like to test several options for managing the hash mapping.  There are two
 * alternatives: */

/* The first alternative simulates the Linux page cache, which uses a single spinlock to
 * protect the entire hash table and hash chaining within each bucket.
 *
 * We know this approach, a single hash lock, is not scalable, but I would like to study
 * the base case which is Linux 2.4 w/o the pagecache_lock SMP-scalability patch.  Maybe
 * only significant for really high processor count (> 8 according to Andrew Morton).
 * Davem & Ingo disagree, noting that it matters in TUX and that it is not due to long
 * lock-hold times, but rather due to dirty-exclusive cachelines being held.  Ben LaHaise
 * claims that the dirty-exclusive issue is an Intel-specific BUG--interesting.  There's a
 * lot of disagreement on this issue, but certainly this will be important for some
 * hardware configurations (especially NUMA). */

/* The second alternative is to use a single spinlock per hash bucket, still hash chaining
 * within each bucket.  @@ Not implemented. */

/* It is worth noting the three properties a replacement algorithm should have according
 * to the Multi-Queue paper:
 *
 * 1. Minimum lifetime: a warm block should stay in the buffer cache for at least the
 *    un-correlated reference period for a given workload (this depends on whether its a
 *    first- or second-level cache).
 * 2. Frequency-based priority: blocks should be prioritized based on their access
 *    frequencies.
 * 3. Temporal frequency: blocksthat were accessed frequently in the past, but have not
 *    been accessed for a relatively long time should be replaced.
 *
 * Only basic LRU is implemented. */

/* I thought we were doing a commit everything when dirty pages exceeds some watermark
 * algorithm? -Hans @@ commit strategy is not directly related to the choice of
 * replacement victims.  this was just here to stimulate thought... and it is basically
 * the same argument going on in the Linux VM area these days.  How to measure and use
 * these statistics to help vm_scanning... */

/* The next option is whether to use a power-of-two hash function or to use a prime
 * number.  With a uniform-random distribution of blockids the overhead for prime hashing
 * is about 0.5%.  With a non-uniform distribution the result could be different. */
#ifndef BM_HASH_PRIME
#define BM_HASH_PRIME 1
#endif

/* Stuff to be replaced w/ <reiser4/debug.h> */
#define assert(label,x) \
    do { if (! (x)) { printf ("%s:%d: %s: assertion failed: %s\n", __FILE__, __LINE__, label, #x); \
    rpanic (label); } } while (0)

#define rpanic(label, ...) __bufmgr_panic(label,__FILE__,__LINE__)
#define warning(label, ...) __bufmgr_panic(label,__FILE__,__LINE__)

extern void __bufmgr_panic    (const char       *label,
			       const char       *file,
			       int               line) __attribute__ ((__noreturn__));
/* this does what? -Hans @@ Everything above (assert, rpanic, warning, __bufmgr_panic) are
 * placeholders awaiting replacement by <reiser4/fs/reiser4/debug.h>. I am not using
 * <debug.h> yet because I can't compile it, and the switch is as trivial as deleting the
 * above macro substitutions. */

/* The unlikely macro. */
#ifndef unlikely
#if defined(__GNUC__) && __GNUC__ >= 3
#define unlikely(x) __builtin_expect((x),0)
#define likely(x)   __builtin_expect((x),1)
#else
#define unlikely(x) (x)
#define likely(x)   (x)
#endif
#endif

/* this does what? -Hans @@ new comment below */
/* The znode_return macro is a common combination of calls used to reset the two location
 * fields, release the frame lock, and return a znode to the list of free znodes. */
#define znode_return(x)                         \
  do {                                          \
    bufmgr_blockid_mknull (& (x)->_blockid);    \
    bufmgr_blockid_mknull (& (x)->_relocid);    \
    spin_unlock           (& (x)->_frame_lock); \
    bufmgr_putframe       (x);                  \
  } while (0)

/* @@ Think about what should be __cachealigned. */

/* This function aquires lock1 while holding lock2, which may require releasing.  If the
 * function returns 1 you must somehow check for a race! */
static __inline__ int
spin_lock_reverse_race (spinlock_t *lock1, spinlock_t *lock2)
{
  if (! spin_trylock (lock1))
    {
      spin_unlock (lock2);
      spin_lock   (lock1);
      spin_lock   (lock2);
      return 1;
    }

  return 0;
}

/* Statistics: actually should use atomic_t if these were really critical, except that
 * only gives 24bits of counter? */
#define BM_STAT_INCR(x)         ((x) += 1)

/* Determine the expected number of znodes, which determines the hash table size.  The
 * number of znodes may be more than the number of available buffers. */
/* what is a frame? -Hans @@ A frame is a znode, a znode is a frame--its been renamed in
 * most places. */
#define BM_EXPECT_ZNODES(x)     ((x) + 2)

/* Macros for accessing the znode bitflags. */
#define	ZF_CLR(p,f)		((p)->_zflags &= ~(f))
#define	ZF_ISSET(p,f)		(((p)->_zflags & (f)) != 0)
#define	ZF_MASK(p,f)		((p)->_zflags & (f))
#define	ZF_SET(p,f)		((p)->_zflags |= (f))

/****************************************************************************************
				    TYPE DELCARATIONS
 ****************************************************************************************/
/* why not simply bool? -Hans @@ is bool defined in the kernel?  part of me likes to use
 * "bool" due to my C++ habits, but the rest of me says to use an int because C
 * programmers expect to read 0 and 1.  */
/*typedef int                        bm_bool;*/

/* I hope that the rest of these are merely temporary renames to be removed after debugging. */
typedef int64_t                    bm_blockno; /* Note: blockno is SIGNED: negative blocks
						* are not-yet-allocated. */

typedef struct _znode              znode;

typedef struct _bm_blockid         bm_blockid;
typedef struct _bm_buf             bm_buf;
typedef struct _bm_mgr             bm_mgr;
typedef struct _bm_blkref          bm_blkref;

typedef struct _txn_atom           txn_atom;
typedef struct _txn_handle         txn_handle;

typedef enum
{
                              /* These flags (protected by _frame_lock) are set TRUE if
			       * the block is currently: */
  ZFLAG_ZERO     = 0,

                                    /* indicating a busy read operation: */
  ZFLAG_READIN   = (1 <<  0),       /* ... being read-in (open handle). */
  ZFLAG_COPYING  = (1 <<  1),       /* ... in-progress copy. */

                                    /* and mutually exclusive: */
  ZFLAG_INHASH   = (1 <<  2),       /* ... in the hash table (public). */
  ZFLAG_COPIED   = (1 <<  3),       /* ... copied (out of the hash chain -- private copy). */
  ZFLAG_VISIBLE  = (ZFLAG_INHASH | ZFLAG_COPIED),

                                    /* capture states: */
  ZFLAG_INACT    = (1 <<  4),       /* ... in the INACT list (possible buffer cache repl. candidate). */
  ZFLAG_CAPTIVE  = (1 <<  5),       /* ... an atoms CAPTURE list (implies aunion.atom != NULL). */

                                    /* modification status: */
  ZFLAG_ALLOC    = (1 <<  6),       /* ... allocated by its current atom (no preserve). */
  ZFLAG_RELOC    = (1 <<  7),       /* ... relocated by its current atom. */
  ZFLAG_WANDER   = (1 <<  8),       /* ... wandered by its current atom. */
  ZFLAG_DELETED  = (1 <<  9),       /* ... deleted. */

  ZFLAG_PRESRV   = (ZFLAG_RELOC | ZFLAG_WANDER),
  ZFLAG_MODIFIED = (ZFLAG_ALLOC | ZFLAG_PRESRV | ZFLAG_DELETED),

                                    /* write states: */
  ZFLAG_DIRTY    = (1 << 10),       /* ... dirty (not flushed). */
  ZFLAG_WRITEOUT = (1 << 11),       /* ... being written: resets dirty, active reference,
				     *     flush in progress.  unlike RBUSY, an atom can
				     *     commit with writeouts in progress*/

  ZFLAG_MAXFLAG  = (1 << 12),

  ZFLAG_BOGUS    = ~(ZFLAG_MAXFLAG-1),

} znode_flags;

#include "tslist.h"
#include "tshash.h"
#include "iosched.h"
#include "freemap.h"
#include "txnmgr.h"
#include "logmgr.h"

/****************************************************************************************
				     TYPE DEFINITIONS
 ****************************************************************************************/

TS_LIST_DECLARE(bwait);

TS_HASH_DECLARE(frame,znode);

struct _bm_blockid
{
  super_block *_super;
  bm_blockno   _blkno;
};

extern const bm_blockid  __blockid_null;

struct _bm_buf
{
  /* -- PLACEHOLDER -- */

  bm_buf           *_next_in_free;
  u_int8_t         *_contents;
};

struct _bm_blkref
{
  bm_blockid        _blockid;
  znode            *_frame;
  u_int32_t         _flags;
};

struct _znode
{
  bm_blockid        _blockid;          /* SUPER and BLKNO: @@ replace super w/ tree says Zam  */
  bm_blockid        _relocid;          /* Temporary location. */

  spinlock_t        _frame_lock;       /* Spinlock on the frame structure, not the page. */

  atomic_t          _refcount;         /* Page ref count: active or waiting znode
					* references. */

  bm_buf           *_buffer;           /* The page resides in this buffer, if present. */

  /* Transaction-related info. */

  txn_aunion        _aunion;           /* The exclusive transaction it belongs to (+ spinlock). */

  capture_list_link _capture_link;     /* The transaction's captured blocks list link. */

  /* Boolean flags. */

  u_int32_t         _zflags;           /* Bitwise flags. */

  /* Read-in related info */

  wait_queue_head_t _readwait_queue;   /* Requests waiting for the page read to
					* complete. */

  /* List links. */

  frame_hash_link   _framehash_link;   /* The FRAME hash chain link. */
};

struct _bm_mgr
{
  /* Buffer array. */
  u_int32_t        _buffer_count;
  bm_buf          *_buffers;          /* Base of the buffer array. */

  /* Frames & allocation. */
  KUT_SLAB        *_frame_slab;
  u_int32_t        _frame_count;      /* Expected number of active frames (a function >=
				       * buffer_count) used to size the hash table. */

  /* Replacement data. */
  spinlock_t        _bufwait_lock;
  wait_queue_head_t _bufwait_queue;   /* Requests waiting for a free buffer to read
				       * into. */
  int               _bufwait_active;  /* True if a process is performing replacement. */
  u_int32_t         _bufwait_count;   /* Number of waiting requests. */
  bm_buf           *_buffree_list;

  /* Hash table elements. */
  u_int32_t        _bufhash_size;
  double           _bufhash_fill_req;
  double           _bufhash_fill_act; /* @@ Can't use FP in kernel! */

#if BM_HASH_PRIME == 0
  u_int32_t        _bufhash_bits;
  u_int32_t        _bufhash_mask;
#endif

  spinlock_t       _bufhash_lock;
  frame_hash_table _bufhash;

  /* Inactive elements. */
  spinlock_t          _inact_lock;
  capture_list_head   _inact_queue;

  /* Transaction state. */
  txn_mgr          _txn_mgr;

  /* Log state. */
  log_mgr          _log_mgr;

  /* Blkref statistics */
  u_int64_t        _blk_hits;                /* Return BM_CACHE_HIT */
  u_int64_t        _blk_hit_page_waits;      /* Return BM_PAGE_WAIT */
  u_int64_t        _blk_hit_races;           /* Causes a retry. */

  u_int64_t        _blk_misses;              /* Return BM_PAGE_READ */
  u_int64_t        _blk_miss_races;          /* Causes a retry. */
  u_int64_t        _blk_miss_buffer_replace; /* Activate bufmgr_replace */

  u_int64_t        _blk_creates;             /* Block creation. */
};

/* There is a single manager, global for now. */
extern bm_mgr _the_mgr;

/****************************************************************************************
				  FUNCTION DECLARATIONS
 ****************************************************************************************/

static __inline__ u_int32_t
bufmgr_blockid_hash  (bm_blockid const *blockid)
{
  u_int32_t x = ((long) (blockid->_super) / sizeof (super_block)) ^ (u_int32_t) blockid->_blkno;

#if BM_HASH_PRIME
  /* If the table is prime compute the modulus. */
  x %= _the_mgr._bufhash_size;
#else
  /* If the table is power-of-two compute the mask. */
  x &= _the_mgr._bufhash_mask;
#endif

  return x;
}

static __inline__ int
bufmgr_blockid_equal (bm_blockid const *a,
		      bm_blockid const *b)
{
  return a->_super == b->_super && a->_blkno == b->_blkno;
}

static __inline__ void
bufmgr_blockid_mknull (bm_blockid *a)
{
  a->_super = NULL;
  a->_blkno = 0;
}

static __inline__ int
bufmgr_blockid_isnull (bm_blockid const *a)
{
  return a->_super == NULL && a->_blkno == 0;
}

static __inline__ int
bufmgr_blockid_isnonnull (bm_blockid const *a)
{
  return a->_super != NULL;
}

static __inline__ int
bufmgr_frame_isfresh (znode *frame)
{
  return frame->_blockid._blkno < 0;
}


extern int                  bufmgr_init        (u_int32_t         page_count,
					        double            fill_factor);

extern int                  bufmgr_blkget      (bm_blockid const *block,
					        bm_blkref        *blkref);

extern void                 bufmgr_blkput      (znode            *frame);

extern void                 bufmgr_blkput_locked (znode            *frame);

extern int                  bufmgr_blkcreate   (super_block      *super,
						bm_blkref        *blkref);

extern void                 bufmgr_blkremap    (znode            *frame,
						bm_blockid const *block);

extern int                  bufmgr_blkcopy     (znode            *frame,
						znode           **copy);

extern void                 bufmgr_putbuf      (bm_buf           *buffer,
						int               deact);

extern znode*               bufmgr_getframe    (void);

extern void                 bufmgr_putframe    (znode*            frame);


static inline void
bufmgr_frame_can_commit (znode *frame)
{
  /* a READIN flag implies an open handle, prevents commit */
  assert ("jmacd-520",  ZF_MASK (frame, ZFLAG_READIN)  == ZFLAG_ZERO);

  /* a COPYING flag implies already committing */
  assert ("jmacd-521",  ZF_MASK (frame, ZFLAG_COPYING)  == ZFLAG_ZERO);

  /* the two visible flags are mutually exclusive, one is always set */
  assert ("jmacd-522", (ZF_MASK (frame, ZFLAG_VISIBLE) == ZFLAG_INHASH ||
			ZF_MASK (frame, ZFLAG_VISIBLE) == ZFLAG_COPIED));

  /* may be inactive (no memory pressure) */
  /* must be captured */
  assert ("jmacd-523",  ZF_ISSET (frame, ZFLAG_CAPTIVE));

  /* reloc and wandered cannot both be set (preserve bits) */
  assert ("jmacd-524",  ZF_MASK (frame, ZFLAG_PRESRV) != ZFLAG_PRESRV);

  /* alloc implies no preserve bits are set */
  assert ("jmacd-525", (ZF_MASK (frame, ZFLAG_ALLOC)   == ZFLAG_ZERO ||
			ZF_MASK (frame, ZFLAG_PRESRV)  == ZFLAG_ZERO));

  /* if the block was deleted, it is no longer part of our capture list, it may be in the
   * deallocate set and if it was relocated, the relocated block has already been
   * released. */
  assert ("jmacd-526", (ZF_MASK (frame, ZFLAG_DELETED) == ZFLAG_ZERO));

  /* may be dirty or in writeout */
  /* check for bogus flags */
  assert ("jmacd-527",  ZF_MASK (frame, ZFLAG_BOGUS)   == ZFLAG_ZERO);
}

static inline void
bufmgr_frame_can_inactivate (znode *frame)
{
  /* Since a block is referenced while being written, being read, copying, or being captured. */
  assert ("jmacd-935", ! ZF_ISSET (frame, ZFLAG_READIN | ZFLAG_COPYING));

  /* The two visible flags are mutually exclusive, one is always set. */
  assert ("jmacd-936", (ZF_MASK (frame, ZFLAG_VISIBLE) == ZFLAG_INHASH ||
		       ZF_MASK (frame, ZFLAG_VISIBLE) == ZFLAG_COPIED));

  /* May be inactive */

  /* Since capturing a block increments its refcount, we can't get here while
   * captured. */
  assert ("jmacd-937", ZF_MASK (frame, ZFLAG_CAPTIVE) == ZFLAG_ZERO);

  /* Can't be modified */
  assert ("jmacd-938", ZF_MASK (frame, ZFLAG_MODIFIED) == ZFLAG_ZERO);

  /* May be inactive */
  /* Since capturing a block increments its refcount, we can't get here while
   * captured. */
  assert ("jmacd-939", ZF_MASK (frame, ZFLAG_CAPTIVE) == ZFLAG_ZERO);

  /* Can't be dirty/writing */
  assert ("jmacd-940", ZF_MASK (frame, ZFLAG_DIRTY | ZFLAG_WRITEOUT) == ZFLAG_ZERO);
}

/****************************************************************************************
				    GENERIC STRUCTURES
 ****************************************************************************************/

TS_LIST_DEFINE(capture,znode,_capture_link);

TS_HASH_DEFINE(frame,znode,bm_blockid,_blockid,_framehash_link,bufmgr_blockid_hash,bufmgr_blockid_equal);

# endif /* __REISER4_BUFMGR_H__ */
