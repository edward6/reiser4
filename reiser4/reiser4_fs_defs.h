/* -*- c -*- */

/* fs/reiser4/reiser4_fs_defs.h */

/* how about just calling it reiser4.h? -Hans */

/* state: uncompiled. */

/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * reiser4_fs_i.h and reiser4_fs_sb.h
 */

#include <linux/list.h>

#if !defined( __REISER4_H__ )
#define __REISER4_H__

/* some defs go here */
#endif

/*
 * $Log$
 * Revision 1.3  2001/08/21 15:10:54  reiser
 * renamed it.
 *
 * Revision 1.2  2001/08/21 11:11:23  reiser
 * reiser4 is called reiser4, not reiser4_fs.  No unnecessary FS usage, and the fs_fs was really unreasonable....
 *
 * This is by user request by the way....
 *
 * Revision 1.1  2001/08/16 11:33:10  god
 * added reiser4_fs_*_.h files
 * plugin/plugin.c:
 *     find_plugin(): commented
 *     Move audit plugin into file plugin type.
 * 	Merged changes from plugin/base.c which is sheduled for removal.
 * 	Improper comments about plugins not being addressed by array
 * 	indices removed.
 * plugin/types.h, plugin/plugin.c:
 * Get rid of the rest of REISER4_DYNAMIC_PLUGINS.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 */
/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"

what is LC mode?

 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
