/* Copyright 2002, 2003, 2004 by Hans Reiser, licensing governed by reiser4/README  */

#include "reiser4.h"
#include "debug.h"
#include "context.h"
#include "txnmgr.h"
#include "writeout.h"

reiser4_internal int get_writeout_flags(void)
{
	reiser4_context * ctx = get_current_context();

	return (ctx->entd || get_rapid_flush_mode()) ? WRITEOUT_FOR_PAGE_RECLAIM : 0;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
