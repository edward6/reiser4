/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*

Because we balance one level at a time, we need for each level to be able to change the delimiting keys of the nodes of
that level independently of the levels above it.  This is a questionable design decision to start with.  Then we add to
this the questionable design decision to replicate the delimiting keys in two places: each first key of an item in a
node is present as the left key of the znode, and as the right key of the left neigboring node's znode.  This is
arguably more efficient for some node layouts.  Maybe those as yet unwritten node layouts should be fixed.;-) This
design needs to be justified in a seminar on Monday.

 *   To simplify balancing, allow some flexibility in locking and speed up
 *   important coord cache optimization, we keep delimiting keys of nodes in
 *   memory. Depending on disk format (implemented by appropriate node plugin)
 *   node on disk can record both left and right delimiting key, only one of
 *   them, or none. Still, our balancing and tree traversal code keep both
 *   delimiting keys for a node that is in memory stored in the znode. When node is first brought into memory during
 *   tree traversal, its left delimiting key is taken from its parent, and its
 *   right delimiting key is either next key in its parent, or is right
 *   delimiting key of parent if node is the rightmost child of parent.
 *
 *   Physical consistency of delimiting key is protected by special dk spin
 *   lock. That is, delimiting keys can only be inspected or modified under
 *   this lock [FIXME-NIKITA it should be probably changed to read/write
 *   lock.]. But dk lock is only sufficient for fast "pessimistic" check,
 *   because to simplify code and to decrease lock contention, balancing
 *   (carry) only updates delimiting keys right before unlocking all locked
 *   nodes on the given tree level. For example, coord-by-key cache scans LRU
 *   list of recently accessed znodes. For each node it first does fast check
 *   under dk spin lock. If key looked for is not between delimiting keys for
 *   this node, next node is inspected and so on. If key is inside of the key
 *   range, long term lock is taken on node and key range is rechecked.
 *
 * COORDINATES
 *
 *   To find something in the tree, you supply a key, and the key is resolved by coord_by_key() into a coord
 *   (coordinate) that is valid as long as the node the coord points to remains locked.  As mentioned above trees
 *   consist of nodes that consist of items that consist of units. A unit is the smallest and indivisible piece of tree
 *   as far as balancing and tree search are concerned. Each node, item, and unit can be addressed by giving its level
 *   in the tree and key occupied by this entity.  coord is a structure containing a pointer to the node, the ordinal
 *   number of the item within this node (a sort of item offset), and the ordinal number of the unit within this item.
 *
 * TREE LOOKUP
 *
 *   There are two types of access to the tree: lookup and modification.
 *
 *   Lookup is a search for the key in the tree. Search can look for either
 *   exactly the key given to it, or for the largest key that is not greater
 *   than the key given to it. This distinction is determined by "bias"
 *   parameter of search routine (coord_by_key()). coord_by_key() either
 *   returns error (key is not in the tree, or some kind of external error
 *   occurred), or successfully resolves key into coord.
 *
 *   This resolution is done by traversing tree top-to-bottom from root level
 *   to the desired level. On levels above twig level (level one above the
 *   leaf level) nodes consist exclusively of internal items. Internal item
 *   is nothing more than pointer to the tree node on the child level. On
 *   twig level nodes consist of internal items intermixed with extent
 *   items. Internal items form normal search tree structure used by
 *   traversal to descent through the tree.
 *
 * COORD CACHE
 *
 *   Tree lookup described above is expensive even if all nodes traversed are
 *   already in the memory: for each node binary search within it has to be
 *   performed and binary searches are CPU consuming and tend to destroy CPU
 *   caches.
 *
 *   To work around this, a "coord by key cache", or cbk_cache as it is called in the code, was introduced.
 *
 *   The coord by key cache consists of small list of recently accessed nodes maintained
 *   according to the LRU discipline. Before doing real top-to-down tree
 *   traversal this cache is scanned for nodes that can contain key requested.
 *
 *   The efficiency of coord cache depends heavily on locality of reference for tree accesses. Our user level
 *   simulations show reasonably good hit ratios for coord cache under most loads so far.
 *

*/
#include "reiser4.h"

/* Long-dead pseudo-code removed Tue Apr 23 17:55:24 MSD 2002. */
				/* email me the removed pseudo-code -Hans */
/**
 * Disk address (block number) never ever used for any real tree node. This is
 * used as block number of "fake" znode.
 *
 * Invalid block addresses are 0 by tradition.
 *
 */
const reiser4_block_nr FAKE_TREE_ADDR = 0ull;

#if REISER4_DEBUG
/* This list and the two fields that follow maintain the currently active
 * contexts, used for debugging purposes.  */
TS_LIST_DEFINE(context, reiser4_context, contexts_link);

static spinlock_t        active_contexts_lock;
static context_list_head active_contexts;
#endif

/* Audited by: umka (2002.06.16) */
node_plugin *node_plugin_by_coord ( const coord_t *coord )
{
	assert( "vs-1", coord != NULL );
	assert( "vs-2", coord -> node != NULL );

	return coord -> node -> nplug;
}



/** insert item into tree. Fields of "coord" are updated so
    that they can be used by consequent insert operation. */
/* Audited by: umka (2002.06.16) */
insert_result insert_by_key( reiser4_tree *tree /* tree to insert new item
						 * into */,
			     const reiser4_key *key /* key of new item */,
			     reiser4_item_data *data UNUSED_ARG /* parameters
								 * for item
								 * creation */,
			     coord_t *coord /* resulting insertion coord */,
			     lock_handle * lh /* resulting lock
						       * handle */,
			     tree_level stop_level /** level where to insert */,
			     inter_syscall_rap *ra UNUSED_ARG /* repetitive
								   * access
								   * hint */,
			     intra_syscall_rap ira UNUSED_ARG /* repetitive
								   * access
								   * hint */, 
			     __u32 flags /* insertion flags */ )
{
	int result;

	assert( "nikita-358", tree != NULL );
	assert( "nikita-360", coord != NULL );
	assert( "nikita-361", ra != NULL );

	result = coord_by_key( tree, key, coord, lh, ZNODE_WRITE_LOCK, 
			       FIND_EXACT, stop_level, stop_level, 
			       flags | CBK_FOR_INSERT);
	switch( result ) {
	default:
		break;
	case CBK_COORD_FOUND:
		result = IBK_ALREADY_EXISTS;
		break;
	case CBK_IO_ERROR:
		result = IBK_IO_ERROR;
		break;
	case CBK_OOM:
		result = IBK_OOM;
		break;
	case CBK_COORD_NOTFOUND:
		assert( "nikita-2017", coord -> node != NULL );
		result = insert_by_coord( coord, 
					  data, key, lh, ra, ira, 0/*flags*/ );
		break;
	}
	return result;
}


/** 
 * insert item by calling carry. Helper function called if short-cut
 * insertion failed 
 */
/* Audited by: umka (2002.06.16) */
static insert_result insert_with_carry_by_coord( coord_t  *coord /* coord
								     * where
								     * to
								     * insert */,
						 lock_handle *lh /* lock
									  * handle
									  * of
									  * insertion
									  * node */,
						 reiser4_item_data *data /* parameters
									  * of
									  * new
									  * item */, 
						 const reiser4_key *key /* key
									 * of
									 * new
									 * item */,
						 carry_opcode cop /* carry
								   * operation
								   * to
								   * perform */,
						 cop_insert_flag flags /* carry
									* flags */ )
{
	int result;
	carry_pool  pool;
	carry_level lowest_level;
	carry_op * op;
	carry_insert_data cdata;

	assert("umka-314", coord != NULL);

	init_carry_pool( &pool );
	init_carry_level( &lowest_level, &pool );

	op = post_carry( &lowest_level, cop, coord->node, 0 );
	if( IS_ERR( op ) || ( op == NULL ) )
		return op ? PTR_ERR (op) : -EIO;
	cdata.coord = coord;
	cdata.data  = data;
	cdata.key   = key;
	op -> u.insert.d = &cdata;
	op -> u.insert.flags = flags;
	op -> u.insert.type = COPT_ITEM_DATA;
	op -> u.insert.child = 0;
	if( lh != NULL ) {
		op -> node -> track = 1;
		op -> node -> tracked = lh;
	}	

	ON_STATS( lowest_level.level_no = znode_get_level( coord -> node ) );
	result = carry( &lowest_level, 0 );
	done_carry_pool( &pool );

	return result;
}

/**
 * form carry queue to perform paste of @data with @key at @coord, and launch
 * its execution by calling carry().
 *
 * Instruct carry to update @lh it after balancing insertion coord moves into
 * different block.
 *
 */
/* Audited by: umka (2002.06.16) */
static int paste_with_carry( coord_t *coord /* coord of paste */, 
			     lock_handle *lh /* lock handle of node
						      * where item is
						      * pasted */,
			     reiser4_item_data *data /* parameters of new
						      * item */,
			     const reiser4_key *key /* key of new item */,
			     unsigned flags /* paste flags */ )
{
	int result;
	carry_pool  pool;
	carry_level lowest_level;
	carry_op * op;
	carry_insert_data cdata;

	assert("umka-315", coord != NULL);
	assert("umka-316", key != NULL);

	init_carry_pool( &pool );
	init_carry_level( &lowest_level, &pool );

	op = post_carry( &lowest_level, COP_PASTE, coord -> node, 0 );
	if( IS_ERR( op ) || ( op == NULL ) )
		return op ? PTR_ERR (op) : -EIO;
	cdata.coord = coord;
	cdata.data  = data;
	cdata.key   = key;
	op -> u.paste.d = &cdata;
	op -> u.paste.flags = flags;
	op -> u.paste.type  = COPT_ITEM_DATA;
	if( lh != NULL ) {
		op -> node -> track = 1;
		op -> node -> tracked = lh;
	}

	ON_STATS( lowest_level.level_no = znode_get_level( coord -> node ) );
	result = carry( &lowest_level, 0 );
	done_carry_pool( &pool );

	return result;
}

/**
 * insert item at the given coord.
 *
 * First try to skip carry by directly calling ->create_item() method of node
 * plugin. If this is impossible (there is not enough free space in the node,
 * or leftmost item in the node is created), call insert_with_carry_by_coord()
 * that will do full carry().
 *
 */
/* Audited by: umka (2002.06.16) */
insert_result insert_by_coord( coord_t  *coord /* coord where to
						    * insert. coord->node has
						    * to be write locked by
						    * caller */,
			       reiser4_item_data *data /* data to be
							* inserted */, 
			       const reiser4_key *key /* key of new item */,
			       lock_handle *lh /* lock handle of write
							* lock on node */,
			       inter_syscall_rap *ra UNUSED_ARG /* repetitive
								     * access
								     * hint */,
			       intra_syscall_rap ira UNUSED_ARG /* repetitive
								     * access
								     * hint */, 
			       __u32 flags /* insertion flags */ )
{
	unsigned item_size;
	int      result;
	znode   *node;

	assert( "vs-247", coord != NULL );
	assert( "vs-248", data != NULL );
	assert( "vs-249", data -> length > 0 );
	assert( "nikita-1191", znode_is_write_locked( coord -> node ) );

	node = coord -> node;
	result = zload( node );
	if( result != 0 )
		return result;

	item_size = space_needed( node, NULL, data, 1 );
	if( item_size > znode_free_space( node ) &&
	    ( flags & COPI_DONT_SHIFT_LEFT ) &&
	    ( flags & COPI_DONT_SHIFT_RIGHT ) &&
	    ( flags & COPI_DONT_ALLOCATE ) ) {
		/*
		 * we are forced to use free space of coord->node and new item
		 * does not fit into it.
		 *
		 * This is used during flushing when we try to pack as many
		 * item into node as possible, but don't shift data from this
		 * node elsewhere. Returning -ENOSPC is "normal" here.
		 */
		result = -ENOSPC;
	} else if( ( item_size <= znode_free_space( node ) ) && 
		   !ncoord_is_before_leftmost( coord ) && 
		   ( node_plugin_by_node( node ) -> fast_insert != NULL ) &&
		   node_plugin_by_node( node ) -> fast_insert( coord ) ) {
		/*
		 * shortcut insertion without carry() overhead.
		 *
		 * Only possible if:
		 *
		 * - there is enough free space
		 *
		 * - insertion is not into the leftmost position in a node
		 *   (otherwise it would require updating of delimiting key in a
		 *   parent)
		 *
		 * - node plugin agrees with this
		 *
		 */
		int result;

		reiser4_stat_tree_add( fast_insert );
		result = node_plugin_by_node( node ) -> create_item
			( coord, key, data, NULL );
		znode_set_dirty( node );
	} else {
		/*
		 * otherwise do full-fledged carry().
		 */
		result = insert_with_carry_by_coord( coord, lh, data, 
						     key, COP_INSERT, flags );
	}
	zrelse( node );
	return result;
}

/*
 * @coord is set to leaf level and @data is to be inserted to twig level
 */
/* Audited by: umka (2002.06.16) */
insert_result insert_extent_by_coord( coord_t  *coord /* coord where to
							  * insert. coord->node
							  * has to be write
							  * locked by caller */,
				      reiser4_item_data *data /* data to be
							       * inserted */, 
				      const reiser4_key *key /* key of new item */,
				      lock_handle *lh /* lock handle of
							       * write lock on
							       * node */
				      )
{
	assert( "vs-405", coord != NULL );
	assert( "vs-406", data != NULL );
	assert( "vs-407", data -> length > 0 );
	assert( "vs-408", znode_is_write_locked( coord -> node ) );
	assert( "vs-409", znode_get_level( coord -> node ) );

	return insert_with_carry_by_coord( coord, lh, data, key, COP_EXTENT, 0/*flags*/ );
}


/**
 * Insert into the item at the given coord.
 *
 * First try to skip carry by directly calling ->paste() method of item
 * plugin. If this is impossible (there is not enough free space in the node,
 * or we are pasting into leftmost position in the node), call
 * paste_with_carry() that will do full carry().
 *
 */
/* paste_into_item */
/* Audited by: umka (2002.06.16) */
static int insert_into_item( coord_t *coord /* coord of pasting */,
			    lock_handle *lh /* lock handle on node
						     * involved */,
			    reiser4_key *key /* key of unit being pasted*/, 
			    reiser4_item_data *data /* parameters for new
						     * unit */,
			    unsigned flags /* insert/paste flags */ )
{
	int result;
	int size_change;
	node_plugin *nplug;
	item_plugin *iplug;

	assert("umka-317", coord != NULL);
	assert("umka-318", key != NULL);
	
	iplug = item_plugin_by_coord( coord );
	nplug = node_plugin_by_coord( coord );

	assert( "nikita-1480", iplug == data -> iplug );


	size_change = space_needed( coord -> node, coord, data, 0 );
	if( size_change > ( int ) znode_free_space( coord -> node ) &&
	    ( flags & COPI_DONT_SHIFT_LEFT ) &&
	    ( flags & COPI_DONT_SHIFT_RIGHT ) &&
	    ( flags & COPI_DONT_ALLOCATE ) ) {
		/*
		 * we are forced to use free space of coord->node and new data
		 * does not fit into it.
		 */
		return -ENOSPC;
	}

	/*
	 * shortcut paste without carry() overhead.
	 *
	 * Only possible if:
	 *
	 * - there is enough free space
	 *
	 * - paste is not into the leftmost unit in a node (otherwise
	 * it would require updating of delimiting key in a parent)
	 *
	 * - node plugin agrees with this
	 *
	 * - item plugin agrees with us
	 */
	if( ( size_change <= ( int ) znode_free_space( coord -> node ) ) && 
	    ( ( coord -> item_pos != 0 ) || 
	      ( coord -> unit_pos != 0 ) ||
	      ( coord -> between == AFTER_UNIT ) ) &&
	    ( coord -> unit_pos != 0 ) &&
	    ( nplug -> fast_paste != NULL ) &&
	    nplug -> fast_paste( coord ) &&
	    ( iplug -> common.fast_paste != NULL ) && 
	    iplug -> common.fast_paste( coord ) ) {
		reiser4_stat_tree_add( fast_paste );
		if( size_change > 0 )
			nplug -> change_item_size( coord, size_change );
		/*
		 * FIXME-NIKITA: huh? where @key is used?
		 */
		result = iplug -> common.paste( coord, data, NULL );
		znode_set_dirty( coord -> node );
		if( size_change < 0 )
			nplug -> change_item_size( coord, size_change );
	} else
		/*
		 * otherwise do full-fledged carry().
		 */
		result = paste_with_carry( coord, lh, data, key, flags );
	return result;
}

/** this either appends or truncates item @coord */
/* Audited by: umka (2002.06.16) */
resize_result resize_item( coord_t *coord /* coord of item being resized */, 
			   reiser4_item_data *data /* parameters of resize*/,
			   reiser4_key *key /* key of new unit */, 
			   lock_handle *lh /* lock handle of node
						    * being modified */,
			   cop_insert_flag flags /* carry flags */ )
{
	int         result;
	carry_pool  pool;
	carry_level lowest_level;
	carry_op   *op;
	znode      *node;


	assert( "nikita-362", coord != NULL );
	assert( "nikita-363", data != NULL );
	assert( "vs-245", data -> length != 0 );

	node = coord -> node;
	result = zload( node );
	if( result != 0 )
		return result;

	init_carry_pool( &pool );
	init_carry_level( &lowest_level, &pool );

	/*
	 * FIXME-NIKITA add shortcut versions here like one for insertion above.
	 * use ->fast_*() methods of node layout plugin
	 */

	if( data -> length < 0 ) {
		/*
		 * if we are trying to shrink item (@data->length < 0), call
		 * COP_CUT operation.
		 */
		op = post_carry( &lowest_level, COP_CUT, coord->node, 0 );
		if( IS_ERR( op ) || ( op == NULL ) ) {
			zrelse( node );
			return op ? PTR_ERR (op) : -EIO;
		}
		not_yet( "nikita-1263", "resize_item() can not cut data yet" );
	} else
		result = insert_into_item( coord, lh, key, data, flags );

	zrelse( node );
	return result;
}

/**
 * Given a coord in parent node, obtain a znode for the corresponding child
 */
/* Audited by: umka (2002.06.16) */
znode *child_znode( const coord_t *parent_coord /* coord of pointer to
						    * child */, 
		    int setup_dkeys_p /* if !0 update delimiting keys of
				       * child */ )
{
	znode *child;
	znode *parent;

	assert( "nikita-1374", parent_coord != NULL );
	assert( "nikita-1482", parent_coord -> node != NULL );
	assert( "nikita-1384", spin_dk_is_locked( current_tree ) );

	parent = parent_coord -> node;
	if( znode_get_level( parent ) <= LEAF_LEVEL ) {		
		/*
		 * trying to get child of leaf node
		 */
		warning( "nikita-1217", "Child of maize?" );
		print_znode( "node", parent );
		return ERR_PTR( -EIO );
	}
	if( item_is_internal( parent_coord ) ) {
		reiser4_block_nr addr;
		item_plugin *iplug;

		iplug = item_plugin_by_coord( parent_coord );		
		assert( "vs-512", iplug -> s.internal.down_link );
		iplug -> s.internal.down_link( parent_coord, NULL, &addr );

		spin_unlock_dk( current_tree );
		child = zget( current_tree, &addr, parent, 
			      znode_get_level( parent ) - 1, GFP_KERNEL );
		spin_lock_dk( current_tree );
		if( !IS_ERR( child ) && setup_dkeys_p ) {
			find_child_delimiting_keys( parent, parent_coord,
						    znode_get_ld_key( child ),
						    znode_get_rd_key( child ) );
		}
	} else {
		warning( "nikita-1483", "Internal item expected" );
		print_znode( "node", parent );
		child = ERR_PTR( -EIO );
	}
	return child;
}


/* Audited by: umka (2002.06.16) */
unsigned node_num_items (const znode * node)
{
	return node_plugin_by_node (node)->num_of_items (node);
}

/* Audited by: umka (2002.06.16) */
int node_is_empty (const znode * node)
{
	return node_plugin_by_node( node ) -> num_of_items (node) == 0;
}


/**
 * debugging aid: magic constant we store in reiser4_context allocated at the
 * stack. Used to catch accesses to staled or uninitialized contexts.
 */
static const __u32 context_magic = 0x4b1b5d0a;

/**
 * initialise context and bind it to the current thread
 *
 * This function should be called at the beginning of reiser4 part of
 * syscall.
 */
/* Audited by: umka (2002.06.16) */
int init_context( reiser4_context *context /* pointer to the reiser4 context
					    * being initalised */, 
		  struct super_block *super /* super block we are going to
					     * work with */ )
{
	reiser4_tree *tree;
	reiser4_super_info_data *sdata;
	__u32 tid;

	if (context == NULL || super == NULL) {
		BUG ();
	}

	xmemset( context, 0, sizeof *context );

	tid = set_current ();
	if( REISER4_DEBUG && current -> journal_info ) {
		BUG ();
	}
	sdata = ( reiser4_super_info_data* ) super -> u.generic_sbp;
	tree  = & sdata -> tree;

	init_lock_stack( &context -> stack );

	context -> super = super;
	context -> tid   = tid;

	if( REISER4_DEBUG ) 
		context -> magic = context_magic;

	current -> journal_info = context;

	init_lock_stack( &context -> stack );

	txn_begin (context);

#if REISER4_DEBUG
	context_list_clean (context); /* to satisfy assertion */
	spin_lock (& active_contexts_lock);
	context_list_push_front (& active_contexts, context);
	spin_unlock (& active_contexts_lock);
#endif
	return 0;
}

/**
 * release resources associated with context.
 *
 * This function should be called at the end of "session" with reiser4,
 * typically just before leaving reiser4 driver back to VFS.
 *
 * This is good place to put some degugging consistency checks, like that
 * thread released all locks and closed transcrash etc.
 *
 * Call to this function is optional.
 *
 */
/* Audited by: umka (2002.06.16) */
void done_context( reiser4_context *context UNUSED_ARG /* context being
							* released */ )
{
	assert( "nikita-860", context != NULL );
	assert( "nikita-859", context -> magic == context_magic );
	assert( "jmacd-673", context -> trans == NULL );
	assert( "jmacd-1002", lock_stack_isclean (& context->stack));
	assert( "nikita-1936", no_counters_are_held() );
	assert( "vs-646", current -> journal_info == context );
	/* add more checks here */

#if REISER4_DEBUG
	/* remove from active contexts */
	spin_lock (& active_contexts_lock);
	context_list_remove (context);
	spin_unlock (& active_contexts_lock);
	current -> journal_info = NULL;
#endif
}

/* Audited by: umka (2002.06.16) */
void
init_context_mgr (void)
{
#if REISER4_DEBUG
	spin_lock_init    (& active_contexts_lock);
	context_list_init (& active_contexts);
#endif
}

#if REISER4_DEBUG
void show_context (int show_tree)
{
	reiser4_context *context;
	reiser4_tree    *tree = NULL;

	spin_lock (& active_contexts_lock);

	for (context = context_list_front (& active_contexts);
	             ! context_list_end   (& active_contexts, context);
	     context = context_list_next  (context)) {

		tree = &get_super_private (context->super)->tree;

		info ("context for thread %u", context->tid);
		print_address ("; tree root", & tree->root_block);
		info ("\n");

		show_lock_stack (context);

		info ("\n");
	}
	
	if (show_tree && (tree != NULL)) {
		/*print_tree_rec ("", tree, REISER4_NODE_PRINT_HEADER);*/
	}

	spin_unlock (& active_contexts_lock);
}
#endif

/**
 * This is called from longterm_unlock_znode() when last lock is released from
 * the node that has been removed from the tree. At this point node is removed
 * from sibling list and its lock is invalidated.
 */
/* Audited by: umka (2002.06.16) */
void forget_znode (lock_handle *handle)
{
	znode *node;

	assert ("umka-319", handle != NULL);
	
	node = handle -> node;
	assert ("vs-164", znode_is_write_locked (node));
	assert ("nikita-1280", ZF_ISSET (node, ZNODE_HEARD_BANSHEE));

	sibling_list_remove (node);
	invalidate_lock (handle);

	/* make sure that we are the only owner of this znode FIXME-NIKITA huh? This is
	   supposed to be called on the last _unlock_ rather than last zput().
	   assert ("vs-145", atomic_read (&node->x_count) == 1);
	*/
}

/**
 * This is called from zput() when last reference is dropped to the znode that
 * was removed from the tree. At this point we free corrsponding bit in bitmap.
 *
 * This is only stub for now.
 *
 * FIXME_JMACD: I strongly object to calling this (in tree.c) from zput() (in
 * znode.c) and then immediately calling zdestroy (in znode.c).  This is just
 * obfuscated.  I would like to see zdestroy() called directly from zput(), which
 * also calls this function, but I don't know what the call in unlock_carry_node
 * should really do.
 */
/* Audited by: umka (2002.06.16) */
int deallocate_znode( znode *node /* znode released */ )
{
	assert ("umka-320", node != NULL);
	assert ("nikita-1281", ZF_ISSET (node, ZNODE_HEARD_BANSHEE));
	zdestroy( node );
	return 0;
}

/**
 * Check that internal item at @pointer really contains pointer to @child.
 */
/* Audited by: umka (2002.06.16) */
int check_tree_pointer( const coord_t *pointer /* would-be pointer to
						   * @child */, 
			const znode *child /* child znode */ )
{
	assert( "nikita-1016", pointer != NULL );
	assert( "nikita-1017", child != NULL );
	assert( "nikita-1018", pointer -> node != NULL );

	assert( "nikita-1325", znode_is_any_locked( pointer -> node ) );

	if( znode_get_level( pointer -> node ) != znode_get_level( child ) + 1 )
		return NS_NOT_FOUND;

	if( ncoord_is_existing_unit( pointer ) ) {
		item_plugin     *iplug;
		reiser4_block_nr addr;

		if( item_is_internal( pointer ) ) {
			iplug = item_plugin_by_coord( pointer );
			assert( "vs-513", iplug -> s.internal.down_link );
			iplug -> s.internal.down_link( pointer, NULL, &addr );
			/*
			 * check that cached value is correct
			 */
			if( disk_addr_eq( &addr, znode_get_block( child ) ) ) {
				reiser4_stat_tree_add( pos_in_parent_hit );
				return NS_FOUND;
			}
		}
	}
	/* warning ("jmacd-1002", "tree pointer incorrect"); */
	return NS_NOT_FOUND;
}

/**
 * find coord of pointer to new @child in @parent.
 *
 * Find the &coord_t in the @parent where pointer to a given @child will
 * be in.
 *
 */
/* Audited by: umka (2002.06.16) */
int find_new_child_ptr( znode *parent /* parent znode, passed locked */,
			znode *child UNUSED_ARG /* child znode, passed locked */,
			znode *left /* left brother of new node */,
			coord_t *result /* where result is stored in */ )
{
	int ret;

	assert( "nikita-1486", parent != NULL );
	assert( "nikita-1487", child != NULL );
	assert( "nikita-1488", result != NULL );

	ret = find_child_ptr( parent, left, result );
	if( ret != NS_FOUND ) {
		warning( "nikita-1489", "Cannot find brother position: %i",
			 ret );
		return -EIO;
	} else {
		result -> between = AFTER_UNIT;
		return NS_NOT_FOUND;
	}
}


/**
 * find coord of pointer to @child in @parent.
 *
 * Find the &coord_t in the @parent where pointer to a given @child is in.
 *
 */
/* Audited by: umka (2002.06.16) */
int find_child_ptr( znode *parent /* parent znode, passed locked */,
		    znode *child /* child znode, passed locked */,
		    coord_t *result /* where result is stored in */ )
{
	int                lookup_res;
	node_plugin       *nplug;
	/* left delimiting key of a child */
	reiser4_key        ld;

	assert( "nikita-934", parent != NULL );
	assert( "nikita-935", child != NULL );
	assert( "nikita-936", result != NULL );
	assert( "zam-356", znode_is_loaded(parent)); 

	assert( "umka-321", current_tree != NULL );

	ncoord_init_zero( result );
	result -> node = parent;

	nplug = parent -> nplug;
	assert( "nikita-939", nplug != NULL );


	/*
	 * fast path. Try to use cached value. Lock tree to keep
	 * node->pos_in_parent and pos->*_blocknr consistent.
	 */
	if( child -> ptr_in_parent_hint.item_pos + 1 != 0 ) {
		reiser4_stat_tree_add( pos_in_parent_set );
		spin_lock_tree( current_tree );
		*result = child -> ptr_in_parent_hint;
		spin_unlock_tree( current_tree );
		if( check_tree_pointer( result, child ) == NS_FOUND )
			return NS_FOUND;

		reiser4_stat_tree_add( pos_in_parent_miss );
		spin_lock_tree( current_tree );
		child -> ptr_in_parent_hint.item_pos = ~0u;
		spin_unlock_tree( current_tree );
	}

	/*
	 * is above failed, find some key from @child. We are looking for the
	 * least key in a child.
	 */
	spin_lock_dk( current_tree );
	ld = *znode_get_ld_key( child );
	/*
	 * now, lookup parent with key just found.
	 */
	lookup_res = nplug -> lookup( parent, &ld, FIND_EXACT, result );
	/* update cached pos_in_node */
	if( lookup_res == NS_FOUND ) {
		spin_lock_tree( current_tree );
		child -> ptr_in_parent_hint = *result;
		child -> ptr_in_parent_hint.between = AT_UNIT;
		spin_unlock_tree( current_tree );
		lookup_res = check_tree_pointer( result, child );
	}
	spin_unlock_dk( current_tree );
	if( lookup_res == NS_NOT_FOUND )
		lookup_res = find_child_by_addr( parent, child, result );
	return lookup_res;
}

/**
 * find coord of pointer to @child in @parent by scanning
 *
 * Find the &coord_t in the @parent where pointer to a given @child
 * is in by scanning all internal items in @parent and comparing block
 * numbers in them with that of @child.
 *
 */
/* Audited by: umka (2002.06.16) */
int find_child_by_addr( znode *parent /* parent znode, passed locked */, 
			znode *child /* child znode, passed locked */, 
			coord_t *result /* where result is stored in */ )
{
	int ret;

	assert( "nikita-1320", parent != NULL );
	assert( "nikita-1321", child != NULL );
	assert( "nikita-1322", result != NULL );

	assert( "umka-323", current_tree != NULL );
	
	ret = NS_NOT_FOUND;

	/* FIXME_NIKITA: The following loop is awkward.  It looks as if it
	 * ALWAYS will return NS_NOT_FOUND.  When else is ret set?
	 *
	 * Also, why not write this as:
	 *
	 * coord_first_unit();
	 * do {
	 *   ...
	 * } while (! coord_next_unit ());
	 *
	 * The for-loop test (coord_of_unit) is only useful the first time
	 * through... every other time it is redundent.
	 */
	
	for_all_units( result, parent ) {
		if( check_tree_pointer( result, child ) == NS_FOUND ) {
			spin_lock_tree( current_tree );
			child -> ptr_in_parent_hint = *result;
			spin_unlock_tree( current_tree );
			ret = NS_FOUND;
			break;
		}
	}
	return ret;
}

/**
 * true, if @addr is "unallocated block number", which is just address, with
 * highest bit set.
 */
/* Audited by: umka (2002.06.16) */
int is_disk_addr_unallocated( const reiser4_block_nr *addr /* address to
							    * check */ )
{
	assert( "nikita-1766", addr != NULL );
	cassert( sizeof( reiser4_block_nr ) == 8 );
	return (*addr & REISER4_BLOCKNR_STATUS_BIT_MASK) == REISER4_UNALLOCATED_STATUS_VALUE;
}

/**
 * convert unallocated disk address to the memory address
 *
 * FIXME: This needs a big comment.
 */
/* Audited by: umka (2002.06.16) */
void *unallocated_disk_addr_to_ptr( const reiser4_block_nr *addr /* address to
								  * convert */ )
{
	assert( "nikita-1688", addr != NULL );
	assert( "nikita-1689", is_disk_addr_unallocated( addr ) );
	return ( void * ) ( long ) ( *addr << 1 );
}

/* try to shift everything from @right to @left. If everything was shifted -
 * @right is removed from the tree.  Result is the number of bytes shifted. FIXME: right? */
/* Audited by: umka (2002.06.16) */
int shift_everything_left (znode * right, znode * left, carry_level *todo)
{
	int result;
	coord_t from;
	node_plugin * nplug;

	ncoord_init_last_unit (&from, right);

	nplug = node_plugin_by_node (right);
	result = nplug->shift (&from, left, SHIFT_LEFT,
			    1/* delete node @right if all its contents was moved to @left */,
			    1/* @from will be set to @left node */,
			    todo);
	znode_set_dirty( right );
	znode_set_dirty( left );
	return result;
}


/* allocate new node and insert a pointer to it into the tree such that new
   node becomes a right neighbor of @insert_coord->node */
/* Audited by: umka (2002.06.16) */
znode *insert_new_node (coord_t * insert_coord, lock_handle *lh)
{
	int result;
	carry_pool  pool;
	carry_level this_level, parent_level;
	carry_node * cn;
	znode * new_znode;


	init_carry_pool (&pool);
	init_carry_level (&this_level, &pool);
	init_carry_level (&parent_level, &pool);

	new_znode = ERR_PTR (-EIO);
	cn = add_new_znode (insert_coord->node, 0, &this_level, &parent_level);
	if (!IS_ERR (cn)) {
		result = longterm_lock_znode (lh, cn->real_node, ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI);
		if (!result) {
			new_znode = cn->real_node;
			result = carry (&parent_level, &this_level);
		}
		if (result)
			new_znode = ERR_PTR (result);
	} else
		new_znode = ERR_PTR (PTR_ERR (cn));

	done_carry_pool (&pool);
	return new_znode;
}


/* returns true if removing bytes of given range of key [from_key, to_key]
 * causes removing of whole item @from */
/* Audited by: umka (2002.06.16) */
static int item_removed_completely (coord_t * from,
				    const reiser4_key * from_key, 
				    const reiser4_key * to_key)
{
	item_plugin * iplug;
	reiser4_key key_in_item;

	assert("umka-325", from != NULL);
	
	/* check first key just for case */
	item_key_by_coord (from, &key_in_item);
	if (keygt (from_key, &key_in_item))
		return 0;

	/* check last key */
	iplug = item_plugin_by_coord (from);
	assert ("vs-611", iplug && iplug->common.real_max_key_inside);

	iplug->common.real_max_key_inside (from, &key_in_item);

	if (keylt (to_key, &key_in_item))
		/* last byte is not removed */
		return 0;
	return 1;
}


/* part of cut_node. It is called when cut_node is called to remove or cut part
 * of extent item. When head of that item is removed - we have to update right
 * delimiting of left neighbor of extent. When item is removed completely - we
 * have to set sibling link between left and right neighbor of removed
 * extent. This may return -EDEADLK because of trying to get left neighbor
 * locked. So, caller should repeat an attempt
 */
/* Audited by: umka (2002.06.16) */
static int prepare_twig_cut (coord_t * from, coord_t * to,
			     const reiser4_key * from_key, 
			     const reiser4_key * to_key,
			     znode * locked_left_neighbor)
{
	int result;
	reiser4_key key;
	lock_handle left_lh;
	coord_t left_coord;
	znode * left_child;
	znode * right_child;


	assert ("umka-326", from != NULL);
	assert ("umka-327", to != NULL);
	
	/* for one extent item only yet */
	assert ("vs-591", item_is_extent (from));
	assert ("vs-592", from->item_pos == to->item_pos);

	if (keygt (from_key, item_key_by_coord (from, &key))) {
		/* head of item @from is not removed, there is nothing to
		 * worry about */
		return 0;
	}

	assert ("vs-593", from->unit_pos == 0);

	ncoord_dup (&left_coord, from);
	init_lh (&left_lh);
	if (ncoord_prev_unit (&left_coord)) {
		if (!locked_left_neighbor) {
			/* @from is leftmost item in its node */
			result = reiser4_get_left_neighbor (&left_lh, from->node,
							    ZNODE_READ_LOCK,
							    GN_DO_READ);
			switch (result) {
			case 0:
				break;
			case -ENAVAIL:
				/* there is no formatted node to the left of
				 * from->node */
				warning ("vs-605", "extent item has smallest key in "
					 "the tree and it is about to be removed");
				return 0;
			case -EDEADLK:
				/* need to restart */
			default:
				return result;
			}

			/* we have acquired left neighbor of from->node */
			ncoord_init_last_unit (&left_coord, left_lh.node);
		} else {
			ncoord_init_last_unit (&left_coord, locked_left_neighbor);
		}
	}

	if (!item_is_internal (&left_coord)) {
		/* there is no left child */
		done_lh (&left_lh);
		/* what else but extent can be on twig level */
		assert ("vs-606", item_is_extent (&left_coord));
		return 0;
	}

	spin_lock_dk (current_tree);
	left_child = child_znode (&left_coord, 1/* update delimiting keys*/);
	spin_unlock_dk (current_tree);

	if (IS_ERR (left_child)) {
		return PTR_ERR (left_child);
	}


	/* left child is acquired, calculate new right delimiting key for it
	 * and get right child if it is necessary */
	if (item_removed_completely (from, from_key, to_key)) {
		/* try to get right child of removed item */
		coord_t right_coord;
		lock_handle right_lh;


		assert ("vs-607", to->unit_pos == ncoord_last_unit_pos (to));
		ncoord_dup (&right_coord, to);
		init_lh (&right_lh);
		if (ncoord_next_unit (&right_coord)) {
			/* @to is rightmost unit in the node */
			result = reiser4_get_right_neighbor (&right_lh, from->node,
							     ZNODE_READ_LOCK,
							     GN_DO_READ);
			switch (result) {
			case 0:
				ncoord_init_first_unit (&right_coord, right_lh.node);
				item_key_by_coord (&right_coord, &key);
				break;

			case -ENAVAIL:
				/* there is no formatted node to the right of
				 * from->node */
				spin_lock_dk (current_tree);
				key = *znode_get_rd_key (from->node);
				spin_unlock_dk (current_tree);
				right_coord.node = 0;
				break;
			default:
				/* real error */
				done_lh (&right_lh);
				done_lh (&left_lh);
				return result;
			}
		} else {
			/* there is an item to the right of @from - take its key */
			item_key_by_coord (&right_coord, &key);
		}

		/* try to get right child of @from */
		if (right_coord.node && /* there is right neighbor of @from */
		    item_is_internal (&right_coord)) { /* it is internal item */
			spin_lock_dk (current_tree);
			right_child = child_znode (&right_coord, 1/* update delimiting keys*/);
			spin_unlock_dk (current_tree);

			if (IS_ERR (right_child)) {
				done_lh (&right_lh);
				done_lh (&left_lh);
				return PTR_ERR (right_child);
			}

			/* link left_child and right_child */
			spin_lock_tree (current_tree);
			link_left_and_right (left_child, right_child);
			spin_unlock_tree (current_tree);

			zput (right_child);
		}
		done_lh (&right_lh);

	} else {
		/* calculate right delimiting key */
		key = *to_key;
		set_key_offset (&key, get_key_offset (&key) + 1);
		assert ("vs-608", (get_key_offset (&key) & 
			     (reiser4_get_current_sb ()->s_blocksize - 1)) == 0);
	}

	/* update right delimiting key of left_child */

	spin_lock_dk (current_tree);	
	*znode_get_rd_key (left_child) = key;
	spin_unlock_dk (current_tree);	

	zput (left_child);
	done_lh (&left_lh);
	return 0;
}


/**
 * cut part of the node
 * 
 * Cut part or whole content of node.
 *
 * cut data between @from and @to of @from->node and call carry() to make
 * corresponding changes in the tree. @from->node may become empty. If so -
 * pointer to it will be removed. Neighboring nodes are not changed. Smallest
 * removed key is stored in @smallest_removed 
 *
 */
/* Audited by: umka (2002.06.16) */
int cut_node (coord_t * from /* coord of the first unit/item that will be
				  * eliminated */, 
	      coord_t * to /* coord of the last unit/item that will be
				* eliminated */,
	      const reiser4_key * from_key /* first key to be removed */, 
	      const reiser4_key * to_key /* last key to be removed */,
	      reiser4_key * smallest_removed /* smallest key actually
					      * removed */,
	      unsigned flags /* cut flags */,
	      znode * locked_left_neighbor /* this is set when cut_node is
					    * called with left neighbor locked
					    * (in squalloc_right_twig_cut,
					    * namely) */)
{
	int result;
	carry_pool  pool;
	carry_level lowest_level;
	carry_op * op;
	carry_cut_data cdata;

	assert ("umka-328", from != NULL);
	assert ("vs-316", !node_is_empty (from->node));

	if (ncoord_eq (from, to) && !ncoord_is_existing_unit (from)) {
		assert ("nikita-1812", !ncoord_is_existing_unit (to));
		/* Napoleon defeated */
		return 0;
	}
	/* set @from and @to to first and last units which are to be removed
	   (getting rid of betweenness) */
	if (ncoord_set_to_right (from) || ncoord_set_to_left (to))
		return -EIO;

	/* make sure that @from and @to are set to existing units in the
	   node */
	assert ("vs-161", ncoord_is_existing_unit (from));
	assert ("vs-162", ncoord_is_existing_unit (to));


	if (znode_get_level (from->node) == TWIG_LEVEL && item_is_extent (from)) {
		/* left child of extent item may have to get updated right
		 * delimiting key and to get linked with right child of extent
		 * @from if it will be removed completely */
		result = prepare_twig_cut (from, to, from_key, to_key,
					   locked_left_neighbor);
		if (result)
			return result;
	}

	init_carry_pool( &pool );
	init_carry_level( &lowest_level, &pool );

	op = post_carry( &lowest_level, COP_CUT, from->node, 0 );
	if( IS_ERR( op ) || ( op == NULL ) )
		return op ? PTR_ERR (op) : -EIO;

	cdata.from = from;
	cdata.to = to;
	cdata.from_key = from_key;
	cdata.to_key = to_key;
	cdata.smallest_removed = smallest_removed;
	cdata.flags = flags;
	op->u.cut = &cdata;

	result = carry (&lowest_level, 0);
	done_carry_pool( &pool );

	return result;
}


/* there is a fundamental problem with optimizing deletes: VFS does it
   one file at a time.  Another problem is that if an item can be
   anything, then deleting items must be done one at a time.  It just
   seems clean to writes this to specify a from and a to key, and cut
   everything between them though.  */

/* use this function with care if deleting more than what is part of a single file. */
/* do not use this when cutting a single item, it is suboptimal for that */

/* You are encouraged to write plugin specific versions of this.  It
   cannot be optimal for all plugins because it works item at a time,
   and some plugins could sometimes work node at a time. Regular files
   however are not optimizable to work node at a time because of
   extents needing to free the blocks they point to.

   Optimizations compared to v3 code:

   It does not balance (that task is left to memory pressure code).

   Nodes are deleted only if empty.

   Uses extents.

   Performs read-ahead of formatted nodes whose contents are part of
   the deletion.
*/

/* Audited by: umka (2002.06.16) */
int cut_tree (reiser4_tree * tree, 
	      const reiser4_key * from_key, const reiser4_key * to_key)
{
	coord_t intranode_to, intranode_from;
	reiser4_key smallest_removed;
	lock_handle lock_handle;
	int result;
	znode * loaded;

	assert("umka-329", tree != NULL);
	assert("umka-330", from_key != NULL);
	assert("umka-331", to_key != NULL);

	ncoord_init_zero (&intranode_to);
	ncoord_init_zero (&intranode_from);
	init_lh(&lock_handle);

#define WE_HAVE_READAHEAD (0)
#if WE_HAVE_READAHEAD
	request_read_ahead_key_range(from, to, LIMIT_READ_AHEAD_BY_CACHE_SIZE_ONLY);
	/* locking? */
	spans_node = key_range_spans_node(from, to);
#endif /* WE_HAVE_READAHEAD */

	do {
		/* look for @to_key in the tree or use @to_coord if it is set
		   properly */
		result = coord_by_hint_and_key (tree, to_key,
						&intranode_to, /* was set as hint in previous loop iteration (if there was one) */
						&lock_handle,
						FIND_MAX_NOT_MORE_THAN, TWIG_LEVEL, LEAF_LEVEL);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND)
			/* -EIO, or something like that */
			break;

		loaded = intranode_to.node;
		result = zload (loaded);
		if (result)
			break;

		/* lookup for @from_key in current node */
		assert ("vs-686", intranode_to.node->nplug);
		assert ("vs-687", intranode_to.node->nplug->lookup);
		result = intranode_to.node->nplug->lookup (intranode_to.node,
							   from_key, FIND_EXACT,
							   &intranode_from);
               
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* -EIO, or something like that */
			zrelse (loaded);
			break;
		}


		if (ncoord_eq (&intranode_from, &intranode_to) && 
		    !ncoord_is_existing_unit (&intranode_from)) {
			/* nothing to cut */
			result = 0;
			zrelse (loaded);
			break;
		}
		/* cut data from one node */
		smallest_removed = *min_key ();
		result = cut_node (&intranode_from,
				   &intranode_to, /* is used as an input and
						     an output, with output
						     being a hint used by next
						     loop iteration */
				   from_key, to_key, &smallest_removed, DELETE_KILL/*flags*/, 0);
		zrelse (loaded);
		if (result)
			break;
		assert ("vs-301", !keyeq (&smallest_removed, min_key ()));
	} while (keygt (&smallest_removed, from_key));

	done_lh(&lock_handle);

	return result;
}


#if REISER4_DEBUG

/** helper called by print_tree_rec() */
static void tree_rec( reiser4_tree *tree /* tree to print */, 
		      znode *node /* node to print */, 
		      __u32 flags /* print flags */ )
{
	int ret;
	coord_t coord;

	ret = zload( node );
	if( ret != 0 ) {
		info( "Cannot load/parse node: %i", ret );
		return;
	}

	if( flags & REISER4_NODE_PRINT_ZNODE )
		print_znode( "", node );

	print_znode_content( node, flags );
	if( node_is_empty( node ) ) {
		indent_znode( node );
		info( "empty\n" );
		zrelse( node );
		return;
	}

	if( flags & REISER4_NODE_CHECK )
		node_check( node, flags );

	if( flags & REISER4_NODE_PRINT_HEADER && znode_get_level( node ) != LEAF_LEVEL ) {
		print_address( "children of node", znode_get_block( node ) );
	}

	for( ncoord_init_before_first_item( &coord, node ); ncoord_next_item( &coord ) == 0; ) {

		if( item_is_internal(&coord ) ) {
			znode *child;

			spin_lock_dk( current_tree );
			child = child_znode( &coord, 0 );
			spin_unlock_dk( current_tree );
			if( !IS_ERR( child ) ) {
				tree_rec( tree, child, flags );
				zput( child );
			} else {
				info( "Cannot get child: %li\n", 
				      PTR_ERR( child ) );
			}
		}
	}
	if( flags & REISER4_NODE_PRINT_HEADER && znode_get_level( node ) != LEAF_LEVEL ) {
		print_address( "end children of node", znode_get_block( node ) );
	}
	zrelse( node );
}

/**
 * debugging aid: recursively print content of a @tree.
 */
void print_tree_rec (const char * prefix /* prefix to print */, 
		     reiser4_tree * tree /* tree to print */, 
		     __u32 flags /* print flags*/ )
{
	znode *fake;
	znode *root;

	if( ( flags & ( unsigned ) ~REISER4_NODE_CHECK ) != 0 )
		info( "tree: [%s]\n", prefix );
	fake = zget( tree, &FAKE_TREE_ADDR, NULL, 0, GFP_KERNEL );
	if( IS_ERR( fake ) ) {
		info( "Cannot get fake\n" );
		return;
	}
	root = zget( tree, &tree -> root_block, fake, tree -> height, 
		     GFP_KERNEL );
	if( IS_ERR( root ) ) {
		info( "Cannot get root\n" );
		return;
	}
	tree_rec( tree, root, flags );

#if REISER4_USER_LEVEL_SIMULATION
	if( ! ( flags & REISER4_NODE_DONT_DOT ) ) {
		char path[ 1024 ];
		FILE *dot;
		extern void tree_rec_dot( reiser4_tree *, znode *, __u32, FILE * );

		sprintf( path, "/tmp/%s.dot", prefix );
		dot = fopen( path, "w+" );
		if( dot != NULL ) {
			fprintf( dot,
				 "digraph L0 {\n"
				 "ordering=out;\n"
				 "node [shape = box];\n" );
			tree_rec_dot( tree, root, flags, dot );
			fprintf( dot, "}\n" );
			fclose( dot );
		}
	}
#endif
	if( ( flags & ( unsigned ) ~REISER4_NODE_CHECK ) != 0 )
		info( "end tree: [%s]\n", prefix );
	zput( root );
	zput( fake );
}


/** Debugging aid: print information about inode. */
void print_inode( const char *prefix /* prefix to print */, 
		  const struct inode *i /* inode to print */ )
{
	reiser4_key         inode_key;
	reiser4_inode_info *ref;

	if( i == NULL ) {
		info( "%s: inode: null\n", prefix );
		return;
	}
	info( "%s: ino: %lu, count: %i, link: %i, mode: %o, size: %llu\n",
	      prefix, i -> i_ino, atomic_read( &i -> i_count ), i -> i_nlink,
	      i -> i_mode, ( unsigned long long ) i -> i_size );
	info( "\tuid: %i, gid: %i, dev: %i, rdev: %i\n", 
	      i -> i_uid, i -> i_gid, i -> i_dev, i -> i_rdev );
	info( "\tatime: %li, mtime: %li, ctime: %li\n",
	      i -> i_atime, i -> i_mtime, i -> i_ctime );
	info( "\tblkbits: %i, blksize: %lu, blocks: %lu\n",
	      i -> i_blkbits, i -> i_blksize, i -> i_blocks );
	info( "\tversion: %lu, generation: %i, state: %lu, flags: %u\n",
	      i -> i_version, i -> i_generation, i -> i_state,
	      i -> i_flags );
	info( "\tis_reiser4_inode: %i\n", is_reiser4_inode( i ) );
	print_key( "\tkey", build_sd_key( i, &inode_key ) );
	ref = reiser4_inode_data( i );
	print_plugin( "\tfile", file_plugin_to_plugin( ref -> file ) );
	print_plugin( "\tdir", dir_plugin_to_plugin( ref -> dir ) );
	print_plugin( "\tperm", perm_plugin_to_plugin( ref -> perm ) );
	print_plugin( "\ttail", tail_plugin_to_plugin( ref -> tail ) );
	print_plugin( "\thash", hash_plugin_to_plugin( ref -> hash ) );
	print_plugin( "\tsd", item_plugin_to_plugin( ref -> sd ) );
	print_seal( "\tsd_seal", &ref -> sd_seal );
	ncoord_print( "\tsd_coord", &ref -> sd_coord, 1 );
	info( "\tflags: %u, bytes: %llu, extmask: %llu, sd_len: %i, pmask: %i, locality: %llu\n",
	      ref -> flags, ref -> bytes, ref -> extmask, 
	      ( int ) ref -> sd_len, ref -> plugin_mask, ref -> locality_id );
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
 * scroll-step: 1
 * End:
 */
