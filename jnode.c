/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Jnode manipulation functions.
 */
/*
 * Jnode is entity used to track blocks with data and meta-data in reiser4.
 *
 * In particular, jnodes are used to track transactional information
 * associated with each block. Each znode contains jnode as ->zjnode field.
 *
 * Jnode stands for either Josh or Journall node.
 *
 */

#include "reiser4.h"

/** block number of node */
/* Audited by: umka (2002.06.11) */
const reiser4_block_nr *jnode_get_block( const jnode *node /* jnode to
							    * query */ )
{
	assert( "nikita-528", node  != NULL );

/* As soon as we implement accessing nodes not stored on block devices
   (e.g. distributed reiserfs), then we need to replace this line with
   a call to a node plugin.

   Josh replies: why not extent the block number to be
   node_id/device/block_nr.  I don't think the concept of a block number
   changes in a distributed setting, but you will need a node method to get
   the block: likely we already have that.
*/
	return & node -> blocknr;
}

/* Audited by: umka (2002.06.11) */
void jnode_set_block( jnode *node /* jnode to update */,
		      const reiser4_block_nr *blocknr /* new block nr */ )
{
	assert( "nikita-2020", node  != NULL );
	assert( "umka-055", blocknr != NULL );
	
	node -> blocknr = *blocknr;
}

/* return true if jnode has real blocknr */
int jnode_has_block (jnode * node)
{
	assert ("vs-673", node);
	assert ("vs-674", jnode_is_unformatted (node));
	return node->blocknr;
}

/* Audited by: umka (2002.06.15) */
void jnode_attach_page_nolock( jnode *node, struct page *pg )
{
	assert( "nikita-2060", node != NULL );
	assert( "nikita-2061", pg != NULL );

	assert( "nikita-2050", 
		( pg -> private == 0ul ) || 
		( pg -> private == ( unsigned long ) node ) );
	if( !PagePrivate( pg ) ) {
		assert( "nikita-2059", pg -> private == 0ul );
		pg -> private = ( unsigned long ) node;
		node -> pg  = pg;
		SetPagePrivate( pg );
		/* add reference to page */
		page_cache_get( pg );
	}
}

/* Audited by: umka (2002.06.15) */
void jnode_attach_page( jnode *node, struct page *pg )
{
	assert( "nikita-2047", node != NULL );
	assert( "nikita-2048", pg != NULL );

	trace_on( TRACE_PCACHE, "attach: node %p, page: %p\n", node, pg );

	spin_lock( &_jnode_ptr_lock );
	jnode_attach_page_nolock( node, pg );
	spin_unlock( &_jnode_ptr_lock );
}

/* Audited by: umka (2002.06.15) */
static void break_page_jnode_linkage( struct page *page, jnode *node )
{
	assert( "nikita-2063", page != NULL );
	assert( "nikita-2064", node != NULL );

	trace_on( TRACE_PCACHE, "break page: %p\n", page );

	page -> private = 0ul;
	ClearPagePrivate( page );
	node -> pg = NULL;
}

/* Audited by: umka (2002.06.15) */
jnode *page_detach_jnode( struct page *page )
{
	jnode *node;

	assert( "nikita-2062", page != NULL );

	trace_on( TRACE_PCACHE, "detach page: %p\n", page );

	spin_lock( &_jnode_ptr_lock );
	node = ( jnode * ) page -> private;
	if( node != NULL )
		break_page_jnode_linkage( page, node );
	spin_unlock( &_jnode_ptr_lock );
	page_cache_release( page );
	return node;
}

/* Audited by: umka (2002.06.15) */
void jnode_detach_page( jnode *node )
{
	struct page *page;

	assert( "nikita-2052", node != NULL );

	trace_on( TRACE_PCACHE, "detach jnode: %p\n", node );

	spin_lock( &_jnode_ptr_lock );
	page = jnode_page( node );
	if( page == NULL ) {
		spin_unlock( &_jnode_ptr_lock );
		return;
	}
	break_page_jnode_linkage( page, node );
	spin_unlock( &_jnode_ptr_lock );
	page_cache_release( page );
}

#if REISER4_DEBUG

#define jnode_state_name( node, flag )			\
	( JF_ISSET( ( node ), ( flag ) ) ? ((#flag ## "|")+6) : "" )

/** debugging aid: output human readable information about @node */
/* Audited by: umka (2002.06.11) */
void info_jnode( const char *prefix /* prefix to print */, 
		 const jnode *node /* node to print */ )
{
	assert( "umka-068", prefix != NULL );
	
	if( node == NULL ) {
		info( "%s: null\n", prefix );
		return;
	}

	info( "%s: %p: state: %lu: [%s%s%s%s%s%s%s%s%s%s%s], level: %i, pg: %p, ",
	      prefix, node, node -> state, 

	      jnode_state_name( node, ZNODE_LOADED ),
	      jnode_state_name( node, ZNODE_HEARD_BANSHEE ),
	      jnode_state_name( node, ZNODE_LEFT_CONNECTED ),
	      jnode_state_name( node, ZNODE_RIGHT_CONNECTED ),
	      jnode_state_name( node, ZNODE_NEW ),
	      jnode_state_name( node, ZNODE_ALLOC ),
	      jnode_state_name( node, ZNODE_RELOC ),
	      jnode_state_name( node, ZNODE_WANDER ),
	      jnode_state_name( node, ZNODE_DIRTY ),
	      jnode_state_name( node, ZNODE_WRITEOUT ),
	      jnode_state_name( node, ZNODE_IS_DYING ),
	      
	      jnode_get_level( node ), jnode_page( node ) );
}

#endif

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
