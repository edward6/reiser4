/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* what does this stuff in this file do and why? -Hans @@ new comment: */

/* This code implements stubs that interface a block I/O scheduler, a testing harness.
 * this allows me to test huge amounts of this code without any invovlement in the kernel.
 * this file maintains a background thread (pthread) that accepts and completes requested
 * I/O operations. */

#include "bufmgr.h"

TS_LIST_DECLARE(ioreq);

typedef struct _ioreq  ioreq;

struct _ioreq
{
  bm_blockid        _block;
  int               _isread;
  znode            *_frame;
  ioreq_list_link   _link;
};

TS_LIST_DEFINE(ioreq,ioreq,_link);

static u_int32_t           _io_count;
static ioreq_list_head     _io_queue;
static spinlock_t          _io_lock;
static pthread_t           _io_thread;
static wait_queue_head_t   _io_wait;
static KUT_SLAB           *_io_slab;

static void   iosched_read_complete (znode     *frame);
static void*  iosched_handler       (void      *arg);

/****************************************************************************************
				    IOSCHED FUNCTIONS
 ****************************************************************************************/

void
iosched_init (void)
{
  _io_slab = kut_slab_create ("IO Requests", sizeof (ioreq), 0, NULL, NULL);

  spin_lock_init (& _io_lock);

  ioreq_list_init (& _io_queue);

  wait_queue_head_init (& _io_wait, & _io_lock);

  pthread_create (& _io_thread, NULL, iosched_handler, NULL);
}

u_int32_t
iosched_count (void)
{
  return _io_count;
}

void
iosched_read (znode            *frame,
	      bm_blockid const *block)
{
  ioreq *req = (ioreq*) kut_slab_new (_io_slab);

  req->_frame  = frame;
  req->_isread = 1;
  req->_block  = *block;

  spin_lock (& _io_lock);

  _io_count += 1;

  ioreq_list_push_back (& _io_queue, req);

  wait_queue_signal (& _io_wait);

  spin_unlock (& _io_lock);
}

void
iosched_read_complete (znode *frame)
{
  spin_lock (& frame->_frame_lock);

  assert ("jmacd-12", ZF_ISSET (frame, ZFLAG_READIN));

  ZF_CLR (frame, ZFLAG_READIN);

  wait_queue_broadcast (& frame->_readwait_queue);

  spin_unlock (& frame->_frame_lock);
}

void
iosched_write (znode             *frame,
	       bm_blockid const  *block)
{
  ioreq *req = (ioreq*) kut_slab_new (_io_slab);

  req->_frame  = frame;
  req->_isread = 0;
  req->_block  = *block;

  spin_lock (& _io_lock);

  _io_count += 1;

  ioreq_list_push_back (& _io_queue, req);

  wait_queue_signal (& _io_wait);

  spin_unlock (& _io_lock);
}

void*
iosched_handler (void *arg)
{
  for (;;)
    {
      ioreq *req;

      spin_lock (& _io_lock);

      if (ioreq_list_empty (& _io_queue))
	{
	  wait_queue_sleep (& _io_wait, 1);
	}

      assert ("jmacd-60", ! ioreq_list_empty (& _io_queue));
      assert ("jmacd-61", spin_is_locked (& _io_lock));

      req = ioreq_list_pop_front (& _io_queue);

      spin_unlock (& _io_lock);

      if (req->_isread)
	{
	  iosched_read_complete (req->_frame);
	}
      else
	{
	  txnmgr_write_complete (req->_frame);
	}
    }
}

/****************************************************************************************
				   WAIT QUEUE FUNCTIONS
 ****************************************************************************************/

/* This stuff depends on KUT_LOCK_POSIX, meaning spinlock_t is typedef'd as
 * pthread_mutex_t. */
struct _usched_queue_head_t
{
  pthread_mutex_t   *_lockp;
  pthread_cond_t     _cond;
  int                _count;
};

static void
wait_queue_checklock (wait_queue_head_t  queue)
{
  assert ("jmacd-59", spin_is_locked (queue->_lockp));
}

void
wait_queue_head_init (wait_queue_head_t *queue,
		      spinlock_t        *lockp)
{
  wait_queue_head_t wq = (wait_queue_head_t) malloc (sizeof (struct _usched_queue_head_t));

  wq->_lockp = lockp;

  pthread_cond_init (& wq->_cond, NULL);

  (*queue) = wq;
}

int
wait_queue_isempty (wait_queue_head_t const *queue)
{
  wait_queue_checklock (*queue);

  return (*queue)->_count == 0;
}

void
wait_queue_sleep (wait_queue_head_t *queue,
		  int                relock)
{
  wait_queue_checklock (*queue);

  pthread_cond_wait (& (*queue)->_cond, (*queue)->_lockp);

  if (! relock)
    {
      pthread_mutex_unlock ((*queue)->_lockp);
    }
}

void
wait_queue_signal (wait_queue_head_t *queue)
{
  wait_queue_checklock (*queue);

  pthread_cond_signal (& (*queue)->_cond);
}

void
wait_queue_broadcast (wait_queue_head_t *queue)
{
  wait_queue_checklock (*queue);

  pthread_cond_broadcast (& (*queue)->_cond);
}
