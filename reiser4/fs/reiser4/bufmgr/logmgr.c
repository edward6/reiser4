/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* please define the responsibilities of the logmgr. -Hans @@ As you can see, there are no
 * respondibilities at this time. */


#include "bufmgr.h"

/*****************************************************************************************
				       LOGMGR_INIT
 *****************************************************************************************/

int
logmgr_init (log_mgr  *mgr)
{
  return 0;
}

/*****************************************************************************************
				 LOGMGR_SET_JOURNAL_BLOCK
 *****************************************************************************************/

void
logmgr_set_journal_block (txn_atom *atom,
			  znode    *frame)
{
}

void
logmgr_prepare_journal (txn_atom *atom)
{
}
