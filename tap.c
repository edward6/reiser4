/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Tree Access Pointers.
 */

#include "forward.h"
#include "debug.h"
#include "coord.h"
#include "tree.h"
#include "tap.h"
#include "znode.h"
#include "tree_walk.h"

int tap_load( tap_t *tap )
{
	if( tap -> loaded == 0 ) {
		int result;

		result = zload( tap -> lh -> node );
		if( result != 0 )
			return result;
	}
	++ tap -> loaded;
	return 0;
}

void tap_relse( tap_t *tap )
{
	-- tap -> loaded;
	if( tap -> loaded == 0 )
		zrelse( tap -> lh -> node );
}

void tap_init( tap_t *tap, coord_t *coord, 
	       lock_handle *lh, znode_lock_mode mode )
{
	tap -> coord = coord;
	tap -> lh = lh;
	tap -> mode = mode;
	tap -> loaded = 0;
	tap_list_clean( tap );
}

void tap_monitor( tap_t *tap )
{
	assert( "nikita-2623", tap != NULL );
	tap_list_push_front( taps_list(), tap );
}

void tap_done( tap_t *tap )
{
	assert( "nikita-2565", tap != NULL );
	assert( "nikita-2566", tap -> coord -> node == tap -> lh -> node );
	done_lh( tap -> lh );
	if( tap -> loaded > 0 )
		zrelse( tap -> lh -> node );
	tap -> loaded = 0;
	tap_list_remove_clean( tap );
	tap -> coord -> node = NULL;
}

int tap_move( tap_t *tap, lock_handle *target )
{
	int result = 0;

	assert( "nikita-2567", tap != NULL );
	assert( "nikita-2568", target != NULL );
	assert( "nikita-2570", target -> node != NULL );
	assert( "nikita-2569", tap -> coord -> node == tap -> lh -> node );

	if( tap -> loaded > 0 )
		result = zload( target -> node );
	
	if( result == 0 ) {
		if( tap -> loaded > 0 )
			zrelse( tap -> coord -> node );
		done_lh( tap -> lh );
		copy_lh( tap -> lh, target );
		tap -> coord -> node = target -> node;
	}
	return result;
}

int tap_to( tap_t *tap, znode *target )
{
	int result;
	
	assert( "nikita-2624", tap != NULL );
	assert( "nikita-2625", target != NULL );

	result = 0;
	if( tap -> coord -> node != target ) {
		lock_handle here;

		init_lh( &here );
		result = longterm_lock_znode( &here, target, tap -> mode,
					      ZNODE_LOCK_HIPRI );
		if( result == 0 ) {
			result = tap_move( tap, &here );
			done_lh( &here );
		}
	}
	return result;
}

int tap_to_coord( tap_t *tap, coord_t *target )
{
	int result;

	result = tap_to( tap, target -> node );
	if( result == 0 )
		coord_dup( tap -> coord, target );
	return result;
}

tap_list_head *taps_list()
{
	return &get_current_context() -> taps;
}

int go_dir_el( tap_t *tap, sideof dir, int units_p )
{
	coord_t  dup;
	coord_t *coord;
	int      result;

	int ( *coord_dir )( coord_t * );
	int ( *get_dir_neighbor )( lock_handle *, znode *, int, int );
	void ( *coord_init )( coord_t *, const znode * );
	ON_DEBUG( int ( *coord_check )( const coord_t * ) );

	assert( "nikita-2556", tap != NULL );
	assert( "nikita-2557", tap -> coord != NULL );
	assert( "nikita-2558", tap -> lh != NULL );
	assert( "nikita-2559", tap -> coord -> node != NULL );

	if( dir == LEFT_SIDE ) {
		coord_dir = units_p ? coord_prev_unit : coord_prev_item;
		get_dir_neighbor = reiser4_get_left_neighbor;
		coord_init = coord_init_last_unit;
	} else {
		coord_dir = units_p ? coord_next_unit : coord_next_item;
		get_dir_neighbor = reiser4_get_right_neighbor;
		coord_init = coord_init_first_unit;
	}
	ON_DEBUG( coord_check = 
		  units_p ? coord_is_existing_unit : coord_is_existing_item );
	assert( "nikita-2560", coord_check( tap -> coord ) );

	coord = tap -> coord;
	coord_dup( &dup, coord );
	if( coord_dir( &dup ) != 0 ) {
		do {
			/*
			 * move to the left neighboring node
			 */
			lock_handle dup;

			init_lh( &dup );
			result = get_dir_neighbor( &dup, coord -> node, 
						   ( int ) tap -> mode, 
						   GN_DO_READ );
			if( result == 0 ) {
				result = tap_move( tap, &dup );
				if( result == 0 )
					coord_init( tap -> coord, dup.node );
				done_lh( &dup );
			}
			/*
			 * skip empty nodes
			 */
		} while( ( result == 0 ) && node_is_empty( coord -> node ) );
	} else {
		result = 0;
		coord_dup( coord, &dup );
	}
	assert( "nikita-2564", ergo( !result, coord_check( tap -> coord ) ) );
	return result;
}

int go_next_unit( tap_t *tap )
{
	return go_dir_el( tap, RIGHT_SIDE, 1 );
}

int go_prev_unit( tap_t *tap )
{
	return go_dir_el( tap, LEFT_SIDE, 1 );
}

int rewind_to( tap_t *tap, go_actor_t actor, int shift )
{
	int    result;

	assert( "nikita-2555", shift >= 0 );
	assert( "nikita-2562", tap -> coord -> node == tap -> lh -> node );

	result = tap_load( tap );
	if( result != 0 )
		return result;

	for( ; shift > 0 ; -- shift ) {
		result = actor( tap );
		assert( "nikita-2563", 
			tap -> coord -> node == tap -> lh -> node );
		if( result != 0 )
			break;
	}
	tap_relse( tap );
	return result;
}

int rewind_right( tap_t *tap, int shift )
{
	return rewind_to( tap, go_next_unit, shift );
}

int rewind_left( tap_t *tap, int shift )
{
	return rewind_to( tap, go_prev_unit, shift );
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
