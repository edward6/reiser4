/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Implementation of internal-item plugin methods.
 */

#include "../../reiser4.h"

/* see internal.h for explanation */

/* plugin->u.item.b.mergeable */
/* Audited by: green(2002.06.14) */
int internal_mergeable (const coord_t * p1 UNUSED_ARG /* first item */,
			const coord_t * p2 UNUSED_ARG /* second item */)
{
	/* internal items are not mergeable */
	return 0;
}

/* ->lookup() method for internal items */
/* Audited by: green(2002.06.14) */
lookup_result internal_lookup (const reiser4_key *key /* key to look up */, 
			       lookup_bias bias UNUSED_ARG /* lookup bias */,
			       coord_t *coord /* coord of item */ )
{
	reiser4_key ukey;

	switch( keycmp( unit_key_by_coord( coord, &ukey ), key ) ) {
	default: impossible( "", "keycmp()?!" );
	case LESS_THAN:
		/*
		 * FIXME-VS: AFTER_ITEM used to be here. But with new coord
		 * item plugin can not be taken using coord set this way
		 */
		assert( "vs-681", coord -> unit_pos == 0 );
		coord -> between = AFTER_UNIT;
	case EQUAL_TO:
		return CBK_COORD_FOUND;
	case GREATER_THAN:
		return CBK_COORD_NOTFOUND;
	}
}

/** return body of internal item at @coord */
/* Audited by: green(2002.06.14) */
static internal_item_layout *internal_at( const coord_t *coord /* coord of
								   * item */ )
{
	assert( "nikita-607", coord != NULL );
	assert( "nikita-1650", item_plugin_by_coord( coord ) == 
		item_plugin_by_id( NODE_POINTER_ID ) );
	return ( internal_item_layout * ) item_body_by_coord( coord );
}

/* FIXME: ugly hack */
void internal_update (const coord_t *coord, reiser4_block_nr blocknr)
{
	internal_item_layout *item = internal_at (coord);
	cpu_to_dblock (blocknr, & item->pointer);
}

/** return child block number stored in the internal item at @coord */
/* Audited by: green(2002.06.14) */
static reiser4_block_nr pointer_at( const coord_t *coord /* coord of item */ )
{
	assert( "nikita-608", coord != NULL );
	return dblock_to_cpu( & internal_at( coord ) -> pointer );
}

/** get znode pointed to by internal @item */
/* Audited by: green(2002.06.14) */
static znode *znode_at( const coord_t *item /* coord of item */, 
			znode *parent /* parent node */, int incore_p )
{
	znode *result;

	/* Take DK lock, as required by child_znode. */
	spin_lock_dk( current_tree );
	result = child_znode( item, parent, incore_p, 0 );
	spin_unlock_dk( current_tree );
	return result;
}

/** store pointer from internal item into "block". Implementation of
    ->down_link() method */
/* Audited by: green(2002.06.14) */
void internal_down_link( const coord_t *coord /* coord of item */, 
			 const reiser4_key *key UNUSED_ARG /* key to get
							    * pointer for */, 
			 reiser4_block_nr *block /* resulting block number */ )
{
#if REISER4_DEBUG
	reiser4_key item_key;
#endif

	assert( "nikita-609", coord != NULL );
	assert( "nikita-611", block != NULL );
	assert( "nikita-612", ( key == NULL ) || 
		/* twig horrors */
		( znode_get_level( coord -> node ) == TWIG_LEVEL ) ||
		keyle( item_key_by_coord( coord, &item_key ), key ) );

	*block = pointer_at( coord );
}


/**
 * Set if the the child is dirty.
 */
/* Audited by: green(2002.06.14) */
int internal_utmost_child_dirty ( const coord_t  *coord,
				  sideof             side UNUSED_ARG,
				  int               *is_dirty )
{
	jnode* child;
	int ret;

	assert ("jmacd-2058", is_dirty != NULL); 

	if ((ret = internal_utmost_child (coord, side, & child))) {
		return ret;
	}

	if (child == NULL) {
		*is_dirty = 0;
	} else {
		*is_dirty = jnode_check_dirty (child);
		jput (child);
	}
	return 0;
}

/**
 * Get the child's block number, or 0 if the block is unallocated.
 */
/* Audited by: green(2002.06.14) */
int internal_utmost_child_real_block ( const coord_t  *coord,
				       sideof             side UNUSED_ARG,
				       reiser4_block_nr  *block )
{
	assert ("jmacd-2059", coord != NULL); 

	*block = pointer_at (coord);

	if (blocknr_is_fake (block)) {
		*block = 0;
	}

	return 0;
}

/**
 * Return the child.
 */
/* Audited by: green(2002.06.14) */
int internal_utmost_child ( const coord_t  *coord,
			    sideof             side UNUSED_ARG,
			    jnode            **childp )
{
	reiser4_block_nr block = pointer_at (coord);
	znode *child;

	assert ("jmacd-2059", childp != NULL); 

	child = zlook (current_tree, & block);

	if (IS_ERR (child)) {
		return PTR_ERR (child);
	}

	*childp = ZJNODE (child);

	return 0;
}

/** 
 * debugging aid: print human readable information about internal item at
 * @coord 
 */
/* Audited by: green(2002.06.14) */
void internal_print( const char *prefix /* prefix to print */, 
		     coord_t *coord /* coord of item to print  */ )
{
	info( "%s: internal: %llu\n", prefix, pointer_at( coord ) );
}

/** return true only if this item really points to "block" */
/* Audited by: green(2002.06.14) */
int internal_has_pointer_to( const coord_t *coord /* coord of item */, 
			     const reiser4_block_nr *block /* block number to
							    * check */ )
{
	assert( "nikita-613", coord != NULL );
	assert( "nikita-614", block != NULL );

	return pointer_at( coord ) == *block;
}

/**
 * hook called by ->create_item() method of node plugin after new internal
 * item was just created.
 *
 * This is point where pointer to new node is inserted into tree. Initialize
 * parent pointer in child znode, insert child into sibling list and slum.
 *
 */
/* Audited by: green(2002.06.14) */
int internal_create_hook( const coord_t *item /* coord of item */, 
			  void *arg /* child's left neighbor, if any */ )
{
	znode            *child;

	assert( "nikita-1252", item != NULL );
	assert( "nikita-1253", item -> node != NULL );
	assert( "nikita-1181", znode_get_level( item -> node ) > LEAF_LEVEL );
	assert( "nikita-1450", item -> unit_pos == 0 );

	child = znode_at( item, item -> node, 0 );
	if( ! IS_ERR( child ) ) {
		int result = 0;
		spin_lock_dk( current_tree );
		spin_lock_tree( current_tree );
		assert( "nikita-1400", 
			( child -> ptr_in_parent_hint.node == NULL ) ||
			( znode_above_root( child -> ptr_in_parent_hint.node ) ) );
		atomic_inc( &item -> node -> c_count );
		child -> ptr_in_parent_hint = *item;
		child -> ptr_in_parent_hint.between = AT_UNIT;
		if( arg != NULL )
			sibling_list_insert_nolock( child, arg );

		ZF_CLR( child, ZNODE_ORPHAN );

		trace_on( TRACE_ZWEB, "create: %lli: %i [%lli]\n",
			  *znode_get_block( item -> node ),
			  atomic_read( &item -> node -> c_count ),
			  *znode_get_block( child ) );

		spin_unlock_tree( current_tree );
		spin_unlock_dk( current_tree );
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
/* Audited by: green(2002.06.14) */
int internal_kill_hook( const coord_t *item /* coord of item */, 
			unsigned from UNUSED_ARG /* start unit */, 
			unsigned count UNUSED_ARG /* stop unit */, 
			void *kill_params UNUSED_ARG )
{
	znode *child;

	assert( "nikita-1222", item != NULL );
	assert( "nikita-1224", from == 0 );
	assert( "nikita-1225", count == 1 );

	child = znode_at( item, item -> node, 0 );
	if( IS_ERR( child ) )
		return PTR_ERR( child );
	else if( node_is_empty( child ) ) {
		assert( "nikita-1397", znode_is_write_locked( child ) );
		assert( "nikita-1398", atomic_read( &child -> c_count ) == 0 );
		/* fare thee well */
		ZF_SET( child, ZNODE_HEARD_BANSHEE );
		spin_lock_tree( current_tree );
		coord_init_zero( &child -> ptr_in_parent_hint );
		spin_unlock_tree( current_tree );
		del_c_ref( item -> node );
		trace_on( TRACE_ZWEB, "kill: %lli: %i [%lli]\n",
			  *znode_get_block( item -> node ),
			  atomic_read( &item -> node -> c_count ),
			  *znode_get_block( child ) );

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
/* Audited by: green(2002.06.14) */
int internal_shift_hook( const coord_t *item /* coord of item */, 
			 unsigned from UNUSED_ARG /* start unit */, 
			 unsigned count UNUSED_ARG /* stop unit */, 
			 znode *old_node /* old parent */ )
{
	znode *child;
	znode *new_node;
	reiser4_tree *tree;

	assert( "nikita-1276", item != NULL );
	assert( "nikita-1277", from == 0 );
	assert( "nikita-1278", count == 1 );
	assert( "nikita-1451", item -> unit_pos == 0 );

	new_node = item -> node;
	assert( "nikita-2132", new_node != old_node );
	tree = current_tree;
	spin_lock_dk( tree );
	child = child_znode( item, old_node, 1, 1 );
	spin_unlock_dk( tree );
	if( child == NULL )
		return 0;
	if( !IS_ERR( child ) ) {
		reiser4_stat_tree_add( reparenting );
		spin_lock_tree( tree );
		atomic_inc( &new_node -> c_count );
		assert( "nikita-1395", znode_parent( child ) == old_node );
		assert( "nikita-1396", atomic_read( &old_node -> c_count ) > 0 );
		child -> ptr_in_parent_hint = *item;
		assert( "nikita-1781", znode_parent( child ) == new_node );
		assert( "nikita-1782", check_tree_pointer( item, 
							   child ) == NS_FOUND );
		del_c_ref( old_node );
		spin_unlock_tree( tree );
		zput( child );
		trace_on( TRACE_ZWEB, "shift: %lli: %i -> %lli: %i [%lli]\n",
			  *znode_get_block( old_node ), 
			  atomic_read( &old_node -> c_count ),
			  *znode_get_block( new_node ), 
			  atomic_read( &new_node -> c_count ),
			  *znode_get_block( child ) );
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
