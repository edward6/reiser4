/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Lnode manipulation functions.
 */

#include "reiser4.h"

struct inode *inode_by_lnode( const lnode *node )
{
	assert( "nikita-1851", node != NULL );
	assert( "nikita-1852", node -> h.type == LNODE_INODE );
	return &list_entry( node, reiser4_inode_info, 
			    plugin.lnode_header ) -> vfs_inode;
}

reiser4_key *lnode_key( const lnode *node, reiser4_key *result )
{
	assert( "nikita-1849", node != NULL );
	switch( node -> h.type ) {
	default: wrong_return_value( "nikita-1850", "node -> h.type" );
	case LNODE_INODE:
		return build_sd_key( inode_by_lnode( node ), result );
	case LNODE_PSEUDO:
		/* FIXME-NIKITA: not yet */
	case LNODE_LW:
		*result = node -> lw.key;
		return result;
	}
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
