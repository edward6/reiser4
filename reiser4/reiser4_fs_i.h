/* -*- c -*- */

/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definition of reiser4-specific part of in-memory VFS struct inode
 */

#if !defined( __REISER4_I_H__ )
#define __REISER4_I_H__

/** reiser4-specific part of inode */
struct reiser4_inode_info {
	/** key of object */
	key                       key;
	/** plugin, associated with inode and its state, including
	     dependant plugins: 
	      . security, 
	      . tail policy, 
	      . hash for directories */
	reiser4_plugin_ref plugin;
};

/* __REISER4_I_H__ */
#endif

/*
 * $Log$
 * Revision 1.3  2001/09/06 10:59:12  reiser
 * Removed unnecessary fs letters.
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
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
