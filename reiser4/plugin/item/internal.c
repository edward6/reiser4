/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Implementation of internal-item plugin methods.
 */

#include "../../reiser4.h"

/* see internal.h for explanation */

/** return body of internal item at @coord */
static internal_item_layout *internal_at( const tree_coord *coord )
{
	assert( "nikita-607", coord != NULL );
	assert( "nikita-1650", item_plugin_by_coord( coord ) == 
		plugin_by_id( REISER4_ITEM_PLUGIN_ID, INTERNAL_ITEM_ID ) );
	return ( internal_item_layout * ) item_body_by_coord( coord );
}

/** return child block number stored in the internal item at @coord */
static __u32 pointer_at( const tree_coord *coord )
{
	assert( "nikita-608", coord != NULL );
	return d64tocpu( &internal_at( coord ) -> pointer );
}

/** get znode pointet to by internal @item */
static znode *znode_at( const tree_coord *item )
{
	znode *result;

	spin_lock_dk( current_tree );
	result = child_znode( item, 0 );
	spin_unlock_dk( current_tree );
	return result;
}

/** store pointer from internal item into "block". Implementation of
    ->down_link() method */
void internal_down_link( const tree_coord *coord, 
			 const reiser4_key *key UNUSED_ARG, 
			 reiser4_disk_addr *block )
{
#if REISER4_DEBUG
	reiser4_key item_key;
#endif

	assert( "nikita-609", coord != NULL );
	assert( "nikita-611", block != NULL );
	assert( "nikita-612", ( key == NULL ) ||
		keycmp( item_key_by_coord( coord, 
					   &item_key ), key ) != GREATER_THAN );

	block -> blk = pointer_at( coord );
}

/** 
 * debugging aid: print human readable information about internal item at
 * @coord 
 */
void internal_print( const char *prefix, tree_coord *coord )
{
	info( "%s: internal: %li\n", prefix, ( long ) pointer_at( coord ) );
}

/** return true only if this item really points to "block" */
int internal_has_pointer_to( const tree_coord *coord, 
			     const reiser4_disk_addr *block )
{
	assert( "nikita-613", coord != NULL );
	assert( "nikita-614", block != NULL );

	return pointer_at( coord ) == block -> blk;
}

/**
 * hook called by ->create_item() method of node plugin after new internal
 * item was just created.
 *
 * This is point where pointer to new node is inserted into tree. Initialize
 * parent pointer in child znode, insert child into sibling list and slum.
 *
 */
int internal_create_hook( const tree_coord *item, void *cookie )
{
	znode            *child;

	assert( "nikita-1252", item != NULL );
	assert( "nikita-1253", item -> node != NULL );
	assert( "nikita-1181", znode_get_level( item -> node ) > LEAF_LEVEL );
	assert( "nikita-1450", item -> unit_pos == 0 );

	child = znode_at( item );
	if( ! IS_ERR( child ) ) {
		int result = 0;
		spin_lock_tree( current_tree );
		assert( "nikita-1400", 
			( child -> ptr_in_parent_hint.node == NULL ) ||
			( znode_above_root( child -> ptr_in_parent_hint.node ) ) );
		atomic_inc( &item -> node -> c_count );
		child -> ptr_in_parent_hint = *item;
		child -> ptr_in_parent_hint.between = AT_UNIT;
		if( cookie != NULL )
			reiser4_sibling_list_insert_nolock( child, cookie );

		ZF_CLR( child, ZNODE_NEW );

		trace_on( TRACE_ZWEB, "create: %lli: %i [%lli]\n",
			  item -> node -> blocknr.blk, 
			  atomic_read( &item -> node -> c_count ),
			  child -> blocknr.blk );

		spin_unlock_tree( current_tree );
		zput( child );
		return result;
	} else
		return PTR_ERR( child );
}

/**
 * hook called by ->cut_and_kill() method of node plugin just before internal
 * item is removed.
 *
 * This is point where empty node is removed from the tree. Clear parent
 * pointer in child, and mark node for pending deletion.
 *
 * Node will be actually deleted later and in several installations:
 *  
 *  . when last lock on this node will be released, node will be removed from
 *  the sibling list and its lock will be invalidated
 *
 *  . when last reference to this node will be dropped, bitmap will be updated
 *  and node will be actually removed from the memory.
 *
 *
 */
int internal_kill_hook( const tree_coord *item, 
			unsigned from UNUSED_ARG, unsigned count UNUSED_ARG )
{
	znode *child;

	assert( "nikita-1222", item != NULL );
	assert( "nikita-1224", from == 0 );
	assert( "nikita-1225", count == 1 );

	child = znode_at( item );
	if( IS_ERR( child ) )
		return PTR_ERR( child );
	else if( is_empty_node( child ) ) {
		assert( "nikita-1397", znode_is_write_locked( child ) );
		assert( "nikita-1398", atomic_read( &child -> c_count ) == 0 );
		/* fare thee well */
		ZF_SET( child, ZNODE_HEARD_BANSHEE );
		spin_lock_tree( current_tree );
		child -> ptr_in_parent_hint.node = NULL;
		spin_unlock_tree( current_tree );
		atomic_dec( &item -> node -> c_count );
		trace_on( TRACE_ZWEB, "kill: %lli: %i [%lli]\n",
			  item -> node -> blocknr.blk, 
			  atomic_read( &item -> node -> c_count ),
			  child -> blocknr.blk );

		zput( child );
		return 0;
	} else {
		warning( "nikita-1223", 
			 "Cowardly refuse to remove link to non-empty node" );
		print_znode( "parent", item -> node );
		print_znode( "child", child );
		zput( child );
		return -EIO;
	}
}

/**
 * hook called by ->shift() node plugin method when iternal item was just
 * moved from one node to another.
 *
 * Update parent pointer in child and c_counts in old and new parent
 *
 */
int internal_shift_hook( const tree_coord *item, 
			 unsigned from UNUSED_ARG, unsigned count UNUSED_ARG, 
			 znode *old_node )
{
	znode *child;

	assert( "nikita-1276", item != NULL );
	assert( "nikita-1277", from == 0 );
	assert( "nikita-1278", count == 1 );
	assert( "nikita-1451", item -> unit_pos == 0 );

	child = znode_at( item );
	if( !IS_ERR( child ) ) {
		reiser4_stat_tree_add( reparenting );
		spin_lock_tree( current_tree );
		atomic_inc( &item -> node -> c_count );
		assert( "nikita-1395", child -> ptr_in_parent_hint.node == old_node );
		assert( "nikita-1396", atomic_read( &old_node -> c_count ) > 0 );
		child -> ptr_in_parent_hint = *item;
		atomic_dec( &old_node -> c_count );
		spin_unlock_tree( current_tree );
		zput( child );
		trace_on( TRACE_ZWEB, "shift: %lli: %i -> %lli: %i [%lli]\n",
			  old_node -> blocknr.blk, 
			  atomic_read( &old_node -> c_count ),
			  item -> node -> blocknr.blk, 
			  atomic_read( &item -> node -> c_count ),
			  child -> blocknr.blk );
		return 0;
	} else
		return PTR_ERR( child );
}

/* plugin->u.item.b.max_key_inside - not defined
 */

/* plugin->u.item.b.nr_units - item.c:single_unit
 */

/* plugin->u.item.b.lookup - not defined
 */





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
