/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*

In 3.6 we balanced with every operation, and we balanced only considering the
immediate neighbors.

In version 4, tree operations that delete from a node (freeing space) do not
immediately re-balance their vicinity in the tree.  Instead, the tree is
balanced prior to flushing from memory.

The tree is not always balanced--there is not a balancing criterion that is
always met--instead it is a "dancing tree".  (A dancer has a sense of balance,
but is not balanced most of the time.)

EXTENT FORMATION: After an unformatted level slum is block allocated, its
blocks are converted into extents.  These consist of blocknumber and number of
blocks.  The big question is, how do we detect and convert these pages into
extents.  I think the answer is using a special range of reserved
blocknumbers, which must be unique so that zget() still works.

*/

#include "reiser4.h"

#if 0
/**
 * Scan all nodes of squeezable slum and squeeze everything to the left.
 *
 * Go ahead and read comments in the body.
 */
int balance_level_slum (slum_scan *scan)
{
	int                  result;
	znode               *target;
	znode               *frontier;

	int                  i;

	reiser4_lock_handle  lh_area[ 2 ];
	reiser4_lock_handle *target_lh;
	reiser4_lock_handle *source_lh;

	carry_node          *addc;

	carry_pool           pool;
	/** slum_level: carry queue where locked nodes are accumulated. */
	carry_level          slum_level;
	/** todo: carry queue where delayed operations to be performed on the
	 * parent level are accumulated for batch processing.  */
	carry_level          todo;

	frontier = target = JZNODE (scan -> node);

	/*
	 * there is no need to take any kind of lock on @frontier: it can only
	 * be different from @target due to carry batching leaving empty nodes
	 * until queue flush, but this means, that @frontier vicinity is
	 * write-locked. 
	 *
	 * When carry queue is flushed, all pending-deletion nodes are
	 * deleted, but @frontier is pinned.
	 */
	zref( frontier );

	/* initialize data structures */
	for( i = 0 ; i < sizeof_array( lh_area ) ; i += 1 ) {
		init_lh( &lh_area[ i ] );
	}

	target_lh = &lh_area[ 0 ];
	source_lh = &lh_area[ 1 ];

	init_carry_pool( &pool );
	init_carry_level( &slum_level, &pool );
	init_carry_level( &todo, &pool );

	/* lock target */
	result = longterm_lock_znode( target_lh, target, ZNODE_WRITE_LOCK, 
				      ZNODE_LOCK_LOPRI );
	if( result != 0 ) {
		goto done;
	}

	result = zload( target );
	if( result != 0 )
		goto done;

	addc = add_to_carry( target, &slum_level );
	if ( IS_ERR( addc ) ) {
		result = PTR_ERR( addc );
		goto done;
	}

	/* 
	 * iterate over all nodes in a slum, squeezing them to the left along
	 * the way. Two variables: @source and @target indicate where we are
	 * in a slum. @source is right neighbor of the @target. As much as
	 * possible of the content of @source is shifted into @target on each
	 * iteration. Updates to the parent nodes are deferred and processed
	 * in batches by flushing @todo carry queue where they are
	 * accumulated. 
	 */
	while( result == 0 ) {

		znode *source;

		assert ("jmacd-1034", znode_is_connected (target));

		/* FIXME_JMACD: We need to avoid fusing atoms due to a
		 * WRITE_LOCK request without modification, otherwise the
		 * scan->atom != target->atom test will never do anything.
		 * Think about that.  Realistically, we can check in_slum
		 * without getting the long-term lock.  That's a decent
		 * solution.  Capturing after the lock request leads to
		 * blocking with a lock held -- to be avoided for reasons
		 * discussed at the top of lock.c
		 *
		 * FIXME-NIKITA initially line below read just
		 *
		 * source = frontier->right;
		 *
		 * Some reason to change it to the long term lock came after
		 * discussing with Josh, but details escape my memory right
		 * now. Maybe something related to the possibility of new
		 * nodes being concurrently inserted between @frontier and
		 * @source once tree lock is released, or something close to
		 * this.
		 *
		 */
		result = reiser4_get_right_neighbor (source_lh, frontier, 
						     ZNODE_WRITE_LOCK, 0);
		if (result != 0) {
			if (result == -ENAVAIL || result == -ENOENT) {
				result = 0;
			}
			break;
		}

		
		source = source_lh->node;

		assert( "nikita-1497", source_lh -> node == source );
		assert( "nikita-1498", target_lh -> node == target );

		/* check if its actually in the slum... */
		if (! znode_is_connected (target) || ! znode_is_dirty (target) || scan->atom != znode_get_atom (target)) {
			break;
		}

		addc = add_to_carry( source, &slum_level );
		if( IS_ERR( addc ) ) {
			result = PTR_ERR( addc );
			break;
		}

		result = zload( source );
		if( result != 0 )
			break;

		assert ("jmacd-1050", ! is_empty_node (target));
		assert ("jmacd-1051", ! is_empty_node (source));

		assert( "nikita-1499", znode_is_write_locked( source ) );
		assert( "nikita-1500", znode_is_write_locked( target ) );
		assert( "nikita-1390", 
			ZF_ISSET( frontier, ZNODE_HEARD_BANSHEE ) ||
			znode_is_write_locked( frontier ) );

		assert( "nikita-1501", source -> lock.nr_readers < -1 );
		assert( "nikita-1502", target -> lock.nr_readers < -1 );

		assert( "nikita-1386", source != target );
		/*
		 * each node in the vicinity of @frontier is either write
		 * locked or orphaned.
		 */
		assert( "nikita-1387", frontier -> right == source );
		/* pack part of @source into @target */
		/* at this point both source and target are write locked */
		result = shift_everything_left( source, target, &todo );

		/* result >= indicates the number of _something_ shifted. */
		if( result >= 0 ) {
			
			result = 0;
			if( !is_empty_node( source ) ) {
				/* move to the next node */
				/* at this point "target" is hopefully well
				   packed. */
				reiser4_lock_handle *tmp = target_lh;
				zput( frontier );
				frontier = target = source;
				zref( frontier );
				target_lh = source_lh;
				source_lh = tmp;
				source = source_lh -> node;
			} else {
				/* 
				 * empty @source was freed already by
				 * shift_everything_left(). Deletion of a
				 * pointer in a parent was batched in @todo
				 * queue.  
				 */
				zput( frontier );
				frontier = source;
				zref( frontier );
			}
			/* 
			 * if there are more pending operations in a @todo
			 * queue than some preconfigured limit, or if we are
			 * keeping more nodes locked than some other limit,
			 * flush todo queue.
			 */
			if( ( carry_op_num( &todo ) > 
			      REISER4_SQUEEZE_OP_MAX ) ||
			    ( carry_node_num( &slum_level ) > 
			      REISER4_SQUEEZE_NODE_MAX ) ) {
				/*
				 * batch-run all pending operations.
				 */
				result = carry( &todo, &slum_level );
				reiser4_stat_slum_add( flush_carry );
				/*
				 * reinitialise carry pool, so that 
				 * new operations will be gathered here.
				 */
				done_carry_pool( &pool );
				init_carry_pool( &pool );
				init_carry_level( &slum_level, &pool );
				init_carry_level( &todo, &pool );
				addc = add_to_carry( target, 
							     &slum_level );
				if ( IS_ERR( addc ) )
					result = PTR_ERR( addc );
			}
		}
		zrelse( source, 1 );
		longterm_unlock_znode( source_lh );
	}

 done:
	
	zrelse( target, 1 );
	zput( frontier );

	/*
	 * unlock on reverse order
	 */
	for( i = sizeof_array( lh_area ) - 1 ; i >= 0 ; i -= 1 ) {
		done_lh( &lh_area[ i ] );
	}

	/* pick up operations still in @todo queue and perform them. */
	if (result == 0) {
		result = carry( &todo, &slum_level );
	} else
		warning( "nikita-1503", "Post squeezing carry skipped: %i",
			 result );
	
	done_carry_pool( &pool );

	reiser4_stat_slum_add( squeeze );
	return result;
}
#endif

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
