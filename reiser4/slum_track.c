/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README.
 */

/*
 * Slum tracking code. A slum is a set of dirty unsqueezed nodes contiguous in the tree
 * order.  Slums are the main device used to implement "super balancing", which is,
 * roughly speaking, the process of squeezing slums to the left, described in balance.c.
 *
 * Our current approach is to track slums all of the time.  Slum objects are dynamically
 * allocated, and each dirty znode points to its slum.  We use sibling pointers of the
 * znodes as the slum ordering.
 *
 * Nodes are either added to the slum at the time they are write-locked (Nikita's
 * proposal) or at the time they are modified (which requires some possibly bug-prone
 * additional work).  At that time, new slums are created or existing slums are modified
 * or merged.  More sophisticated flushing techniques will require ability to split one
 * slum into two when node is flushed to the disk in the middle of transaction.
 *
 * We do not maintain a tree-wide record of all slums in existance, instead each atom
 * maintains a level-specific lists of its captured znodes.  Znodes refer to their slum,
 * so the atom can find all slums when trying to commit.  This solution simplifies slum
 * management (especially w.r.t. locking) at the possible cost of extra list-scanning
 * during pre-commit.
 *
 * When we decide to perform super balancing (balance.c) of an atom, all slums are scanned
 * from leaf level upward and squeezed, if possible.
 *
 * Possible optimizations deferred for now:
 *
 * * to not put every dirty node into a slum
 *
 * * make decision of whether to squeeze a slum dependent on not just whether a nodeful of
 *   space could be saved without additional disk IO but upon more complex criteria such
 *   as the amount of CPU bandwidth required to save the nodeful.  */

#include "reiser4.h"

static kmem_cache_t *slum_slab;

static slum* alloc_slum( txn_atom *atom );
static void  dealloc_slum( slum *hood );
static slum* merge_slums( slum *left, slum *right );
static void  update_leftmost( slum *hood, znode *node );

/** Called once on reiser4 initialisation. */
int slums_init( void )
{
	slum_slab = kmem_cache_create( "slum_cache", sizeof( slum ),
				       0, SLAB_HWCACHE_ALIGN, NULL, NULL );
	if( slum_slab == NULL ) {
		return -ENOMEM;
	}

	return 0;
}

/** called during system shutdown */
int slums_done( void )
{
	if ( slum_slab != NULL) {
		return kmem_cache_destroy( slum_slab );
	}

	return 0;
}

/** helper function: set fields in "node" indicating it's now in
    "hood". Update counter of members in "hood", update free space
    in "hood". */
static void add_node_to_slum (znode *node, slum *hood)
{
 	assert( "jmacd-1013", node -> zslum == NULL );

	node->zslum = hood;

	hood->free_space   += znode_save_free_space (node);
	hood->num_of_nodes += 1;

	trace_on (TRACE_SLUM, "add to slum: %llu level %u slum %p nodes %u leftmost %u\n",
		  node->blocknr.blk, znode_get_level (node), hood, hood->num_of_nodes, (int)hood->leftmost->blocknr.blk);
}

void delete_node_from_slum (znode *node)
{
	slum *hood = node->zslum;
	unsigned free_space;
	
	assert ("jmacd-1026", hood != NULL);
	assert ("jmacd-1028", hood->num_of_nodes != 0); 
	assert ("jmacd-1030", znode_is_connected (node));
	assert ("jmacd-1031", spin_tree_is_locked (current_tree));

	free_space = znode_recover_free_space (node);

	assert ("jmacd-1029", hood->free_space >= free_space);
	
	hood->free_space   -= free_space;
	hood->num_of_nodes -= 1;
	node->zslum         = NULL;

	trace_on (TRACE_SLUM, "delete from slum: %llu level %u slum %p nodes %u\n", node->blocknr.blk, znode_get_level (node), hood, hood->num_of_nodes);

	if (hood->num_of_nodes == 0) {
		assert ("jmacd-1038", hood->free_space == 0);

		dealloc_slum (hood);
		
	} else if (hood->leftmost == node) {

		assert ("jmacd-1032", node->left == NULL || node->left->zslum != hood);

		update_leftmost (hood, hood->leftmost->right);

		assert ("jmacd-1039", hood->leftmost->zslum == hood);
	}
}

/** set ->leftmost pointer in hood to node */
static void update_leftmost( slum *hood, znode *node )
{
	assert( "nikita-1272", hood != NULL );
	assert( "nikita-1273", node != NULL );

	if( hood -> leftmost != NULL ) {
		zput( hood -> leftmost );
	}
	
	hood -> leftmost = zref( node );
}

/** Add "node" to slum. This is called after node content was modified, for example as a
 * result of lazy balancing. This can result in slum merging, etc. Only call this on
 * connected, write-locked node. */
int add_to_slum( znode *node )
{
	znode *left_neighbor;
	znode *right_neighbor;
	
	slum *left;             /* our left neighboring slum, if any */
	slum *right;            /* our right neighboring slum, if any */
	slum *new_slum = NULL; 	/* new slum we are creating, if any */
	slum *node_slum; 	/* node's slum */

	trace_stamp( TRACE_SLUM );

	assert( "nikita-816", node != NULL );
	assert( "jmacd-1057", node->atom != NULL );

	/* Calling add_to_slum implies that we hold a write_lock. */
 	assert( "jmacd-767", znode_is_write_locked( node ) );

	/* Need the tree lock to check slum pointer, connected bit, and head_of_slum
	 * status.  Note that we do not take znode locks below because the tree_lock
	 * protects the slum data structures.  We CANNOT take znode spinlocks while
	 * holding the tree lock or else deadlock. */
 	assert( "jmacd-709", spin_tree_is_locked( current_tree ));
 	assert( "nikita-823", znode_is_connected( node ) );

	/* If node is already in a slum, this shouldn't be called. */
	assert( "jmacd-1035", node -> zslum == NULL );

 restart:
	/* Restart point: Restarts are because allocation can sleep and we cannot hold
	 * tree lock during this. */

	if( node -> left ) {
		left_neighbor = zref( node -> left );
	} else {
		left_neighbor = NULL;
	}

	if( node -> right ) {
		right_neighbor = zref( node -> right );
	} else {
		right_neighbor = NULL;
	}

	assert( "nikita-826", node != right_neighbor );
	assert( "nikita-828", node != left_neighbor );

	left  = left_neighbor  ? left_neighbor  -> zslum : NULL;
	right = right_neighbor ? right_neighbor -> zslum : NULL;

	/* check if neighboring slums refuse merges  */
	if( left != NULL && ( ( left -> flags & SLUM_BEING_SQUEEZED ) ||
			      ( left -> atom != node -> atom ) ) ) {
		left = NULL;
	}
	/* FIXME_JMACD what kind of node/atom locking is required, if any? -josh */
	if( right != NULL && ( ( right -> flags & SLUM_BEING_SQUEEZED ) ||
			       ( right -> atom != node -> atom ) ) ) {
		right = NULL;
	}
	
	/* four cases:
	   (0==00) left == NULL, right == NULL => create new slum
	   (1==01) left == NULL, right != NULL => append node to right
	   (2==10) left != NULL, right == NULL => append node to left
	   (3==11) left != NULL, right != NULL => merge left, node, right
	   
	   following switch handles all cases
	*/
	switch( ( left ? 2 : 0 ) | ( right ? 1 : 0 ) ) {
	case 0: /* create new slum */
		if( new_slum == NULL ) {
			/* Unlock tree before possible schedule. */
			spin_unlock_tree( current_tree );

			/* Release neighbors. */
			if (left_neighbor) {
				zput( left_neighbor );
			}
			if (right_neighbor) {
				zput( right_neighbor );
			}

			/* Allocate new slum. */
			new_slum = alloc_slum( node -> atom );
			
			if( new_slum == NULL ) {
				return -ENOMEM;
			}

			spin_lock_tree( current_tree );

			/* Restart in case of a race. */
			goto restart;
		}

		/* Set right so the fall through case works, unset new_slum so it is not
		 * deleted. */
		right    = new_slum;
		new_slum = NULL;
		/* FALL THROUGH */

	case 1: /* prepend node to the right neighboring slum */
		update_leftmost( right, node );
		node_slum = right;
		break;

	case 3: /* merge slums */
		if (left != right) {
			node_slum = merge_slums( left, right );
			break;
		}
		/* FALL THROUGH: (add to middle of a slum) */

	case 2: /* node to the left neighboring slum */
		node_slum = left;
		break;
	default:
		impossible( "nikita-825", "Absolutely impossible" );
	}

	add_node_to_slum( node, node_slum );
	
	/* Release neighbor node references. */

	if( left_neighbor ) {
		zput( left_neighbor );
	}
	if( right_neighbor ) {
		zput( right_neighbor );
	}
	if( new_slum != NULL ) {
		dealloc_slum( new_slum );
	}

	return 0;
}

/** merges any neighboring slums into one as a result of atom fusion */
void slum_merge_neighbors (znode *node, txn_atom *dying, txn_atom *growing)
{
	slum *hood;
	
	trace_stamp( TRACE_SLUM );

	spin_lock_tree (current_tree);

	hood = node->zslum;

	if (znode_is_connected (node) && hood != NULL && (hood->flags & SLUM_BEING_SQUEEZED) == 0) {

		znode *left  = node->left;
		znode *right = node->right;
		slum  *lhood;
		slum  *rhood;

		assert ("jmacd-1060", hood->num_of_nodes > 1 || hood->leftmost == node);

		trace_on (TRACE_SLUM, "merge neighbor: %p node %u nodes %u leftmost %u\n", hood, (int)node->blocknr.blk, hood->num_of_nodes, (int)hood->leftmost->blocknr.blk);

		if (right != NULL && (rhood = right->zslum) != NULL && rhood->atom == growing && hood != rhood && (rhood->flags & SLUM_BEING_SQUEEZED) == 0) {

			assert ("jmacd-904", right->left == node); 

			hood = merge_slums (hood, rhood);
		}

		if (left != NULL && (lhood = left->zslum) != NULL && lhood->atom == growing && hood != lhood && (lhood->flags & SLUM_BEING_SQUEEZED) == 0) {

			assert ("jmacd-905", left->right == node); 

			hood = merge_slums (lhood, hood);
		}
	}

	if (hood != NULL) {
		trace_on (TRACE_SLUM, "merge neighbor: slum %p old atom %u new atom %u\n", hood, dying->atom_id, growing->atom_id);

		hood->atom = growing;
	}

	spin_unlock_tree (current_tree);
}

/** merge slums: holding tree lock */
static slum* merge_slums (slum *left, slum *right)
{
	slum  *large;
	slum  *small;
	znode *node;
	unsigned small_nodes = 0;

	assert ("jmacd-910", left != right);
	assert ("jamcd-918", (right->flags & SLUM_BEING_SQUEEZED) == 0);
	assert ("jamcd-919", (left->flags & SLUM_BEING_SQUEEZED) == 0);

	trace_stamp (TRACE_SLUM);

	if (left->num_of_nodes > right->num_of_nodes) {
		large = left;
		small = right;
	} else {
		small = left;
		large = right;
	}

	trace_on (TRACE_SLUM, "merge left: %p nodes %u leftmost %u\n", left, left->num_of_nodes, (int)left->leftmost->blocknr.blk);
	trace_on (TRACE_SLUM, "merge right: %p nodes %u leftmost %u\n", right, right->num_of_nodes, (int)right->leftmost->blocknr.blk);
		
	for (node = small->leftmost; is_in_slum (node, small); node = node->right) {

		assert ("jmacd-906", node->right == NULL || node->right->left == node);

		/*trace_on (TRACE_SLUM, "small node: %u\n", (int)node->blocknr.blk);*/
		
		node->zslum = large;
		small_nodes += 1;
	}

	large->free_space   += small->free_space;
	large->num_of_nodes += small->num_of_nodes;

	if (large == right) {
		zput (large->leftmost);
		large->leftmost = left->leftmost;
	} else {
		zput (small->leftmost);
	}

	trace_on (TRACE_SLUM, "merge result: slum %p nodes %u leftmost %u\n", large, large->num_of_nodes, (int)large->leftmost->blocknr.blk);
	
	assert ("jmacd-907", small_nodes == small->num_of_nodes);

	dealloc_slum (small);

	return large;
}

/** allocate new slums */
static slum *alloc_slum( txn_atom *atom )
{
	slum *hood;

	trace_stamp( TRACE_SLUM );

	hood = kmem_cache_alloc( slum_slab, GFP_KERNEL );
	memset( hood, 0, sizeof *hood );
	hood->atom = atom;
	return hood;
}

/** free slum data structure */
static void dealloc_slum( slum *hood )
{
	assert( "nikita-831", hood != NULL );

	trace_stamp( TRACE_SLUM );

	trace_on (TRACE_SLUM, "dealloc slum: %p\n", hood);
	
	kmem_cache_free( slum_slab, hood );
}

/** checks whether given node belongs to given slum.  must have tree locked. */
int is_in_slum( znode *node, slum *hood )
{
	assert ("jmacd-1090", ergo( node != NULL,
				    spin_tree_is_locked( current_tree ) ) );
	return ( node != NULL ) && ( node -> zslum == hood );
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
