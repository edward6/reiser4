/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "bufmgr.h"

/****************************************************************************************
				      SINK FUNCTIONS
 ****************************************************************************************/

sm_sink*
sink_create (const char  *name)
{
  sm_sink *sink = (sm_sink*) KMALLOC (sizeof (sm_sink));

  sink->_sinkname = name;

  spin_lock_init (& sink->_sinklock);

  sm_list_init (& sink->_readyq);

  return sink;
}

void*
sink_dequeue (sm_sink *sink)
{
  sm_qelt *qelt  = NULL;
  void    *reqdata = NULL;

  spin_lock   (& sink->_sinklock);

  if (! sm_list_empty (& sink->_readyq))
    {
      qelt = sm_list_pop_front (& sink->_readyq);
    }

  spin_unlock (& sink->_sinklock);

  if (qelt)
    {
      reqdata = qelt->_reqdata;

      kut_slab_free (_the_mgr._qelt_slab, qelt);
    }

  return reqdata;
}

void
sink_waitq_complete (sm_list_head *waitq)
{
  while (! sm_list_empty (waitq))
    {
      sm_qelt *qelt = sm_list_pop_front (waitq);

      sink_enqueue (qelt->_sink, qelt);
    }
}

void
sink_enqueue (sm_sink          *sink,
	      sm_qelt          *qelt)
{
  spin_lock   (& sink->_sinklock);

  sm_list_push_back (& sink->_readyq, qelt);

  spin_unlock (& sink->_sinklock);
}
