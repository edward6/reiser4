/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* A sink is a finite queue.
 *
 * The design of sink and queueelt are taken from the ideas that came out my work with the
 * Ninja PartHTTP cache manager and specifically Matt Welsh's SEDA research (Scalable
 * Event-Driven Architecture, to appear at SOSP 2001).  Basically the idea is to enforce
 * good queuing discipline and minimal lock contention between threads of control.
 * Queuing common requests together has a benefitical side effect on I-cache
 * performance. */

#ifndef __REISER4_SINKMGR_H__
#define __REISER4_SINKMGR_H__

/****************************************************************************************
				    TYPE DELCARATIONS
 ****************************************************************************************/

TS_LIST_DECLARE(sm);

typedef struct _sm_qelt            sm_qelt;
typedef struct _sm_sink            sm_sink;

/****************************************************************************************
				     TYPE DEFINITIONS
 ****************************************************************************************/

struct _sm_qelt
{
  sm_sink        *_sink;      /* The destination of this queue element. */
  sm_list_link    _link;      /* List-link of qelts. */
  void           *_reqdata;   /* Client-supplied request info. */
};

struct _sm_sink
{
  const char    *_sinkname;
  spinlock_t     _sinklock;
  sm_list_head   _readyq;
};

TS_LIST_DEFINE(sm, sm_qelt, _link);

/****************************************************************************************
				  FUNCTION DECLARATIONS
 ****************************************************************************************/

/* To add a waiting event to a WAITQ the caller must somehow lock the
 * WAITQ.  For example, the buffer manager holds _framelock when
 * adding to the read-wait queue. */
static __inline__ void
sink_waitq_add (sm_qelt          *qelt,
		sm_sink          *sink,
		sm_list_head     *waitq,
		void             *reqdata)
{
  qelt->_sink    = sink;
  qelt->_reqdata = reqdata;

  sm_list_push_back (waitq, qelt);
}

extern sm_sink*             sink_create         (const char       *name);
extern void*                sink_dequeue        (sm_sink          *sink);
extern void                 sink_enqueue        (sm_sink          *sink,
						 sm_qelt          *qelt);
extern void                 sink_waitq_complete (sm_list_head     *waitq);

#endif /* __REISER4_SINKMGR_H__ */
