/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/* rules for locking during searches: never insert before the first
   item in a node without locking the left neighbor and the patch to
   the common parent from the node and left neighbor.  This ensures
   that searching works. */


/* tree searching algorithm, intranode searching algorithms are in
   plugin/nodes/* */

/* return pointer to item body */
void *item_body_by_coord( const tree_coord *coord /* coord to query */ )
{
	assert( "nikita-324", coord != NULL );
	assert( "nikita-325", coord -> node != NULL );
	assert( "nikita-326", znode_is_loaded( coord -> node ) );
	trace_stamp( TRACE_TREE );

	return node_plugin_by_node( coord -> node ) -> item_by_coord( coord );
}

/** return length of item at @coord */
int item_length_by_coord( const tree_coord *coord /* coord to query */ )
{
	assert( "nikita-327", coord != NULL );
	assert( "nikita-328", coord -> node != NULL );
	assert( "nikita-329", znode_is_loaded( coord -> node ) );
	trace_stamp( TRACE_TREE );

	return node_plugin_by_node( coord -> node ) -> length_by_coord( coord );
}

/** return plugin of item at @coord */
item_plugin *item_plugin_by_coord( const tree_coord *coord /* coord to query */ )
{
	assert( "nikita-330", coord != NULL );
	assert( "nikita-331", coord -> node != NULL );
	assert( "nikita-332", znode_is_loaded( coord -> node ) );
	trace_stamp( TRACE_TREE );

	return node_plugin_by_node( coord -> node ) -> plugin_by_coord( coord );
}

/** return node plugin of @node */
node_plugin * node_plugin_by_node( const znode *node /* node to query */ )
{
	assert( "vs-213", node != NULL );
	assert( "vs-214", znode_is_loaded( node ) );

	return node->nplug;
}

/** return type of item at @coord */
item_type_id item_type_by_coord( const tree_coord *coord /* coord to query */ )
{
	assert( "nikita-333", coord != NULL );
	assert( "nikita-334", coord -> node != NULL );
	assert( "nikita-335", znode_is_loaded( coord -> node ) );
	assert( "nikita-336", item_plugin_by_coord( coord ) != NULL );

	trace_stamp( TRACE_TREE );

	return item_plugin_by_coord( coord ) -> common.item_type;
}

/* return id of item */
item_id item_id_by_coord( const tree_coord *coord /* coord to query */ )
{
	assert( "vs-539", coord != NULL );
	assert( "vs-538", coord -> node != NULL );
	assert( "vs-537", znode_is_loaded( coord -> node ) );
	assert( "vs-536", item_plugin_by_coord( coord ) != NULL );

	trace_stamp( TRACE_TREE );

	assert( "vs-540",
		item_id_by_plugin( item_plugin_by_coord( coord ) ) < LAST_ITEM_ID );
	return item_id_by_plugin( item_plugin_by_coord( coord ) );
}

/** return key of item at @coord */
reiser4_key *item_key_by_coord( const tree_coord *coord /* coord to query */, 
				reiser4_key *key /* result */ )
{
	assert( "nikita-338", coord != NULL );
	assert( "nikita-339", coord -> node != NULL );
	assert( "nikita-340", znode_is_loaded( coord -> node ) );
	trace_stamp( TRACE_TREE );

	return node_plugin_by_node( coord -> node ) -> key_at( coord, key );
}

/** return key of unit at @coord */
reiser4_key *unit_key_by_coord( const tree_coord *coord /* coord to query */, 
				reiser4_key *key /* result */ )
{
	assert( "nikita-772", coord != NULL );
	assert( "nikita-774", coord -> node != NULL );
	assert( "nikita-775", znode_is_loaded( coord -> node ) );
	trace_stamp( TRACE_TREE );

	if( item_plugin_by_coord( coord )->common.unit_key != NULL )
		return item_plugin_by_coord( coord )->common.unit_key
			( coord, key );
	else
		return item_key_by_coord( coord, key );
}


/* tree lookup cache */

TS_LIST_DEFINE( cbk_cache, cbk_cache_slot, lru );

/** Initialise coord cache slot */
static void cbk_cache_init_slot( cbk_cache_slot *slot )
{
	assert( "nikita-345", slot != NULL );

	cbk_cache_list_clean( slot );
	slot -> node = NULL;
}

/** Initialise coord cache */
int cbk_cache_init( cbk_cache *cache /* cache to init */ )
{
	int i;

	assert( "nikita-346", cache != NULL );

	cbk_cache_list_init( &cache -> lru );
	for( i = 0 ; i < CBK_CACHE_SLOTS ; ++ i ) {
		cbk_cache_init_slot( &cache -> slot[ i ] );
		cbk_cache_list_push_back( &cache -> lru, cache -> slot + i );
	}
	spin_lock_init( &cache -> guard );
	return 0;
}

static void cbk_cache_lock( cbk_cache *cache /* cache to lock */ )
{
	assert( "nikita-1800", cache != NULL );
	spin_lock( &cache -> guard );
}

static void cbk_cache_unlock( cbk_cache *cache /* cache to unlock */)
{
	assert( "nikita-1801", cache != NULL );
	spin_unlock( &cache -> guard );
}

/**
 * macro to iterate over all cbk cache slots
 */
#define for_all_slots( cache, slot )					\
	for( ( slot ) = cbk_cache_list_front( &( cache ) -> lru ) ;	\
	     !cbk_cache_list_end( &( cache ) -> lru, ( slot ) ) ; 	\
	     ( slot ) = cbk_cache_list_next( slot ) )

/**
 * Remove references, if any, to @node from coord cache
 */
void cbk_cache_invalidate( const znode *node /* node to remove from cache */ )
{
	cbk_cache_slot *slot;
	cbk_cache      *cache;

	assert( "nikita-350", node != NULL );
	assert( "nikita-1479", lock_counters() -> spin_locked_tree > 0 );

	cache = current_tree -> cbk_cache;
	cbk_cache_lock( cache );
	for_all_slots( cache, slot ) {
		if( slot -> node == NULL )
			break;
		if( slot -> node == node ) {
			cbk_cache_list_remove( slot );
			cbk_cache_list_push_back( &cache -> lru, slot );
			slot -> node = NULL;
			break;
		}
	}
	cbk_cache_unlock( cache );
}

/** add to the cbk-cache in the "tree" information about "node". This
    can actually be update of existing slot in a cache. */
void cbk_cache_add( znode *node /* node to add to the cache */ )
{
	cbk_cache        *cache;
	cbk_cache_slot   *slot;

	assert( "nikita-352", node != NULL );

	cache = current_tree -> cbk_cache;
	cbk_cache_lock( cache );
	/* find slot to update/add */
	for_all_slots( cache, slot ) {
		/* oops, this node is already in a cache */
		if( slot -> node == node )
			break;
		if( slot -> node == NULL ) {
			slot = NULL;
			break;
		}
	}
	/* if all slots are used, reuse least recently used one */
	if( ( slot == NULL ) || cbk_cache_list_end( &cache -> lru, slot ) )
		slot = cbk_cache_list_back( &cache -> lru );
	slot -> node = node;
	cbk_cache_list_remove( slot );
	cbk_cache_list_push_back( &cache -> lru, slot );
	cbk_cache_unlock( cache );
}

#if REISER4_DEBUG
/** Debugging aid: print human readable information about @slot */
void print_cbk_slot( const char *prefix /* prefix to print */, 
		     cbk_cache_slot *slot /* slot to print */ )
{
	if( slot == NULL )
		info( "%s: null slot\n", prefix );
	else
		print_znode( "node", slot -> node );
}

/** Debugging aid: print human readable information about @cache */
void print_cbk_cache( const char *prefix /* prefix to print */, 
		      cbk_cache  *cache /* cache to print */)
{
	if( cache == NULL )
		info( "%s: null cache\n", prefix );
	else {
		cbk_cache_slot *scan;

		info( "%s: cache: %p\n", prefix, cache );
		for_all_slots( cache, scan )
			print_cbk_slot( "slot", scan );
	}
}
#endif

/** struct to pack all numerous arguments of tree lookup.  Used to avoid
    passing a lot of arguments to helper functions. */
typedef struct cbk_handle {
	/** tree we are in */
	reiser4_tree        *tree;
	/** key we are going after */
	const reiser4_key   *key;
	/** coord we to store result */
	tree_coord  	    *coord;
	/** lock to take on target node */
	znode_lock_mode      lock_mode;
	/** lookup bias */
	lookup_bias          bias;
	/** lock level */
	tree_level           llevel;
	/** stop level */
	tree_level           slevel;
	/** level we are currently at */
	tree_level           level;
	/** block number of "active" node */
	reiser4_block_nr    block;
	/** put here error message to be printed by caller */
	const char          *error;
	/** result passed back to caller */
	lookup_result        result;
	/** lock handles for active and parent */
	lock_handle *parent_lh;
	lock_handle *active_lh;
	reiser4_key          ld_key;
	reiser4_key          rd_key;
	__u32                flags;
} cbk_handle;

static lookup_result cbk( cbk_handle *h );
static int cbk_cache_search( cbk_handle *h );

static level_lookup_result cbk_level_lookup( cbk_handle *h );
static level_lookup_result cbk_node_lookup ( cbk_handle *h );

/* helper functions */

/** type of lock we want to obtain during tree traversal. On stop level
    we want type of lock user asked for, on upper levels: read lock. */
static znode_lock_mode cbk_lock_mode( tree_level level, cbk_handle *h );
/** release parent node during traversal */
static void put_parent( cbk_handle *h );
/** check consistency of fields */
static int sanity_check( cbk_handle *h );
/** release resources in handle */
static void hput( cbk_handle *h );

static void setup_delimiting_keys( cbk_handle *h );
static int prepare_delimiting_keys( cbk_handle *h );
static level_lookup_result search_to_left( cbk_handle *h );

/**
 * main tree lookup procedure
 *
 * Check coord cache. If key we are looking for is not found there, call cbk()
 * to do real tree traversal.
 *
 * As we have extents on the twig level, @lock_level and @stop_level can
 * be different from LEAF_LEVEL and each other.
 *
 * Thread cannot keep any reiser4 locks (tree, znode, dk spin-locks, or znode
 * long term locks) while calling this. 
 */
lookup_result coord_by_key( reiser4_tree *tree /* tree to perform search
						* in. Usually this tree is
						* part of file-system
						* super-block */, 
			    const reiser4_key *key /* key to look for */,
			    tree_coord *coord /* where to store found
						* position in a tree. Fields
						* in "coord" are only valid if
						* coord_by_key() returned
						* "CBK_COORD_FOUND" */,
			    lock_handle *lh ,
			    znode_lock_mode lock_mode /* type of lookup we
						       * want on node. Pass
						       * ZNODE_READ_LOCK here
						       * if you only want to
						       * read item found and
						       * ZNODE_WRITE_LOCK if
						       * you want to modify
						       * it */, 
			    lookup_bias bias /* what to return if coord
					      * with exactly the @key is
					      * not in the tree */,
			    tree_level lock_level /* tree level where to start
						   * taking @lock type of
						   * locks */,
			    tree_level stop_level /* tree level to stop. Pass
						   * leaf_level or twig_level
						   * here Item being looked
						   * for has to be between
						   * @lock_level and
						   * @stop_level, inclusive */,
			    __u32      flags /* search flags */ )
{
	cbk_handle          handle;
	lock_handle parent_lh;

	init_lh(lh);
	init_lh(&parent_lh);

	assert( "nikita-353", tree != NULL );
	assert( "nikita-354", key != NULL );
	assert( "nikita-355", coord != NULL );
	assert( "nikita-356",
		( bias == FIND_EXACT ) || ( bias == FIND_MAX_NOT_MORE_THAN ) );
	assert( "nikita-357", stop_level >= LEAF_LEVEL );
	trace_stamp( TRACE_TREE );

	xmemset( &handle, 0, sizeof handle );

	handle.tree      = tree;
	handle.key       = key;
	handle.lock_mode = lock_mode;
	handle.bias      = bias;
	handle.llevel    = lock_level;
	handle.slevel    = stop_level;
	handle.coord     = coord;
	/* set flags. See comment in tree.h:cbk_flags */
	handle.flags     = flags | CBK_TRUST_DK;

	handle.active_lh = lh;
	handle.parent_lh = &parent_lh;

	/* first check whether "key" is in cache of recent lookups. */
	if( cbk_cache_search( &handle ) == 0 )
		return handle.result;
	else
	  /* this is what happens when I say no do_ ? ;-/ Fix this. -Hans */
		return cbk( &handle );
}


/* relook for @key in the tree if @coord is not set correspondingly already */
int coord_by_hint_and_key (reiser4_tree * tree, const reiser4_key * key,
			   tree_coord * coord, lock_handle * lh,
			   lookup_bias bias,
			   tree_level lock_level,/* at which level to start
						    getting write locks */
			   tree_level stop_level)
{
	int result;

	done_lh (lh);
	init_lh (lh);

	result = coord_by_key (tree, key, coord, lh,
			       ZNODE_WRITE_LOCK, bias,
			       lock_level, stop_level, 0);
	return result;
}


/**
 * Execute actor for each item (or unit, depending on @through_units_p),
 * starting from @coord, right-ward, until either: 
 *
 * - end of the tree is reached
 * - unformatted node is met
 * - error occured
 * - @actor returns 0 or less
 *
 * Error code, or last actor return value is returned.
 *
 * This is used by readdir() and alikes.
 */
int iterate_tree( reiser4_tree *tree /* tree to scan */, 
		  tree_coord *coord /* coord to start from */, 
		  lock_handle *lh /* lock handle to start with and to
					   * update along the way */, 
		  tree_iterate_actor_t actor /* function to call on each
					      * item/unit */, 
		  void *arg /* argument to pass to @actor */,
		  znode_lock_mode mode /* lock mode on scanned nodes */, 
		  int through_units_p /* call @actor on each item or on each
				       * unit */ )
{
	int result;

	assert( "nikita-1143", tree != NULL );
	assert( "nikita-1145", coord != NULL );
	assert( "nikita-1146", lh != NULL );
	assert( "nikita-1147", actor != NULL );

	if( !coord_is_existing_unit( coord ) )
		return -ENOENT;
	result = zload( coord -> node );
	if( result != 0 )
		return result;
	while( ( result = actor( tree, coord, lh, arg ) ) > 0 ) {
		/*
		 * move further 
		 */
		if( ( through_units_p && coord_is_rightmost_unit( coord ) ) || 
		    ( !through_units_p && coord_is_rightmost_item( coord ) ) ) {
			do {
				lock_handle couple;

				/* 
				 * move to the next node 
				 */
				init_lh( &couple );
				result = reiser4_get_right_neighbor
					( &couple, coord -> node, ( int ) mode, 
					  GN_DO_READ );
				zrelse( coord -> node );
				if( result == 0 ) {
					done_lh( lh );

					result = zload( couple.node );
					if( result != 0 )
						return result;
					coord_init_first_unit( coord, couple.node );
					move_lh( lh, &couple );
				} else
					return result;
			} while( node_is_empty( coord -> node ) );
		} else if( through_units_p ) {
			coord_next_unit( coord );
		} else {
			coord_next_item( coord );
		}
		/* FIXME_COORD: this looks suspicious */
		assert( "nikita-1149", coord_is_existing_unit( coord ) );
	}
	zrelse( coord -> node );
	return result;
}

/** main function that handles common parts of tree traversal: starting
    (fake znode handling), restarts, error handling, completion */
static lookup_result cbk( cbk_handle *h /* search handle */ )
{
	int done;
	int iterations;

	assert( "nikita-365", h != NULL );
	assert( "nikita-366", h -> tree != NULL );
	assert( "nikita-367", h -> key != NULL );
	assert( "nikita-368", h -> coord != NULL );
	assert( "nikita-369", ( h -> bias == FIND_EXACT ) ||
		( h -> bias == FIND_MAX_NOT_MORE_THAN ) );
	assert( "nikita-370", h -> slevel >= LEAF_LEVEL );
	trace_stamp( TRACE_TREE );
	reiser4_stat_tree_add( cbk );

	iterations = 0;

	/* loop for restarts */
 restart:

	h -> result = CBK_COORD_FOUND;

	{ 
/* vs, determine whether we can easily eliminate this fake znode.  I
   worry that our new programmers merely copy the other literature without
   really considering whether those others are right. -Hans */
/* obtain fake znode */
		znode *fake;

		assert ("zam-355", lock_stack_isclean(
				get_current_lock_stack()));
/* ewww, ugly, why not just follow the super block pointer to the root
   of the tree. Or even better, have some nice little
   get_tree_root_node macro.  -Hans */
		fake = zget(h->tree, &FAKE_TREE_ADDR,
			    NULL, 0 , GFP_KERNEL);

		if (IS_ERR(fake)) return PTR_ERR(fake);

		done = longterm_lock_znode(h->parent_lh, fake, 
					   GN_DO_READ, ZNODE_LOCK_LOPRI);

		assert( "nikita-1637", done != -EDEADLK );

		zput(fake);
		if (done)
			return done;
		/* connect_znode() needs it */
		h -> coord -> node = fake;
		h -> ld_key = *min_key();
		h -> rd_key = *max_key();
	}

	h -> block = h -> tree -> root_block;
	h -> level = h -> tree -> height;
	h -> error = NULL;

	/* loop descending a tree */
	for( ; !done ; ++ iterations ) {

		if( REISER4_CBK_ITERATIONS_LIMIT && 
		    ( iterations > REISER4_CBK_ITERATIONS_LIMIT ) &&
		    !( iterations & ( iterations - 1 ) ) ) {
			warning( "nikita-1481", "Too many iterations: %i",
				 iterations );
			print_key( "key", h -> key );
		}
		switch( cbk_level_lookup( h ) ) {
		    case LLR_CONT:
			    move_lh(h->parent_lh, h->active_lh);
			    continue;
		    default:
			    wrong_return_value( "nikita-372", "cbk_level" );
		    case LLR_DONE:
			    done = 1;
			    break;
		    case LLR_REST:
			    reiser4_stat_tree_add( cbk_restart );
			    hput(h);
			    ++ iterations;
			    goto restart;
		}
	}

	/* that's all. The rest if error handling */
	if( h -> error != NULL ) {
		warning( "nikita-373", "%s: level: %i, "
			 "lock_level: %i, stop_level: %i "
			 "lock_mode: %s, bias: %s",
			 h -> error, h -> level, h -> llevel, h -> slevel,
			 lock_mode_name( h -> lock_mode ), bias_name( h -> bias ) );
		print_address( "block", &h -> block );
		print_key( "key", h -> key );
		print_coord_content( "coord", h -> coord );
		print_znode( "active", h -> active_lh -> node);
		print_znode( "parent", h -> parent_lh -> node);
	}
	if( ( h -> result != CBK_COORD_FOUND ) &&
	    ( h -> result != CBK_COORD_NOTFOUND ) ) {
		/* failure. do cleanup */
		hput( h );
	}
	assert( "nikita-1605", ergo( ( h -> result == CBK_COORD_FOUND ) &&
				     ( h -> bias == FIND_EXACT ),
				     coord_is_existing_unit( h -> coord ) ) );
	return h -> result;
}

/** coord_by_key level function that maintains znode sibling/parent
    pointers (web of znodes)) */
static level_lookup_result cbk_level_lookup (cbk_handle *h /* search handle */)
{
	int ret;
	znode * active;

	active = zget( h -> tree, &h -> block,
		       h -> parent_lh -> node,
		       h -> level, GFP_KERNEL );

	if (IS_ERR(active)) {
		h->result = PTR_ERR(active);
		return LLR_DONE;
	}

	h->result = longterm_lock_znode(h->active_lh, active,
					cbk_lock_mode(h->level, h), ZNODE_LOCK_LOPRI);
	zput(active);
	if (h->result)
		goto fail_or_restart;

	put_parent(h);

	if( ! znode_is_loaded( active ) )
		setup_delimiting_keys( h );

	/*
	 * FIXME-NIKITA this is ugly kludge. To get rid of it, get rid of
	 * tree_coord.between field first.
	 */
	/*h->coord->between = AT_UNIT;*/
	coord_set_to_unit( h -> coord );

	spin_lock_tree (h->tree);
	if (!znode_is_loaded(active) && (h->coord->node != NULL))
		active->ptr_in_parent_hint = *h->coord;
	spin_unlock_tree (h->tree);

	/* protect sibling pointers and `connected' state bits, check
	 * znode state */
	spin_lock_tree(h -> tree);
	ret = znode_is_connected(active);
	spin_unlock_tree(h -> tree);

	if (!ret) {
		/*
		 * FIXME: h->coord->node and active are of different levels?
		 */
		h->result = connect_znode(h->coord, active);
		if (h->result)
			goto fail_or_restart;
	}

	if( ( !znode_contains_key_lock( active, h -> key ) &&
	      ( h -> flags & CBK_TRUST_DK ) ) ||
	    ZF_ISSET( active, ZNODE_HEARD_BANSHEE ) ) {
		/*
		 * 1. key was moved out of this node while this thread was
		 * waiting for the lock. Restart. More elaborate solution is
		 * to determine where key moved (to the left, or to the right)
		 * and try to follow it through sibling pointers.
		 *
		 * 2. or, node itself is going to be removed from the
		 * tree. Release lock and restart.
		 */
		if( REISER4_STATS ) {
			if( znode_contains_key_lock( active, h -> key ) )
				reiser4_stat_tree_add( cbk_met_ghost );
			else
				reiser4_stat_tree_add( cbk_key_moved );
		}
		h -> result = -EAGAIN;
	}
	if( h -> result == -EAGAIN )
		return LLR_REST;

	h -> result = zload( active );
	if( h -> result ) {
		return LLR_DONE;
	}

	/* sanity checks */
	if( sanity_check( h ) ) {
		zrelse(active);
		return LLR_DONE;
	}

	ret =  cbk_node_lookup(h);
	zrelse(active);

	return ret;

 fail_or_restart:
	if (h->result == -EDEADLK)
		return LLR_REST;
	return LLR_DONE;
}


/*
 * look to the right of @coord. If it is an item of internal type - 1 is
 * returned. If that item is in another node - @coord and @lh are switched to
 * that node
 */
static int is_next_item_internal( tree_coord *coord,  lock_handle *lh )
{
	int result;


	if( !coord_is_rightmost_item (coord) ) {
		/*
		 * next item is in the same node
		 */
		coord_next_item (coord);
		if( item_is_internal( coord ) )
			return 1;
		coord_prev_item (coord);
		return 0;
	} else {
		/*
		 * look for next item in right neighboring node
		 */
		lock_handle right_lh;
		tree_coord right;


		init_lh( &right_lh );
		result = reiser4_get_right_neighbor( &right_lh,
						     coord -> node,
						     ZNODE_READ_LOCK,
						     GN_DO_READ);
		if( result && result != -ENAVAIL ) {
			/* error occured */
			done_lh( &right_lh );
			return result;
		}
		if( !result ) {
			coord_init_first_unit( &right, right_lh.node );
			if( item_is_internal( &right ) ) {
				/*
				 * switch to right neighbor
				 */
				done_lh( lh );

				coord_dup( coord, &right );
				move_lh( lh, &right_lh );

				return 1;
			}
		}
		/* item to the right of @coord either does not exist or is not
		   of internal type */
		done_lh( &right_lh );
		return 0;
	}
}


/*
 * inserting empty leaf after (or between) item of not internal type we have to
 * know which right delimiting key corresponding znode has to be inserted with
 */
static reiser4_key *rd_key( tree_coord *coord, reiser4_key *key )
{
	if( !coord_is_rightmost_item( coord ) ) {
		/*
		 * get right delimiting key from an item to the right of @coord
		 */
		tree_coord tmp;

		coord_dup( &tmp, coord );
		coord_next_item( &tmp );
		item_key_by_coord( &tmp, key );

	} else {
		/*
		 * use right delimiting key of znode we insert new pointer to
		 */
		spin_lock_dk( current_tree );
		*key = *znode_get_rd_key( coord -> node );
		spin_unlock_dk( current_tree );
	}
	return key;
}


/*
 * this is used to insert empty node into leaf level if tree lookup can not go
 * further down because it stopped between items of not internal type
 */
static int add_empty_leaf( tree_coord *insert_coord, lock_handle *lh,
			   const reiser4_key *key, const reiser4_key *rdkey )
{
	int result;
	carry_pool        pool;
	carry_level       todo;
	carry_op         *op;
	znode            *node;
	reiser4_item_data item;
	carry_insert_data cdata;

	init_carry_pool( &pool );
	init_carry_level( &todo, &pool );
	ON_STATS( todo.level_no = TWIG_LEVEL );

	node = new_node( insert_coord -> node, LEAF_LEVEL );	
	if( IS_ERR( node ) )
		return PTR_ERR( node );
	/*
	 * setup delimiting keys for node being inserted
	 */
	spin_lock_dk( current_tree );
	*znode_get_ld_key( node ) = *key;
	*znode_get_rd_key( node ) = *rdkey;
	spin_unlock_dk( current_tree );

	op = post_carry( &todo, COP_INSERT, insert_coord -> node, 0 );
	if( !IS_ERR( op ) ) {
		cdata.coord = insert_coord;
		cdata.key   = key;
		cdata.data  = &item;
		op -> u.insert.d = &cdata;
		op -> u.insert.type = COPT_ITEM_DATA;
		build_child_ptr_data( node, &item );
		item.arg = NULL;
		/*
		 * have @insert_coord to be set at inserted item after
		 * insertion is done
		 */
		op -> node -> track = 1;
		op -> node -> tracked = lh;
		
		result = carry( &todo, 0 );
	} else
		result = PTR_ERR( op );
	zput( node );
	done_carry_pool( &pool );
	return result;
}

/*
   Process one node during tree traversal.
   This is standard function independent of tree locking protocols.
 */
static level_lookup_result cbk_node_lookup( cbk_handle *h /* search handle */ )
{
	node_plugin      *nplug;
	item_plugin      *iplug;
	lookup_bias       node_bias;
	znode            *active;
	reiser4_tree     *tree;
	int               result;

	/**
	 * true if @key is left delimiting key of @node
	 */
	static int key_is_ld( znode *node, const reiser4_key *key )
	{
		int is_ld;
			
		assert( "nikita-1716", node != NULL );
		assert( "nikita-1758", key != NULL );
			
		spin_lock_dk( current_tree );
		assert( "nikita-1759", znode_contains_key( node, key ) );
		is_ld = keyeq( znode_get_ld_key( node ), key );
		spin_unlock_dk( current_tree );
		return is_ld;
	}


	assert( "nikita-379", h != NULL );

	/* disinter actively used active out of handle */
	active = h -> active_lh -> node;
	tree   = h -> tree;

	nplug = active -> nplug;
	assert( "nikita-380", nplug != NULL );

	/* 
	 * return item from "active" node with maximal key not greater than
	 * "key" 
	 */
	node_bias = h -> bias;
	result = nplug -> lookup( active, h -> key, node_bias, h -> coord );
	if( result != NS_FOUND && result != NS_NOT_FOUND ) {
		/* error occured */
		h -> result = result;
		return LLR_DONE;
	}
	if( h -> level == h -> slevel ) {
		/* welcome to the stop level */
		assert( "nikita-381", h -> coord -> node == active );
		if( result == NS_FOUND ) {
			/* success of tree lookup */
			assert( "nikita-1604",
				ergo( h -> bias == FIND_EXACT, 
				      coord_is_existing_unit( h -> coord ) ) );
			if( !( h -> flags & CBK_UNIQUE ) && 
			    key_is_ld( active, h -> key ) ) {
				return search_to_left( h );
			} else
				h -> result = CBK_COORD_FOUND;
			reiser4_stat_tree_add( cbk_found );
		} else {
			h -> result = CBK_COORD_NOTFOUND;
			reiser4_stat_tree_add( cbk_notfound );
		}
		cbk_cache_add( active );
		return LLR_DONE;
	}

	if( h -> level > TWIG_LEVEL && result == NS_NOT_FOUND ) {
		h -> error = "not found on internal node";
		h -> result = result;
		return LLR_DONE;
	}

	assert( "vs-361", h -> level > h -> slevel );

	iplug = item_plugin_by_coord( h -> coord );
	if( !item_is_internal( h -> coord ) ) {
		/* strange item type found on non-stop level?!  Twig
		   horrors? */
		assert( "vs-356", h -> level == TWIG_LEVEL );
		assert( "vs-357",
			item_id_by_coord( h -> coord )== EXTENT_POINTER_ID );

		if( result == NS_FOUND ) {
			/*
			 * we have found desired key on twig level in extent item
			 */
			h -> result = CBK_COORD_FOUND;
			reiser4_stat_tree_add( cbk_found );
			return LLR_DONE;
		}

		/* take a look at the item to the right of h -> coord */
		result = is_next_item_internal( h -> coord, h -> active_lh );
		if( result < 0 ) {
			/*
			 * error occured while we were trying to look at the
			 * item to the right
			 */
			h -> error = "could not check next item";
			h -> result = result;
			return LLR_DONE;
		}
		if ( !result ) {
			/*
			 * item to the right is not internal one. Allocate a
			 * new node and insert pointer to it after item h ->
			 * coord
			 */
			reiser4_key key;
			
			
			if (cbk_lock_mode( h -> level, h ) != ZNODE_WRITE_LOCK ) {
				/*
				 * we got node read locked, restart
				 * coord_by_key to have write lock on twig
				 * level
				 */
				assert( "vs-360", h -> llevel < TWIG_LEVEL );
				h -> llevel = TWIG_LEVEL;
				return LLR_REST;
			}
			
			result = add_empty_leaf( h -> coord, h -> active_lh,
						 h -> key, rd_key( h -> coord, &key ) );
			if( result ) {
				h -> error = "could not add empty leaf";
				h -> result = result;
				return LLR_DONE;
			}
			assert( "vs-358",
				keyeq( h -> key,
				       item_key_by_coord( h -> coord, &key ) ) );
		} else {
			/* 
			 * this is special case mentioned in the comment on
			 * tree.h:cbk_flags. We have found internal item
			 * immediately on the right of extent, and we are
			 * going to insert new item there. Key of item we are
			 * going to insert is smaller than leftmost key in the
			 * node pointed to by said internal item (otherwise
			 * search wouldn't come to the extent in the first
			 * place).
			 */
			h -> flags &= ~CBK_TRUST_DK;
		}
		assert( "vs-362", item_is_internal( h -> coord ) );
		iplug = item_plugin_by_coord( h -> coord );
	}

	/* prepare delimiting keys for the next node */
	if( prepare_delimiting_keys( h ) ) {
		h -> error = "cannot prepare delimiting keys";
		h -> result = CBK_IO_ERROR;
		return LLR_DONE;
	}

	/* go down to next level */
	assert( "vs-515", item_is_internal ( h -> coord ) );
	iplug -> s.internal.down_link( h -> coord, h -> key, &h -> block );
	-- h -> level;
	return LLR_CONT; /* continue */
}

/** true if @key is one of delimiting keys in @node */
static int key_is_delimiting( znode *node /* node to check key against */, 
			      const reiser4_key *key /* key to check */ )
{
	int result;

	assert( "nikita-1760", node != NULL );
	assert( "nikita-1722", key != NULL );

	spin_lock_dk( current_tree );
	result = 
		keyeq( znode_get_ld_key( node ), key ) ||
		keyeq( znode_get_rd_key( node ), key );
	spin_unlock_dk( current_tree );
	return result;
}

static int cbk_cache_scan_slots( cbk_handle *h /* cbk handle */ )
{
	level_lookup_result llr;
	znode              *node;
	cbk_cache_slot     *slot;
	cbk_cache          *cache;
	tree_level          level;
	int                 result;

	assert( "nikita-1317", h != NULL );
	assert( "nikita-1315", h -> tree != NULL );
	assert( "nikita-1316", h -> key != NULL );

	cache = h -> tree -> cbk_cache;
	node  = NULL; /* to keep gcc happy */
	level = LEAF_LEVEL; /* to keep gcc happy */
	/*
	 * keep cache spin locked during this test, to avoid race with
	 * cbk_cache_invalidate()
	 */
	cbk_cache_lock( cache );
	for_all_slots( cache, slot ) {
		int is_key_inside;

		node = slot -> node;
		if( node != NULL )
			zref( node );
		if( node == NULL )
			break;

		level = znode_get_level( node );
		if( ( h -> slevel > level ) || ( level > h -> llevel ) )
			zput( node );
		else {
			/* min_key <= key <= max_key */
			is_key_inside = znode_contains_key_lock( node, h -> key );
			if( ! is_key_inside )
				zput( node );
			else
				break;
		}
	}
	cbk_cache_unlock( cache );
	if( ( node == NULL ) || cbk_cache_list_end( &cache -> lru, slot ) ) {
		h -> result = CBK_COORD_NOTFOUND;
		return -ENOENT;
	}

	result = longterm_lock_znode( h -> active_lh, node, 
				      cbk_lock_mode( level, h ), 
				      ZNODE_LOCK_LOPRI );
	zput( node );
	if( result != 0 )
		return result;
	result = zload( node );
	if( result != 0 )
		return result;

	/* recheck keys */
	result = znode_contains_key_lock( node, h -> key ) && 
		! ZF_ISSET( node, ZNODE_HEARD_BANSHEE );
	if( result ) {
		/* do lookup inside node */
		h -> level = level;
		llr = cbk_node_lookup( h );
		
		if( llr != LLR_DONE )
			/* restart of continue on the next level */
			result = -ENOENT;
		else if( ( h -> result != CBK_COORD_NOTFOUND ) &&
			 ( h -> result != CBK_COORD_FOUND ) )
			/* io or oom */
			result = -ENOENT;
		else if( key_is_delimiting( node, h -> key ) ) {
			/*
			 * we are looking for possibly non-unique key
			 * and it is item is at the edge of @node. May
			 * be it is in the neighbor.
			 */
			reiser4_stat_tree_add( cbk_cache_utmost );
			result = -ENOENT;
		} else 
			/* good. Either item found or definitely not found. */
			result = 0;
	} else {
		/*
		 * race. While this thread was waiting for the lock, node was
		 * rebalanced and item we are looking for, shifted out of it
		 * (if it ever was here).
		 *
		 * Continuing scanning is almost hopeless: node key range was
		 * moved to, is almost certainly at the beginning of the LRU
		 * list at this time, because it's hot, but restarting
		 * scanning from the very beginning is complex. Just return,
		 * so that cbk() will be performed. This is not that
		 * important, because such races should be rare. Are they?
		 */
		reiser4_stat_tree_add( cbk_cache_race );
		result = -ENOENT; /* -ERAUGHT */
	}
	zrelse( node );
	return result;
}

/**
 * look for item with given key in the coord cache
 *
 * This function, called by coord_by_key(), scans "coord cache" (&cbk_cache)
 * which is a small LRU list of znodes accessed lately. For each znode in
 * znode in this list, it checks whether key we are looking for fits into key
 * range covered by this node. If so, and in addition, node lies at allowed
 * level (this is to handle extents on a twig level), node is locked, and
 * lookup inside it is performed.
 *
 * we need a measurement of the cost of this cache search compared to the cost
 * of coord_by_key.
 *
 */
static int cbk_cache_search( cbk_handle *h /* cbk handle */ )
{
	int result;

	result = cbk_cache_scan_slots( h );
	if( result != 0 ) {
		done_lh( h -> active_lh );
		done_lh( h -> parent_lh );
		reiser4_stat_tree_add( cbk_cache_miss );
	} else {
		assert( "nikita-1319", 
			( h -> result == CBK_COORD_NOTFOUND ) || 
			( h -> result == CBK_COORD_FOUND ) );
		reiser4_stat_tree_add( cbk_cache_hit );
	}
	return result;
}

/** type of lock we want to obtain during tree traversal. On stop level
    we want type of lock user asked for, on upper levels: read lock. */
static znode_lock_mode cbk_lock_mode( tree_level level, cbk_handle *h )
{
	assert( "nikita-382", h != NULL );

	return ( level <= h -> llevel ) ? h -> lock_mode : ZNODE_READ_LOCK;
}

/**
 * find delimiting keys of child
 *
 * Determine left and right delimiting keys for child pointed to by
 * @parent_coord.
 *
 */
int find_child_delimiting_keys( znode *parent /* parent znode, passed
					       * locked */, 
				const tree_coord *parent_coord /* coord where
								 * pointer to
								 * child is
								 * stored */, 
				reiser4_key *ld /* where to store left
						 * delimiting key */,
				reiser4_key *rd /* where to store right
						 * delimiting key */)
{
	tree_coord neighbor;
	
	assert( "nikita-1484", parent != NULL );
	assert( "nikita-1485", spin_dk_is_locked( current_tree ) );
	
	coord_dup( &neighbor, parent_coord );

	/* imitate item ->lookup() behavior. */
	/* FIXME_COORD: is it right? */
	coord_set_after_unit( &neighbor );

	if( coord_is_existing_unit( &neighbor ) || 
	    ( coord_set_to_left( &neighbor ) == 0 ) )
		unit_key_by_coord( &neighbor, ld );
	else
		*ld = *znode_get_ld_key( parent );

	coord_dup( &neighbor, parent_coord );
	coord_set_after_unit( &neighbor );
	/* FIXME_COORD: is it right? */
	if( coord_set_to_right( &neighbor ) == 0 )
		unit_key_by_coord( &neighbor, rd );
	else
		*rd = *znode_get_rd_key( parent );

	return 0;
}

/**
 * helper function used by coord_by_key(): remember in @h delimiting keys of
 * child that will be processed on the next level.
 *
 */
static int prepare_delimiting_keys( cbk_handle *h /* search handle */ )
{
	int result;
	assert( "nikita-1095", h != NULL );

	spin_lock_dk( current_tree );
	result = find_child_delimiting_keys( h -> active_lh -> node, h -> coord, 
					     &h -> ld_key, &h -> rd_key );
	spin_unlock_dk( current_tree );
	return result;
}

static level_lookup_result search_to_left( cbk_handle *h /* search handle */ )
{
	level_lookup_result result;
	tree_coord         *coord;
	znode              *node;
	znode              *neighbor;

	lock_handle lh;

	assert( "nikita-1761", h != NULL );
	assert( "nikita-1762", h -> level == h -> slevel );

	init_lh( &lh );
	coord = h -> coord;
	node  = h -> active_lh -> node;
	assert( "nikita-1763", coord_is_leftmost_unit( coord ) );

	reiser4_stat_tree_add( check_left_nonuniq );
	h -> result = reiser4_get_left_neighbor( &lh, node, 
						 ( int ) h -> lock_mode, 
						 GN_DO_READ );
	neighbor = NULL;
	switch( h -> result ) {
	case -EDEADLK:
		result = LLR_REST;
		break;
	case 0: {
		node_plugin         *nplug;
		tree_coord           crd;
		lookup_bias          bias;
		
		neighbor = lh.node;
		h -> result = zload( neighbor );
		if( h -> result != 0 ) {
			result = LLR_DONE;
			break;
		}

		nplug = neighbor -> nplug;

		bias = h -> bias;
		h -> bias = FIND_EXACT;
		h -> result = nplug -> lookup( neighbor, h -> key,
					       h -> bias, &crd );
		h -> bias = bias;

		if( h -> result == NS_NOT_FOUND ) {
	case -ENAVAIL:
			h -> result = CBK_COORD_FOUND;
			reiser4_stat_tree_add( cbk_found );
			cbk_cache_add( node );
	default: /* some other error */ 
			result = LLR_DONE;
		} else if( h -> result == NS_FOUND ) {
			reiser4_stat_tree_add( left_nonuniq_found );
			spin_lock_dk( current_tree );
			h -> rd_key = *znode_get_ld_key( node );
			leftmost_key_in_node( neighbor, &h -> ld_key );
			spin_unlock_dk( current_tree );
			h -> block = *znode_get_block( neighbor );
			result = LLR_CONT;
		} else {
			result = LLR_DONE;
		}
		if( neighbor != NULL )
			zrelse( neighbor );
	}
	}
	done_lh( &lh );
	return result;
}

/** helper macro: swap its arguments */
#define swap( a, b ) 								\
	({ typedef _t = (a); _t tmp ; tmp = (a) ; (a) = (b) ; (b) = tmp; })

/** debugging aid: return symbolic name of search bias */
const char *bias_name( lookup_bias bias /* bias to get name of */ )
{
	if( bias == FIND_EXACT )
		return "exact";
	else if( bias == FIND_MAX_NOT_MORE_THAN )
		return "left-slant";
/* 	else if( bias == RIGHT_SLANT_BIAS ) */
/* 		return "right-bias"; */
	else {
		static char buf[ 30 ];

		sprintf( buf, "unknown: %i", bias );
		return buf;
	}
}

/** debugging aid: print human readable information about @p */
void print_coord_content( const char *prefix /* prefix to print */, 
			  tree_coord *p /* coord to print */ )
{
	reiser4_key key;

	if( p == NULL ) {
		info( "%s: null\n", prefix );
		return;
	}
	info( "%s: data: %p, length: %i\n", prefix,
	      item_body_by_coord( p ), item_length_by_coord( p ));
	print_znode( prefix, p -> node );
	item_key_by_coord( p, &key );
	print_key( prefix, &key );
	print_plugin( prefix, item_plugin_to_plugin (item_plugin_by_coord( p ) ) );
}

/** debugging aid: print human readable information about @block */
void print_address( const char *prefix /* prefix to print */, 
		    const reiser4_block_nr *block /* block number to print */ )
{
	if( block == NULL ) {
		info( "%s: null\n", prefix );
	} else {
		info( "%s: %llu\n", prefix, *block );
	}
}

/** release parent node during traversal */
static void put_parent( cbk_handle *h /* search handle */ )
{
	assert( "nikita-383", h != NULL );
	if(h->parent_lh->node != NULL) {
		longterm_unlock_znode(h->parent_lh);
	}
}

/**
 * helper function used by coord_by_key(): release reference to parent znode
 * stored in handle before processing its child. */
static void hput( cbk_handle *h /* search handle */ )
{
	assert( "nikita-385", h != NULL );
	done_lh(h->parent_lh);
	done_lh(h->active_lh);
}

/**
 * Helper function used by cbk(): update delimiting keys of child node (stored
 * in h->active_lh->node) using key taken from parent on the parent level.
 */
static void setup_delimiting_keys( cbk_handle *h /* search handle */ )
{
	assert( "nikita-1088", h != NULL );
	spin_lock_dk( current_tree );
	*znode_get_ld_key( h -> active_lh -> node ) = h -> ld_key;
	*znode_get_rd_key( h -> active_lh -> node ) = h -> rd_key;
	spin_unlock_dk( current_tree );
}

static int block_nr_is_correct( reiser4_block_nr *block UNUSED_ARG /* block
								    * number
								    * to
								    * check */, 
				reiser4_tree *tree UNUSED_ARG /* tree to check
							       * against */ )
{
	assert( "nikita-757", block != NULL );
	assert( "nikita-758", tree != NULL );

	/* XXX add some sensible checks here */
	/* check to see if it exceeds the size of the device. */
	return 1;
}

/** check consistency of fields */
static int sanity_check( cbk_handle *h /* search handle */ )
{
	assert( "nikita-384", h != NULL );

	if( h -> level < h -> slevel ) {
		h -> error = "Buried under leaves";
		h -> result = CBK_IO_ERROR;
		return LLR_DONE;
	} else if( !block_nr_is_correct( &h -> block, h -> tree ) ) {
		h -> error = "bad block number";
		h -> result = CBK_IO_ERROR;
		return LLR_DONE;
	} else
		return 0;
}

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
