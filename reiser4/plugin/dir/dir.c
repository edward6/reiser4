/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Methods of directory plugin.
 */

/* version 3 has no directory read-ahead.  This is silly/wrong.  It
   would be nice if there was some commonality between file and
   directory read-ahead code, but I am not sure how well this can be
   done.  */

#include "../../reiser4.h"

/*
 * Directory read-ahead control.
 *
 * FIXME-NIKITA this is just stub. This function is supposed to be
 * called during lookup, readdir, and may be creation.
 *
 */
void reiser4_directory_readahead( struct inode *dir, tree_coord *coord )
{
	assert( "nikita-1682", dir != NULL );
	assert( "nikita-1683", coord != NULL );
	assert( "nikita-1684", coord -> node != NULL );
	assert( "nikita-1685", znode_is_any_locked( coord -> node ) );

	trace_stamp( TRACE_DIR );
}

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */

