/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README. */

/* implementation of carry operations */

#include "reiser4.h"

static int carry_shift_data( sideof side, coord_t *insert_coord, znode *node,
			     carry_level *doing, carry_level *todo, 
			     int including_insert_coord_p );

extern int lock_carry_node( carry_level *level, carry_node *node );
extern int lock_carry_node_tail( carry_node *node );

/**
 * find left neighbor of a carry node
 *
 * Look for left neighbor of @node and add it to the @doing queue. See
 * comments in the body.
 *
 */
static carry_node *find_left_neighbor( carry_node *node /* node to find left
							 * neighbor of */, 
				       carry_level *doing /* level to scan */ )
{
	int         result;
	carry_node *left;

	/* first, check whether left neighbor is already in a @doing queue */
	left = find_left_carry( node, doing );
	/*
	 * if we are not at the left end of level, check whether left neighbor
	 * was found.
	 */
	if( left != NULL ) {
		spin_lock_tree( current_tree );
		if( node -> real_node -> left != left -> real_node ) {
			left = NULL;
		}
		spin_unlock_tree( current_tree );
		if( left != NULL ) {
			reiser4_stat_level_add( doing, carry_left_in_carry );
			return left;
		}
	}

	left = add_carry_skip( doing, POOLO_BEFORE, node );
	if( IS_ERR( left ) )
		return left;

	left -> node = node -> node;
	left -> free = 1;

	/* then, feeling lucky, peek left neighbor in the cache. */
	result = reiser4_get_left_neighbor( &left -> lock_handle,
					    node -> real_node,
					    ZNODE_WRITE_LOCK, GN_TRY_LOCK );
	if( result == 0 ) {
		/* ok, node found and locked. */
		result = lock_carry_node_tail( left );
		if( result != 0 )
			left = ERR_PTR( result );
		reiser4_stat_level_add( doing, carry_left_in_cache );
	} else if( result == -ENAVAIL ) {
		/*
		 * node is leftmost node in a tree, or there is an extent on
		 * the left.
		 */
		reiser4_stat_level_add( doing, carry_left_not_avail );
		reiser4_pool_free( &left -> header );
		left = NULL;
	} else if( doing -> restartable ) {
		/*
		 * if left neighbor is not in a cache or is locked,
		 * and level is restartable, add new node to @doing
		 * and restart.
		 */
		assert( "nikita-913", node -> parent != 0 );
		assert( "nikita-914", node -> node != NULL );
		left -> left = 1;
		left -> free = 0;
		left = ERR_PTR( -EAGAIN );
	} else {
		/*
		 * left neighbor is not in a cache or locked, level cannot be
		 * restarted. Just ignore left neighbor.
		 */
		reiser4_pool_free( &left -> header );
		left = NULL;
		reiser4_stat_level_add( doing, carry_left_refuse );
	}
	return left;
}

/**
 * find right neighbor of a carry node
 *
 * Look for right neighbor of @node and add it to the @doing queue. See
 * comments in the body.
 *
 */
static carry_node *find_right_neighbor( carry_node *node /* node to find right
							  * neighbor of */, 
					carry_level *doing /* level to scan */ )
{
	int         result;
	carry_node *right;
	lock_handle lh;

	init_lh( &lh );

	/* first, check whether right neighbor is already in a @doing queue */
	right = find_right_carry( node, doing );
	/*
	 * if we are not at the right end of level, check whether right
	 * neighbor was found.
	 */
	if( right != NULL ) {
		spin_lock_tree( current_tree );
		if( node -> real_node -> right != right -> real_node ) {
			right = NULL;
		}
		spin_unlock_tree( current_tree );
		if( right != NULL ) {
			reiser4_stat_level_add( doing, carry_right_in_carry );
			return right;
		}
	}

	/* then, try to lock right neighbor */
	init_lh( &lh );
	result = reiser4_get_right_neighbor( &lh, node -> real_node,
					     ZNODE_WRITE_LOCK, GN_DO_READ );
	if( result == 0 ) {
		/* ok, node found and locked. */
		reiser4_stat_level_add( doing, carry_right_in_cache );
		right = add_carry_skip( doing, POOLO_AFTER, node );
		if( !IS_ERR( right ) ) {
			right -> node = lh.node;
			move_lh( &right -> lock_handle, &lh );
			right -> free = 1;
			result = lock_carry_node_tail( right );
			if( result != 0 )
				right = ERR_PTR( result );
		} else
			done_lh( &lh );
	} else if( result == -ENAVAIL ) {
			/*
			 * node is rightmost node in a tree, or there is an
			 * extent on the right.
			 */
			right = NULL;
			reiser4_stat_level_add( doing, carry_right_not_avail );
	} else
			right = ERR_PTR( result );
	done_lh( &lh );
	return right;
}

/**
 * how much free space in a @node is needed for @op
 *
 * How much space in @node is required for completion of @op, where @op is
 * insert or paste operation.
 */
static unsigned int space_needed_for_op( znode *node /* znode data are
						      * inserted or
						      * pasted in */, 
					 carry_op *op /* carry
							 operation */ )
{
	assert( "nikita-919", op != NULL );

	switch( op -> op ) {
	default:
		impossible( "nikita-1701", "Wrong opcode" );
	case COP_INSERT:
		return space_needed( node, NULL, op -> u.insert.d -> data, 1 );
	case COP_PASTE:
		return space_needed( node, 
				     op -> u.insert.d -> coord, 
				     op -> u.insert.d -> data, 0 );
	}
}

/**
 * how much space in @node is required to insert or paste @data at
 * @coord.
 */
unsigned int space_needed( const znode *node /* node data are inserted or
					      * pasted in */, 
			   const coord_t *coord /* coord where data are
						     * inserted or pasted
						     * at */,
			   const reiser4_item_data *data /* data to insert or
							  * paste */, 
			   int insertion /* non-0 is inserting, 0---paste */ )
{
	int         result;
       item_plugin *iplug;

	assert( "nikita-917", node != NULL );
	assert( "nikita-918", node_plugin_by_node( node ) != NULL );
	assert( "vs-230", !insertion || ( coord == NULL ) );

	result = 0;
	iplug = data -> iplug;
	if( iplug -> common.estimate != NULL ) {
		/*
		 * ask item plugin how much space is needed to insert this
		 * item
		 */
		result += iplug -> common.estimate ( insertion ? NULL : coord, data );
	} else {
		/* reasonable default */
		result += data -> length;
	}
	if( insertion ) {
		node_plugin *nplug;
		
		nplug = node -> nplug;
		/* and add node overhead */
		if( nplug -> item_overhead != NULL ) {
			result += nplug -> item_overhead( node, 0 );
		}
	}
	return result;
}

/* find &coord in parent where pointer to new child is to be stored. */
static int find_new_child_coord( carry_op *op /* COP_INSERT carry operation to
					       * insert pointer to new
					       * child */ )
{
	int    result;
	znode *node;
	znode *child;

	assert( "nikita-941", op != NULL );
	assert( "nikita-942", op -> op == COP_INSERT );

	trace_stamp( TRACE_CARRY );

	node = op -> node -> real_node;
	assert( "nikita-943", node != NULL );
	assert( "nikita-944", node_plugin_by_node( node ) != NULL );

	child = op -> u.insert.child -> real_node;
	result = find_new_child_ptr( node, child, op -> u.insert.brother,
				     op -> u.insert.d -> coord );

	build_child_ptr_data( child, op -> u.insert.d -> data );
	return result;
}

/* additional amount of free space in @node required to complete @op */
static int free_space_shortage( znode *node /* node to check */, 
				carry_op *op /* operation being performed */ )
{
	assert( "nikita-1061", node != NULL );
	assert( "nikita-1062", op != NULL );

	switch( op -> op ) {
	default:
		impossible( "nikita-1702", "Wrong opcode" );
	case COP_INSERT:
	case COP_PASTE:
		return space_needed_for_op( node, op ) - znode_free_space( node );
	case COP_EXTENT:
		/*
		 * when inserting extent shift data around until insertion
		 * point is utmost in the node.
		 */
		if( coord_wrt( op -> u.insert.d -> coord ) == COORD_INSIDE )
			return +1;
		else 
			return -1;
	}
}

/**
 * This is insertion policy function. It shifts data to the left and right
 * neighbors of insertion coord and allocates new nodes until there is enough
 * free space to complete @op.
 *
 * Follows logic of fs/reiser4/tree.c:insert_single_item()
 *
 * See comments in the body.

Assumes that the node format favors insertions at the right end of the node as node40 does. 

GREEN-FIXME-HANS: why isn't this code audited?

NIKITA-FIXME-HANS: can this be broken into subfunctions that will look like the following except with function arguments:

if(!enough_space())
    shift_insert_point_to_left;
if(!enough_space())
    shift_after_insert_point_to_right();
if(!enough_space()){
    insert_node_after_insert_point();
    if(!insert_point_at_node_end())
	shift_after_insert_point_to_right();
        insert_node_after_insert_point();// avoids pushing detritus around when repeated insertions occur
}


 */
static int make_space( carry_op *op /* carry operation, insert or paste */, 
		       carry_level *doing /* current carry queue */, 
		       carry_level *todo /* carry queue on the parent level */ )
{
	znode *node;
	int    result;
	int    not_enough_space;
	int    blk_alloc;
	znode *orig_node;

	carry_node *tracking;


	assert( "nikita-890", op != NULL );
	assert( "nikita-891", todo != NULL );
	assert( "nikita-892", op -> op == COP_INSERT || op -> op == COP_PASTE ||
		op -> op == COP_EXTENT );
	assert( "nikita-1607", 
		op -> node -> real_node == op -> u.insert.d -> coord -> node );

	trace_stamp( TRACE_CARRY );

	orig_node = node = op -> u.insert.d -> coord -> node;
	tracking  = op -> node;

	assert( "nikita-908", node != NULL );
	assert( "nikita-909", node_plugin_by_node( node ) != NULL );

	result = 0;
	/*
	 * If there is not enough space in a node, try to shift something to
	 * the left neighbor. This is a bit tricky, as locking to the left is
	 * low priority. This is handled by restart logic in carry().
	 */
	not_enough_space = free_space_shortage( node, op );
	if( not_enough_space <= 0 )
		/*
		 * it is possible that carry was called when there actually
		 * was enough space in the node. For example, when inserting
		 * leftmost item so that delimiting keys have to be updated.
		 */
		return 0;	
	if( !( op -> u.insert.flags & COPI_DONT_SHIFT_LEFT ) ) {
		carry_node *left;
		/* 
		 * make note in statistics of an attempt to move
		 * something into the left neighbor
		 */
		reiser4_stat_level_add( doing, insert_looking_left );
		left = find_left_neighbor( op -> node, doing );
		if( unlikely( IS_ERR( left ) ) ) {
			if( PTR_ERR( left ) == -EAGAIN )
				return -EAGAIN;
			else {
				/*
				 * some error other than restart request
				 * occurred. This shouldn't happen. Issue a
				 * warning and continue as if left neighbor
				 * weren't existing.
				 */
				warning( "nikita-924",
					 "Error accessing left neighbor: %li",
					 PTR_ERR( left ) );
				print_znode( "node", node );
			}
		} else if( left != NULL ) {
			/*
			 * shift everything possible on the left of and
			 * including insertion coord into the left neighbor
			 */
			result = carry_shift_data( LEFT_SIDE, 
						   op -> u.insert.d -> coord,
						   left -> real_node, 
						   doing, todo, 0 );
			/*
			 * reget node from coord: shift_left() might move
			 * insertion coord to the left neighbor
			 */
			node = op -> u.insert.d -> coord -> node;
			not_enough_space = free_space_shortage( node, op );
			/*
			 * There is not enough free space in @node, but
			 * may be, there is enough free space in
			 * @left. Various balancing decisions are valid here.
			 * The same for the shifiting to the right.
			 */
		}
	}
	/* If there still is not enough space, shift to the right */
	if( ( not_enough_space > 0 ) &&
	    !( op -> u.insert.flags & COPI_DONT_SHIFT_RIGHT ) ) {
		carry_node *right;

		reiser4_stat_level_add( doing, insert_looking_right );
		right = find_right_neighbor( op -> node, doing );
		if( IS_ERR( right ) ) {
			warning( "nikita-1065", 
				 "Error accessing right neighbor: %li",
				 PTR_ERR( right ) );
			print_znode( "node", node );
		} else if( right != NULL ) {
			/*
			 * both nodes are write locked by now.
			 *
			 * shift everything possible on the right of but
			 * excluding insertion coord into the right neighbor
			 */
			result = carry_shift_data( RIGHT_SIDE, 
						   op -> u.insert.d -> coord,
						   right -> real_node, 
						   doing, todo, 0 );
			/*
			 * reget node from coord: shift_right() might move
			 * insertion coord to the right neighbor
			 */
			node = op -> u.insert.d -> coord -> node;
			not_enough_space = free_space_shortage( node, op );
		}
	}
	/* 
	 * If there is still not enough space, allocate new node(s). 
	 *
	 * We try to allocate new blocks if COPI_DONT_ALLOCATE is not set in
	 * the carry operation flags (currently this is needed during flush
	 * only).
	 */
	for( blk_alloc = 0 ; 
	     ( not_enough_space > 0 ) && ( result == 0 ) && ( blk_alloc < 2 ) &&
	     !( op -> u.insert.flags & COPI_DONT_ALLOCATE ) ; ++ blk_alloc ) {
		carry_node *fresh; /* new code we are allocating */
		coord_t coord_shadow; /* remembered insertion point before
				       * shifting into data into new node */
		carry_node *node_shadow; /* remembered insertion node before
					  * shifting */

		reiser4_stat_level_add( doing, insert_alloc_new );
		if( blk_alloc > 0 )
			reiser4_stat_level_add( doing, insert_alloc_many );

		/*
		 * allocate new node on the right of @node. Znode and disk
		 * fake block number for new node are allocated.
		 *
		 * add_new_znode() posts carry operation COP_INSERT with
		 * COPT_CHILD option to the parent level to add
		 * pointer to newly created node to its parent.
		 *
		 * Subtle point: if several new nodes are required to complete
		 * insertion operation at this level, they will be inserted
		 * into their parents in the order of creation, which means
		 * that @node will be valid "cookie" at the time of insertion.
		 *
		 */
		fresh = add_new_znode( node, op -> node, doing, todo );
		if( IS_ERR( fresh ) )
			return PTR_ERR( fresh );

		/* Try to shift into new node. */
		result = lock_carry_node( doing, fresh );
		zput( fresh -> real_node );
		if( result != 0 ) {
			warning( "nikita-947",
				 "Cannot lock new node: %i", result );
			print_znode( "new", fresh -> real_node );
			print_znode( "node", node );
			return result;
		}

		/*
		 * both nodes are write locked by now.
		 *
		 * shift everything possible on the right of and
		 * including insertion coord into the right neighbor.
		 */
		coord_dup( &coord_shadow, op -> u.insert.d -> coord );
		node_shadow = op -> node;
		result = carry_shift_data( RIGHT_SIDE, op -> u.insert.d -> coord,
					   fresh -> real_node, doing, todo, 1 );
		node = op -> u.insert.d -> coord -> node;
		/*
		 * if insertion point was actually moved into new node,
		 * update carry node pointer in operation.
		 */
		if( node != op -> node -> real_node )
			op -> node = fresh;
		not_enough_space = free_space_shortage( node, op );
		if( ( not_enough_space > 0 ) && ( node != coord_shadow.node ) ) {
			/* 
			 * still no enough space?! Maybe there is enough space
			 * in the source node (i.e., node data are moved from)
			 * now.
			 */
			coord_normalize( &coord_shadow );
			coord_dup( op -> u.insert.d -> coord, &coord_shadow );
			node = op -> u.insert.d -> coord -> node;
			op -> node = node_shadow;
			not_enough_space = free_space_shortage( node, op );
		}
	}
	if( not_enough_space > 0 ) {
		if( !( op -> u.insert.flags & COPI_DONT_ALLOCATE ) )
			warning( "nikita-948", "Cannot insert new item" );
		result = -ENOSPC;
	}
	if( ( result == 0 ) && ( node != orig_node ) && tracking -> track ) {
		/*
		 * inserting or pasting into node different from
		 * original. Update lock handle supplied by caller.
		 */
		assert( "nikita-1417", tracking -> tracked != NULL );
		done_lh( tracking -> tracked );
		init_lh( tracking -> tracked );
		result = longterm_lock_znode( tracking -> tracked, node, 
					      ZNODE_WRITE_LOCK, 
					      ZNODE_LOCK_HIPRI );
		reiser4_stat_level_add( doing, track_lh );
	}
	assert( "nikita-1622", ergo( result == 0, op -> node -> real_node == op -> u.insert.d -> coord -> node ) );
	return result;
}

/**
 * insert_paste_common() - common part of insert and paste operations
 *
 * This function performs common part of COP_INSERT and COP_PASTE.
 *
 * There are two ways in which insertion/paste can be requested: 
 *
 *  . by directly supplying reiser4_item_data. In this case, op ->
 *  u.insert.type is set to COPT_ITEM_DATA.
 *
 *  . by supplying child pointer to which is to inserted into parent. In this
 *  case op -> u.insert.type == COPT_CHILD.
 *
 *  . by supplying key of new item/unit. This is currently only used during
 *  extent insertion
 *
 * This is required, because when new node is allocated we don't know at what
 * position pointer to it is to be stored in the parent. Actually, we don't
 * even know what its parent will be, because parent can be re-balanced
 * concurrently and new node re-parented, and because parent can be full and
 * pointer to the new node will go into some other node.
 *
 * insert_paste_common() resolves pointer to child node into position in the
 * parent by calling find_new_child_coord(), that fills
 * reiser4_item_data. After this, insertion/paste proceeds uniformly.
 *
 * Another complication is with finding free space during pasting. It may
 * happen that while shifting items to the neighbors and newly allocated
 * nodes, insertion coord can no longer be in the item we wanted to paste
 * into. At this point, paste becomes (morphs) into insert. Moreover free
 * space analysis has to be repeated, because amount of space required for
 * insertion is different from that of paste (item header overhead, etc).
 *
 * This function "unifies" different insertion modes (by resolving child
 * pointer or key into insertion coord), and then calls make_space() to free
 * enough space in the node by shifting data to the left and right and by
 * allocating new nodes if necessary. Carry operation knows amount of space
 * required for its completion. After enough free space is obtained, caller of
 * this function (carry_{insert,paste,etc.}) performs actual insertion/paste
 * by calling item plugin method.
 *
 */
static int insert_paste_common( carry_op *op /* carry operation being
					      * performed */, 
				carry_level *doing /* current carry level */, 
				carry_level *todo /* next carry level */,
				carry_insert_data *cdata /* pointer to
							  * cdata */,
				coord_t *coord /* insertion/paste coord */, 
				reiser4_item_data *data /* data to be
							 * inserted/pasted */ )
{
	assert( "nikita-981", op != NULL );
	assert( "nikita-980", todo != NULL );
	assert( "nikita-979",
		( op -> op == COP_INSERT ) || ( op -> op == COP_PASTE ) ||
		( op -> op == COP_EXTENT ) );

	trace_stamp( TRACE_CARRY );

	if( op -> u.insert.type == COPT_PASTE_RESTARTED ) {
		/* nothing to do. Fall through to make_space(). */
		;
	} else if( op -> u.insert.type == COPT_KEY ) {
		node_search_result  intra_node;
		znode              *node;
		/*
		 * Problem with doing batching at the lowest level, is that
		 * operations here are given by coords where modification is
		 * to be performed, and one modification can invalidate coords
		 * of all following operations.
		 *
		 * So, we are implementing yet another type for operation that
		 * will use (the only) "locator" stable across shifting of
		 * data between nodes, etc.: key (COPT_KEY).
		 *
		 * This clause resolves key to the coord in the node.
		 *
		 * But node can change also. Probably some pieces have to be
		 * added to the lock_carry_node(), to lock node by its key.
		 *
		 */
		/*
		 * FIXME-NIKITA Lookup bias is fixed to FIND_EXACT. Complain
		 * if you need something else.
		 */
		op -> u.insert.d -> coord = coord;
		node = op -> node -> real_node;
		intra_node = node_plugin_by_node( node ) -> lookup
			( node, op -> u.insert.d -> key, FIND_EXACT, 
			  op -> u.insert.d -> coord );
		if( ( intra_node != NS_FOUND ) && 
		    ( intra_node != NS_NOT_FOUND ) ) {
			warning( "nikita-1715", "Intra node lookup failure: %i",
				 intra_node );
			print_znode( "node", node );
			return intra_node;
		}
	} else if( op -> u.insert.type == COPT_CHILD ) {
		/*
		 * if we are asked to insert pointer to the child into
		 * internal node, first convert pointer to the child into
		 * coord within parent node.
		 */
		znode *child;
		int    result;

		op -> u.insert.d = cdata;
		op -> u.insert.d -> coord = coord;
		op -> u.insert.d -> data  = data;
		op -> u.insert.d -> coord -> node = op -> node -> real_node;
		result = find_new_child_coord( op );
		child = op -> u.insert.child -> real_node;
		if( result != NS_NOT_FOUND ) {
			warning( "nikita-993",
				 "Cannot find a place for child pointer: %i",
				 result );
			print_znode( "child", child );
			print_znode( "parent", op -> node -> real_node );
			return result;
		}
		/*
		 * This only happens when we did multiple insertions at
		 * the previous level, trying to insert single item and
		 * it so happened, that insertion of pointers to all new
		 * nodes before this one already caused parent node to
		 * split (may be several times).
		 *
		 * I am going to come up with better solution.
		 *
		 * You are not expected to understand this.
		 *        -- v6root/usr/sys/ken/slp.c
		 */
		if( op -> node -> real_node != op -> u.insert.d -> coord -> node ) {
			op -> node = add_carry_skip( doing, POOLO_AFTER, 
						     op -> node );
			if( IS_ERR( op -> node ) )
				return PTR_ERR( op -> node );
			op -> node -> node = op -> u.insert.d -> coord -> node;
			op -> node -> free = 1;
			result = lock_carry_node( doing, op -> node );
			if( result != 0 )
				return result;
		}

		spin_lock_dk( current_tree );
		op -> u.insert.d -> key = 
			leftmost_key_in_node( child, znode_get_ld_key( child ) );
		op -> u.insert.d -> data -> arg = op -> u.insert.brother;
		spin_unlock_dk( current_tree );
	} else {
		assert( "vs-243", op -> u.insert.d -> coord != NULL );
		op -> u.insert.d -> coord -> node = op -> node -> real_node;
	}


	/* find free space. */
	return make_space( op, doing, todo );
}

/**
 * handle carry COP_INSERT operation.
 *
 * Insert new item into node. New item can be given in one of two ways:
 *
 * - by passing &tree_coord and &reiser4_item_data as part of @op. This is
 * only applicable at the leaf/twig level.
 *
 * - by passing a child node pointer to which is to be inserted by this
 * operation.
 *
 */
static int carry_insert( carry_op *op /* operation to perform */, 
			 carry_level *doing /* queue of operations @op
					     * is part of */, 
			 carry_level *todo /* queue where new operations
					    * are accumulated */ )
{
	znode            *node;
	carry_insert_data cdata;
	coord_t           coord;
	reiser4_item_data data;
	carry_plugin_info info;
	int               result;

	assert( "nikita-1036", op != NULL );
	assert( "nikita-1037", todo != NULL );
	assert( "nikita-1038", op -> op == COP_INSERT );

	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, insert );

	/*
	 * FIXME-VS: init_coord used to be here. insert_paste_common seems to
	 * use that zeroed coord
	 */
	coord_init_zero (&coord);

	/* perform common functionality of insert and paste. */
	result = insert_paste_common( op, doing, todo, &cdata, &coord, &data );
	if( result != 0 )
		return result;

	node = op -> u.insert.d -> coord -> node;
	assert( "nikita-1039", node != NULL );
	assert( "nikita-1040", node_plugin_by_node( node ) != NULL );

	assert( "nikita-949",
		space_needed_for_op( node, op ) <= znode_free_space( node ) );

	/* ask node layout to create new item. */
	info.doing  = doing;
	info.todo   = todo;
	result = node_plugin_by_node( node ) -> create_item
		( op -> u.insert.d -> coord, op -> u.insert.d -> key,
		  op -> u.insert.d -> data, &info );
	doing -> restartable = 0;
	znode_set_dirty( node );

	return result;
}

/**
 * implements COP_DELETE opration
 *
 * Remove pointer to @op -> u.delete.child from it's parent.
 *
 * This function also handles killing of a tree root is last pointer from it
 * was removed. This is complicated by our handling of "twig" level: root on
 * twig level is never killed.
 *
 */
static int carry_delete( carry_op *op /* operation to be performed */, 
			 carry_level *doing UNUSED_ARG /* current carry
							* level */, 
			 carry_level *todo /* next carry level */ )
{
	int         result;
	coord_t     coord;
	coord_t     coord2;
	znode      *parent;
	znode      *child;
	carry_plugin_info info;

	assert( "nikita-893", op != NULL );
	assert( "nikita-894", todo != NULL );
	assert( "nikita-895", op -> op == COP_DELETE );
	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, delete );

	coord_init_zero( &coord );
	coord_init_zero( &coord2 );

	parent = op -> node -> real_node;
	child  = op -> u.delete.child ?
		op -> u.delete.child -> real_node : op -> node -> node;

	assert( "nikita-1213", znode_get_level( parent ) > LEAF_LEVEL );

	/*
	 * Twig level horrors: tree should be of height at least 2. So, last
	 * pointer from the root at twig level is preserved even if child is
	 * empty. This is ugly, but so it was architectured.
	 */

	if( znode_is_root( parent ) && 
	    ( znode_get_level( parent ) <= REISER4_MIN_TREE_HEIGHT ) &&
	    ( node_num_items( parent ) == 1 ) ) {
		/* Delimiting key manipulations. */
		spin_lock_dk( current_tree );
		*znode_get_ld_key( child ) = *znode_get_ld_key( parent ) = 
			*min_key();
		*znode_get_rd_key( child ) = *znode_get_rd_key( parent ) = 
			*max_key();
		spin_unlock_dk( current_tree );
		return 0;
	}

	/* convert child pointer to the tree_coord */
	result = find_child_ptr( parent, child, &coord );
	if( result != NS_FOUND ) {
		warning( "nikita-994", "Cannot find child pointer: %i", result );
		print_znode( "child", child );
		print_znode( "parent", parent );
		print_coord_content( "coord", &coord );
		return result;
	}

	coord_dup( &coord2, &coord );
	info.doing  = doing;
	info.todo   = todo;
	result = node_plugin_by_node( parent ) -> cut_and_kill
		( &coord, &coord2, NULL, NULL, NULL, &info, 
		  NULL, op -> u.delete.flags );
	doing -> restartable = 0;
	znode_set_dirty( coord.node );
	znode_set_dirty( coord2.node );
	/* check whether root should be killed violently */
	if( znode_is_root( parent ) &&
	    /* don't kill roots at and lower than twig level */
	    ( znode_get_level( parent ) > REISER4_MIN_TREE_HEIGHT ) &&
	    ( node_num_items( parent ) == 1 ) ) {
		result = kill_tree_root( coord.node );
	}

	return result < 0 ? : 0 ;
}

/**
 * implements COP_CUT opration
 *
 * Cuts part or whole content of node.
 *
 */
static int carry_cut( carry_op *op /* operation to be performed */, 
		      carry_level *doing UNUSED_ARG /* current carry
						     * level */, 
		      carry_level *todo /* next carry level */ )
{
	int result;
	carry_plugin_info info;

	assert( "nikita-896", op != NULL );
	assert( "nikita-897", todo != NULL );
	assert( "nikita-898", op -> op == COP_CUT );
	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, cut );

	info.doing  = doing;
	info.todo   = todo;
	if( op -> u.cut -> flags & DELETE_KILL )
		/* data gets removed from the tree */
		result = node_plugin_by_node( op -> node -> real_node ) ->
			cut_and_kill( op -> u.cut -> from, op -> u.cut -> to,
				      op -> u.cut -> from_key, op -> u.cut -> to_key,
				      op -> u.cut -> smallest_removed,
				      &info, op -> u.cut -> iplug_params, 
				      0 /* FIXME-NIKITA flags */ );
	else
		/* data get cut,  */
		result = node_plugin_by_node( op -> node -> real_node ) ->
			cut( op -> u.cut -> from, op -> u.cut -> to,
			     op -> u.cut -> from_key, op -> u.cut -> to_key,
			     op -> u.cut -> smallest_removed,
			     &info, 0 /* FIXME-NIKITA flags */ );
	znode_set_dirty( op -> u.cut -> from -> node );
	znode_set_dirty( op -> u.cut -> to -> node );
	doing -> restartable = 0;
	return result < 0 ? : 0 ;
}

/**
 * implements COP_PASTE operation
 *
 * Paste data into existing item. This is complicated by the fact that after
 * we shifted something to the left or right neighbors trying to free some
 * space, item we were supposed to paste into can be in different node than
 * insertion coord. If so, we are no longer doing paste, but insert. See
 * comments in insert_paste_common().
 *
 */
static int carry_paste( carry_op *op /* operation to be performed */, 
			carry_level *doing UNUSED_ARG /* current carry
						       * level */, 
			carry_level *todo /* next carry level */ )
{
	znode               *node;
	carry_insert_data    cdata;
	coord_t           coord;
	reiser4_item_data    data;
	int                  result;
	int                  real_size;
	item_plugin         *iplug;
	int                  can_paste_here;
	carry_plugin_info    info;

	assert( "nikita-982", op != NULL );
	assert( "nikita-983", todo != NULL );
	assert( "nikita-984", op -> op == COP_PASTE );

	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, paste );

	coord_init_zero( &coord );

	result = insert_paste_common( op, doing, todo, &cdata, &coord, &data );
	if( result != 0 )
		return result;

	/*
	 * handle case when op -> u.insert.coord doesn't point to the item
	 * of required type. restart as insert.
	 */
	iplug = coord_is_existing_item( op -> u.insert.d -> coord ) ? 
		item_plugin_by_coord( op -> u.insert.d -> coord ) : NULL;
	can_paste_here = 
		coord_is_existing_item( op -> u.insert.d -> coord ) &&
		( iplug == op -> u.insert.d -> data -> iplug );

	if( !can_paste_here || 
	    ( can_paste_here &&
	      !item_can_contain_key( op -> u.insert.d -> coord, 
				     op -> u.insert.d -> key,
				     op -> u.insert.d -> data ) ) ) {
		op -> op = COP_INSERT;
		op -> u.insert.type = COPT_PASTE_RESTARTED;
		reiser4_stat_level_add( doing, paste_restarted );
		result = op_dispatch_table[ COP_INSERT ].handler( op, 
								  doing, todo );

		return result;
	}

	node = op -> u.insert.d -> coord -> node;
	assert( "nikita-985", node != NULL );
	assert( "nikita-986", node_plugin_by_node( node ) != NULL );

	assert( "nikita-987",
		space_needed_for_op( node, op ) <= znode_free_space( node ) );

	assert( "nikita-1286", coord_is_existing_item( op -> u.insert.d -> coord ) );

	assert( "nikita-992", iplug != NULL );
	real_size = space_needed_for_op( node, op );
	if( real_size > 0 ) {
		node -> nplug ->
			change_item_size( op -> u.insert.d -> coord, real_size );
	}
	doing -> restartable = 0;
	info.doing  = doing;
	info.todo   = todo;
	result = iplug -> common.paste( op -> u.insert.d -> coord,
					op -> u.insert.d -> data, &info );
	znode_set_dirty( node );
	if( real_size < 0 ) {
		node -> nplug ->
			change_item_size( op -> u.insert.d -> coord, 
					  real_size );
	}
	/* if we pasted at the beginning of the item, update item's key. */
	if( ( op -> u.insert.d -> coord -> unit_pos == 0 ) &&
	    ( op -> u.insert.d -> coord -> between != AFTER_UNIT ) ) {
		reiser4_key item_key;

		unit_key_by_coord( op -> u.insert.d -> coord, &item_key );
		node -> nplug -> update_item_key 
			( op -> u.insert.d -> coord, &item_key, &info );
	}

	return result;
}

/* handle carry COP_EXTENT operation. */
static int carry_extent( carry_op *op /* operation to perform */, 
			 carry_level *doing /* queue of operations @op
					     * is part of */, 
			 carry_level *todo /* queue where new operations
					    * are accumulated */ )
{
	znode            *node;
	carry_insert_data cdata;
	coord_t        coord;
	reiser4_item_data data;
	carry_op         *delete_dummy;
	carry_op         *insert_extent;
	int               result;
	carry_plugin_info info;

	assert( "nikita-1751", op != NULL );
	assert( "nikita-1752", todo != NULL );
	assert( "nikita-1753", op -> op == COP_EXTENT );

	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, extent );

	/*
	 * extent insertion overview:
	 *
	 * extents live on the TWIG LEVEL, which is level one above the leaf
	 * one. This complicates extent insertion logic somewhat: it may
	 * happen (and going to happen all the time) that in logical key
	 * ordering extent has to be placed between items I1 and I2, located
	 * at the leaf level, but I1 and I2 are in the same formatted leaf
	 * node N1. To insert extent one has to 
	 *
	 *  (1) reach node N1 and shift data between N1, its neighbors and
	 *  possibly newly allocated nodes until I1 and I2 fall into different
	 *  nodes. Since I1 and I2 are still neighboring items in logical key
	 *  order, they will be necessary utmost items in their respective
	 *  nodes.
	 *
	 *  (2) After this new extent item is inserted into node on the twig
	 *  level.
	 *
	 * Fortunately this process can reuse almost all code from standard
	 * insertion procedure (viz. make_space() and insert_paste_common()),
	 * due to the following observation: make_space() only shifts data up
	 * to and excluding or including insertion point. It never
	 * "over-moves" through insertion point. Thus, one can use
	 * make_space() to perform step (1). All required for this is just to
	 * instruct free_space_shortage() to keep make_space() shifting data
	 * until insertion point is at the node border.
	 *
	 */

	/* perform common functionality of insert and paste. */
	result = insert_paste_common( op, doing, todo, &cdata, &coord, &data );
	if( result != 0 )
		return result;

	node = op -> u.extent.d -> coord -> node;
	assert( "nikita-1754", node != NULL );
	assert( "nikita-1755", node_plugin_by_node( node ) != NULL );
	assert( "nikita-1700", 
		coord_wrt( op -> u.extent.d -> coord ) != COORD_INSIDE );
	/*
	 * FIXME-NIKITA add some checks here. Not assertions, -EIO. Check that
	 * extent fits between items.
	 */

	info.doing = doing;
	info.todo  = todo;

	/*
	 * there is another complication due to placement of extents on the
	 * twig level: extents are "rigid" in the sense that key-range
	 * occupied by extent cannot grow indefinitely to the right as it is
	 * for the formatted leaf nodes. Because of this when search finds two
	 * adjacent extents on the twig level, it has to "drill" to the leaf
	 * level, creating new node. Here we are removing this node.
	 */
	if( node_is_empty( node ) ) {
		delete_dummy = node_post_carry( &info, COP_DELETE, node, 1 );
		if( IS_ERR( delete_dummy ) )
			return PTR_ERR( delete_dummy );
		delete_dummy -> u.delete.child = NULL;
		delete_dummy -> u.delete.flags = DELETE_RETAIN_EMPTY;
	}

	/*
	 * proceed with inserting extent item into parent. We are definitely
	 * inserting rather than pasting if we get that far.
	 */
	insert_extent = node_post_carry( &info, COP_INSERT, node, 1 );
	if( IS_ERR( insert_extent ) )
		/* FIXME-NIKITA cleanup @delete_dummy */
		return PTR_ERR( insert_extent );
	/*
	 * FIXME-NIKITA insertion by key is simplest option here. Another
	 * possibility is to insert on the left or right of already existing
	 * item.
	 */
	insert_extent -> u.insert.type = COPT_KEY;
	insert_extent -> u.insert.d = op -> u.extent.d;
	assert( "nikita-1719", op -> u.extent.d -> key != NULL );
	insert_extent -> u.insert.d -> data -> arg = op -> u.extent.d -> coord;
		
	return 0;
}

/**
 * update key in @parent between pointers to @left and @right.
 *
 * Find coords of @left and @right and update delimiting key between them.
 * 
 */
static int update_delimiting_key( znode *parent /* node key is updated
						 * in */,
				  znode *left /* child of @parent */, 
				  znode *right /* child of @parent */,
				  carry_level *doing /* current carry
						      * level */, 
				  carry_level *todo /* parent carry
						     * level */,
				  const char **error_msg /* place to
							  * store error
							  * message */)
{
	coord_t      left_pos;
	coord_t      right_pos;
	int          result;
	reiser4_key  ldkey;
	carry_plugin_info info;

	assert( "nikita-1177", right != NULL );
	/* find position of right left child in a parent */
	result = find_child_ptr( parent, right, &right_pos );
	if( result != NS_FOUND ) {
		*error_msg = "Cannot find position of right child";
		return result;
	}

	if( ( left != NULL ) && !coord_is_leftmost_unit( &right_pos ) ) {
		/* find position of the left child in a parent */
		result = find_child_ptr( parent, left, &left_pos );
		if( result != NS_FOUND ) {
			*error_msg = "Cannot find position of left child";
			return result;
		}
		assert( "nikita-1355", left_pos.node != NULL );
	} else
		left_pos.node = NULL;

	/*
	 * check that they are separated by exactly one key and are basically
	 * sane
	 */
	if( REISER4_DEBUG ) {
		if( ( left_pos.node != NULL ) && !coord_is_existing_unit( &left_pos ) ) {
			*error_msg = "Left child is bastard";
			return -EIO;
		}
		if( !coord_is_existing_unit( &right_pos ) ) {
			*error_msg = "Right child is bastard";
			return -EIO;
		}
		if( ( left_pos.node != NULL ) && 
		    !coord_are_neighbors( &left_pos, &right_pos ) ) {
			*error_msg = "Children are not direct siblings";
			return -EIO;
		}
	}
	*error_msg = NULL;

	info.doing  = doing;
	info.todo   = todo;
	node_plugin_by_node( parent ) -> update_item_key
		( &right_pos, leftmost_key_in_node( right, &ldkey ), &info );
	doing -> restartable = 0;
	znode_set_dirty( parent );
	return 0;
}


/**
 * implements COP_UPDATE opration
 *
 * Update delimiting keys.
 *
 */
static int carry_update( carry_op *op /* operation to be performed */, 
			 carry_level *doing /* current carry level */, 
			 carry_level *todo /* next curry level */ )
{
	int         result;
	carry_node *missing UNUSED_ARG;
	znode      *left;
	znode      *right;
	carry_node *lchild;
	carry_node *rchild;
	const char *error_msg;

	assert( "nikita-902", op != NULL );
	assert( "nikita-903", todo != NULL );
	assert( "nikita-904", op -> op == COP_UPDATE );
	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, update );

	lchild = op -> u.update.left;
	rchild = op -> node;

	if( rchild != NULL ) {
		assert( "nikita-1000", rchild -> parent );
		assert( "nikita-1002", !rchild -> left );
		right = rchild -> real_node;
	} else
		right = NULL;
	if( lchild != NULL ) {
		assert( "nikita-1001", lchild -> parent );
		assert( "nikita-1003", !lchild -> left );
		left = lchild -> real_node;
	} else
		left = NULL;

	spin_lock_tree( current_tree );
	assert( "nikita-1190", znode_parent( rchild -> node ) != NULL );
	if( znode_parent( rchild -> node ) != right ) {
		/*
		 * parent node was split, and pointer to @rchild was
		 * inserted/moved into new node. Wonders of balkancing (sic.).
		 */
		reiser4_stat_level_add( doing, half_split_race );
		right = znode_parent( rchild -> node );
	}
	spin_unlock_tree( current_tree );

	if( right == NULL ) {
		error_msg = "Cannot find node to update key in";
		return -EIO;	
	}
	
	result = update_delimiting_key( right, 
					lchild ? lchild -> node : NULL,
					rchild -> node,
					doing, todo, &error_msg );

	/*
	 * operation will be reposted to the next level by the
	 * ->update_item_key() method of node plugin, if necessary.
	 */

	if( result != 0 ) {
		warning( "nikita-999",
			 "Error updating delimiting key: %s (%i)",
			 error_msg ? : "", result );
		print_znode( "left", left );
		print_znode( "right", right );
		print_znode( "lchild", lchild ? lchild -> node : NULL );
		print_znode( "rchild", rchild -> node );
	}
	return result;
}

/**
 * implements COP_MODIFY opration
 *
 * Notify parent about changes in its child
 *
 */
static int carry_modify( carry_op *op /* operation to be performed */, 
			 carry_level *doing UNUSED_ARG /* current carry
							* level */, 
			 carry_level *todo UNUSED_ARG /* next curry level */ )
{
	znode *node;

	assert( "nikita-905", op != NULL );
	assert( "nikita-906", todo != NULL );
	assert( "nikita-907", op -> op == COP_MODIFY );
	trace_stamp( TRACE_CARRY );
	reiser4_stat_level_add( doing, modify );

	node = op -> node -> real_node;
	assert( "nikita-995", node != NULL );
#ifdef MODIFY_EXISTS
	if( node_plugin_by_node( node ) -> modify != NULL )
		return node_plugin_by_node( node ) -> modify
			( node, op -> u.modify.child -> real_node,
			  op -> u.modify.flag, todo );
	else
#endif
		return 0;
}

/* move items from @node during carry */
static int carry_shift_data( sideof side /* in what direction to move data */, 
			     coord_t *insert_coord /* coord where new item
							* is to be inserted */, 
			     znode *node /* node which data are moved from */,
			     carry_level *doing /* active carry queue */, 
			     carry_level *todo /* carry queue where new
						* operations are to be put
						* in */, 
			     int including_insert_coord_p /* true if
							   * @insertion_coord
							   * can be moved */)
{
	int result;
	znode *source;
	carry_plugin_info info;

	source = insert_coord -> node;
	info.doing = doing;
	info.todo  = todo;
	result = node_plugin_by_node( node ) -> shift
		( insert_coord, node, 
		  ( side == LEFT_SIDE ) ? SHIFT_LEFT : SHIFT_RIGHT, 0,
		  including_insert_coord_p, &info );
	/*
	 * the only error ->shift() method of node plugin can return is
	 * -ENOMEM due to carry node/operation allocation.
	 */
	assert( "nikita-915", ( result >= 0 ) || ( result == -ENOMEM ) );
	if( result > 0 ) {
		doing -> restartable = 0;
		znode_set_dirty( source );
		znode_set_dirty( node );
	}
	assert( "nikita-2077", coord_check( insert_coord ) );
	return 0;
}

typedef carry_node * ( *carry_iterator )( carry_node *node );
static carry_node *find_dir_carry( carry_node *node, carry_level *level,
				   carry_iterator iterator );

/**
 * look for the left neighbor of given carry node in a carry queue.
 *
 * This is used by find_left_neighbor(), but I am not sure that this
 * really gives any advantage. More statistics required.
 *
 */
carry_node *find_left_carry( carry_node *node /* node to fine left neighbor
					       * of */,
			     carry_level *level /* level to scan */ )
{
	return find_dir_carry( node, level, 
			       ( carry_iterator ) pool_level_list_prev );
}

/**
 * look for the right neighbor of given carry node in a
 * carry queue.
 *
 * This is used by find_right_neighbor(), but I am not sure that this
 * really gives any advantage. More statistics required.
 *
 */
carry_node *find_right_carry( carry_node *node /* node to fine right neighbor
					       * of */,
			      carry_level *level /* level to scan */ )
{
	return find_dir_carry( node, level, 
			       ( carry_iterator ) pool_level_list_next );
}

/**
 * look for the left or right neighbor of given carry node in a carry
 * queue.
 *
 * Helper function used by find_{left|right}_carry().
 */
static carry_node *find_dir_carry( carry_node *node /* node to start scanning
						     * from */, 
				   carry_level *level /* level to scan */,
				   carry_iterator iterator /* operation to
							    * move to the next
							    * node */)
{
	carry_node *neighbor;

	assert( "nikita-1059", node != NULL );
	assert( "nikita-1060", level != NULL );

	/*
	 * scan list of carry nodes on this list dir-ward, skipping all
	 * carry nodes referencing the same znode.
	 */
	neighbor = node;
	while( 1 ) {
		neighbor = iterator( neighbor );
		if( pool_level_list_end( &level -> nodes, &neighbor -> header ) )
			return NULL;
		if( neighbor -> real_node != node -> real_node )
			return neighbor;
	}
}

/**
 * ->estimate method of tree operations
 */
static __u64 common_estimate( carry_op *op, carry_level *doing )
{
	__u64 result;
	reiser4_tree *tree;

	assert( "nikita-2310", op != NULL );
	assert( "nikita-2311", doing != NULL );

	tree = current_tree;

	switch( op -> op ) {
	case COP_INSERT:
	case COP_PASTE:
	case COP_EXTENT:
		/*
		 * reserve for insertion of two block at each level, plus new
		 * tree root.
		 */
		result = ( __u64 ) 2 * ( tree -> height + 1 );
		break;
	case COP_DELETE:
	case COP_CUT:
		/*
		 * FIXME-NIKITA when key compression will be implemented,
		 * COP_UPDATE should be moved to COP_INSERT and friends,
		 * because them, update can possibly enlarge a key and result
		 * in insertion.
		 */
	case COP_UPDATE:
		result = ( __u64 ) 0;
		break;
	default:
		not_implemented( "nikita-2313", 
				 "Carry operation %i is not supported", 
				 op -> op );
	}
	return result;
}

/**
 * This is dispatch table for carry operations. It can be trivially
 * abstracted into useful plugin: tunable balancing policy is a good
 * thing.
 **/
carry_op_handler op_dispatch_table[ COP_LAST_OP ] = {
	[ COP_INSERT ] = {
		.handler  = carry_insert,
		.estimate = common_estimate
	},
	[ COP_DELETE ] = {
		.handler  = carry_delete,
		.estimate = common_estimate
	},
	[ COP_CUT    ] = {
		.handler  = carry_cut,
		.estimate = common_estimate
	},
	[ COP_PASTE  ] = {
		.handler  = carry_paste,
		.estimate = common_estimate
	},
	[ COP_EXTENT ] = {
		.handler  = carry_extent,
		.estimate = common_estimate
	},
	[ COP_UPDATE ] = {
		.handler  = carry_update,
		.estimate = common_estimate
	},
	[ COP_MODIFY ] = {
		.handler  = carry_modify,
		.estimate = common_estimate
	}
};


/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
