/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

unsigned coord_num_units (const tree_coord * coord)
{
	assert ("vs-276", coord_of_item (coord));
	return item_plugin_by_coord (coord)->common.nr_units (coord);
}

unsigned last_unit_pos (const tree_coord * coord)
{
	return coord_num_units (coord) - 1;
}


/* 1 is returned if @coord's fields are of reasonable values, 0 - otherwise */
int coord_correct (const tree_coord * coord)
{
	if (coord->node == NULL)
		return 0;
	if (coord->between != AT_UNIT &&
	    coord->between != BEFORE_UNIT &&
	    coord->between != AFTER_UNIT)
		/**/
		return 0;

	if (coord->item_pos > node_num_items (coord->node))
		/* position within a node is out of range */
		return 0;

	if (node_is_empty (coord->node)) {
		/* position within an empty node can be only 0, 0 */
		if (coord->item_pos || coord->unit_pos)
			return 0;
		return 1;
	}
	if (coord->item_pos == node_num_items (coord->node)) {
		if (coord->unit_pos != 0)
			/* position within an not-existing item is not 0 */
			return 0;
		return 1;
	}

	/* coord is set to an existing item */
	if (coord->unit_pos > coord_num_units (coord))
		/* position within an item is out of range */
		return 0;
	else if (coord->unit_pos == coord_num_units (coord) && 
		 coord->between != BEFORE_UNIT)
		return 0;
	return 1;
}


/* 1 is returned if @coord->item_pos is set ot existing item within a
   @coord->node */
int coord_of_item (const tree_coord * coord)
{
	return coord->item_pos < node_num_items (coord->node);
}


/* 1 is returned if @coord is set to certain unit within a node, 0 -
   otherwise */
int coord_of_unit (const tree_coord * coord)
{
	if (coord->between != AT_UNIT)
		return 0;
	if (node_is_empty (coord->node))
		return 0;
	if (coord->item_pos >= node_num_items (coord->node))
		return 0;
	if (coord->unit_pos > last_unit_pos (coord))
		return 0;
	return 1;
}


/**
 * returns true iff @coord addresses something within a node.
 **/
int coord_is_in_node( const tree_coord *coord )
{
	pos_in_node pos;
	unsigned unit;
	unsigned items;
	unsigned units;
	between_enum tweenness;

	assert( "nikita-952", coord != NULL );
	assert( "nikita-953", coord -> node != NULL );
	assert( "nikita-954", node_plugin_by_coord( coord ) != NULL );
	
	pos = coord -> item_pos;
	unit = coord -> unit_pos;
	tweenness = coord -> between;
	items = node_num_items( coord -> node );
	if( coord_of_item( coord ) )
		units = last_unit_pos( coord ) + 1;
	else
		units = ~0u;
	if( ( pos == 0 ) && ( unit == 0 ) && ( tweenness == BEFORE_UNIT ) )
		return 0;
	if( ( pos == items - 1 ) && ( unit == units - 1 ) && 
	    ( tweenness == AFTER_UNIT ) )
		return 0;
	return ( pos < items );
}


/* this assumes that @coord is set to existing unit within a node. Move @coord
   to next unit. 1 is returned if coord is set already to last unit in the
   node. 0 - otherwise */
int coord_next_unit (tree_coord * coord)
{
	assert ("vs-199", coord_correct (coord));
	assert ("vs-200", coord_of_unit (coord));

	if (coord_is_rightmost(coord))
		return 1;
	coord->unit_pos ++;
	if (coord->unit_pos > last_unit_pos (coord)) {
		coord->item_pos ++;
		coord->unit_pos = 0;
	}
	return 0;
}


/* this assumes that @coord is set to existing unit within a node. Move @coord
   to previous unit. 1 is returned if coord is set already to first unit in
   the node. 0 - otherwise */
int coord_prev_unit (tree_coord * coord)
{
	assert ("vs-201", coord_correct (coord));
	assert ("vs-202", coord_of_unit (coord));

	if (coord->item_pos == 0 &&
	    coord->unit_pos == 0)
		return 1;
	if (coord->unit_pos) {
		coord->unit_pos --;
	} else {
		coord->item_pos --;
		coord->unit_pos = last_unit_pos (coord);		
	}
	return 0;
}


/*
 * set coord to first unit of an item
 */
void coord_first_item_unit (tree_coord * coord)
{
	coord->unit_pos = 0;
	coord->between = AT_UNIT;
}


/*
 * set coord to last unit of an item
 */
void coord_last_item_unit (tree_coord * coord)
{
	coord->unit_pos = last_unit_pos (coord);
	coord->between = AT_UNIT;
}


/*
 * set coord to first unit within a node
 */
void coord_first_unit (tree_coord * coord, znode *node)
{
	if (node != NULL) { coord->node = node; }
	coord->item_pos = 0;
	coord_first_item_unit (coord);
}


void coord_last_unit (tree_coord * coord, znode *node)
{
	if (node != NULL) { coord->node = node; }

	if (node_is_empty (coord->node)) {
		coord_first_unit (coord, node);
		return;
	}

	coord->item_pos = node_num_items (coord->node) - 1;
	coord->unit_pos = last_unit_pos (coord);
	coord->between = AT_UNIT;
}


/*
 * set coord to first unit of next item, return 0 if there is no one
 */
int coord_next_item (tree_coord * coord)
{
	assert ("vs-427", coord_correct (coord));
	assert ("vs-428", coord_of_unit (coord));

	if (coord->item_pos >= node_num_items (coord->node) - 1)
		return 0;
	coord->item_pos ++;
	coord_first_item_unit (coord);
	return 1;
}


/*
 * set coord to first unit of prev item, return 0 if there is no one
 */
int coord_prev_item (tree_coord * coord)
{
	assert ("vs-452", coord_correct (coord));
	assert ("vs-453", coord_of_unit (coord));

	if (coord->item_pos == 0)
		return 0;
	coord->item_pos --;
	coord_first_item_unit (coord);
	return 1;
}


/* return 1 if @coord is set between items, 0 - otherwise */
int coord_between_items (const tree_coord * coord)
{
	if (coord_of_item (coord)) {
		/* in node position is set to item within a node */
		if (coord->unit_pos <= last_unit_pos (coord)) {
			/* in item position is set to unit within an item */
			if (coord->unit_pos == 0) {
				if (coord->between == BEFORE_UNIT)
					return 1;
			}
			if (coord->unit_pos == last_unit_pos (coord)) {
				if (coord->between == AFTER_UNIT)
					return 1;
			}
			return 0;
		}
		/* in item position is not set to unit within an item */
	}
	/* in node position is not set to item within a node */
	return 1;
}


/* return 1 if @coord is set after last unit within @coord->node */
int coord_after_last (const tree_coord * coord)
{
	if (coord->item_pos == node_num_items (coord->node))
		return 1;
	if (coord_is_rightmost(coord) && coord->between == AFTER_UNIT)
		return 1;
	return 0;
}


/* return 1 if @coord is set before first unit within @coord->node */
int coord_before_first (const tree_coord * coord)
{
	return coord_between_items (coord) && left_item_pos (coord) == -1;
}

/* this is supposed to be used when @coord is set between items. Return value
   is position of item which is left of those neighboring items */
int left_item_pos (const tree_coord * coord)
{
	assert ("vs-208", coord_correct (coord));
	assert ("vs-197", !node_is_empty (coord->node));
	assert ("vs-152", coord_between_items (coord) == 1);

	if ((coord->item_pos == 0) && (coord->between != AFTER_UNIT))
		return -1;
	if (coord->item_pos == node_num_items (coord->node))
		return node_num_items (coord->node) - 1;

	if (coord->unit_pos == last_unit_pos (coord) && coord->between == AFTER_UNIT)
		return coord->item_pos;

	assert ("vs-159", (coord->unit_pos == 0 && coord->between == BEFORE_UNIT));
	return coord->item_pos - 1;
}


/* @coord can be set between items and between units. This sets @coord to
   existing unit nearest to the left of @coord */
int coord_set_to_left (tree_coord * coord)
{
	assert ("vs-203", coord_correct (coord));

	if (coord_between_items (coord)) {
		if (left_item_pos (coord) < 0)
			/* there is no units on the left of @coord */
			return 1;
		coord->item_pos = left_item_pos (coord);
		coord->unit_pos = last_unit_pos (coord);
	} else {
		if (coord->between == BEFORE_UNIT) {
			/* unit_pos can not be 0 because it would have to be
			   interpreted as being between items */
			assert ("vs-156", coord->unit_pos != 0);
			coord->unit_pos --;
		}
	}
	coord->between = AT_UNIT;
	return 0;
}


/* this is supposed to be used when @coord is set between items. Return value
   is position of item which is right of those neighboring items */
unsigned right_item_pos (const tree_coord * coord)
{
	assert ("vs-204", coord_correct (coord));
	assert ("vs-198", !node_is_empty (coord->node));
	assert ("vs-153", coord_between_items (coord) == 1);

	if (coord->item_pos > node_num_items (coord->node) - 1)
		return coord->item_pos;
	if (coord->unit_pos == 0 && coord->between == BEFORE_UNIT)
		return coord->item_pos;

	assert ("vs-160",
		(coord->unit_pos == last_unit_pos (coord) &&
		 coord->between == AFTER_UNIT) ||
		(coord->unit_pos == coord_num_units (coord) &&
		 coord->between == BEFORE_UNIT));
	
	return coord->item_pos + 1;
}


/* @coord can be set between items and between units. This sets @coord to
   existing unit nearest to the right of @coord */

int coord_set_to_right (tree_coord * coord)
{
	assert ("vs-207", coord_correct (coord));

	if (node_is_empty (coord->node))
		return 1;
	else if (coord_between_items (coord)) {
		if (right_item_pos (coord) + 1 > node_num_items (coord->node))
			/* there is no units on the right of @coord */
			return 1;
		coord->item_pos = right_item_pos (coord);
		coord->unit_pos = 0;
	} else if (coord->between == AFTER_UNIT) {
		assert ("vs-157", 
			coord->unit_pos < last_unit_pos (coord));
		coord->unit_pos ++;
	}
	coord->between = AT_UNIT;
	return 0;
}


/* this only works for now if both coords are set to existing units within the
   same node */
coord_cmp compare_coords (tree_coord * c1, tree_coord * c2)
{
	assert ("vs-209", c1->node == c2->node);
	assert ("vs-194", coord_of_unit (c1) && coord_of_unit (c2));

	if (c1->item_pos > c2->item_pos)
		return COORD_CMP_ON_RIGHT;
	if (c1->item_pos < c2->item_pos)
		return COORD_CMP_ON_LEFT;
	if (c1->unit_pos > c2->unit_pos)
		return COORD_CMP_ON_RIGHT;
	if (c1->unit_pos < c2->unit_pos)
		return COORD_CMP_ON_LEFT;
	return COORD_CMP_SAME;
}

int coord_are_neighbors( tree_coord *c1, tree_coord *c2 )
{
	tree_coord *left;
	tree_coord *right;

	assert( "nikita-1241", c1 != NULL );
	assert( "nikita-1242", c2 != NULL );
	assert( "nikita-1243", c1 -> node == c2 -> node );
	assert( "nikita-1244", coord_of_unit( c1 ) );
	assert( "nikita-1245", coord_of_unit( c2 ) );

	switch( compare_coords( c1, c2 ) ) {
	case COORD_CMP_ON_LEFT:
		left = c1;
		right = c2;
		break;
	case COORD_CMP_ON_RIGHT:
		left = c2;
		right = c1;
		break;
	default:
		wrong_return_value( "nikita-1246", "compare_coords()" );
	case COORD_CMP_SAME:
		return 0;
	}
	if( left -> item_pos == right -> item_pos )
		return left -> unit_pos + 1 == right -> unit_pos;
	else if( left -> item_pos + 1 == right -> item_pos ) {
		return ( left -> unit_pos == last_unit_pos( left ) ) &&
			( right -> unit_pos == 0 );
	} else 
		return 0;
}

int coord_is_leftmost( const tree_coord *coord )
{
	assert( "nikita-1090", coord != NULL );
	return ( coord -> item_pos == 0 ) && ( coord -> unit_pos == 0 );
}

int coord_is_rightmost( const tree_coord *coord )
{
	assert( "nikita-1091", coord != NULL );
	return 
		( coord -> item_pos + 1u == node_num_items( coord -> node ) ) && 
		( coord -> unit_pos == last_unit_pos( coord ) );
}

int coord_is_utmost( const tree_coord *coord, sideof side )
{
	assert( "nikita-1092", coord != NULL );
	assert( "nikita-1093", ( side == LEFT_SIDE ) || ( side == RIGHT_SIDE ) );
	switch( side ) {
	default:
		wrong_return_value( "nikita-1094", "side" );
	case LEFT_SIDE:
		return coord_is_leftmost( coord );
	case RIGHT_SIDE:
		return coord_is_rightmost( coord );
	}
}

int coord_next_utmost_item( tree_coord *coord, sideof side )
{
	assert( "jmacd-1092", coord != NULL );
	assert( "jmacd-1093", ( side == LEFT_SIDE ) || ( side == RIGHT_SIDE ) );

	if (side == LEFT_SIDE) {
		return coord_prev_item (coord);
	} else {
		return coord_next_item (coord);
	}
}

int coord_next_utmost_unit( tree_coord *coord, sideof side )
{
	assert( "jmacd-1094", coord != NULL );
	assert( "jmacd-1095", ( side == LEFT_SIDE ) || ( side == RIGHT_SIDE ) );

	if (side == LEFT_SIDE) {
		return coord_prev_unit (coord);
	} else {
		return coord_next_unit (coord);
	}
}

int coord_after_utmost( const tree_coord *coord, sideof side )
{
	assert( "jmacd-1096", coord != NULL );
	assert( "jmacd-1097", ( side == LEFT_SIDE ) || ( side == RIGHT_SIDE ) );

	if (side == LEFT_SIDE) {
		return coord_before_first (coord);
	} else {
		return coord_after_last (coord);
	}
}

void coord_utmost_unit( tree_coord *coord, znode *node, sideof side )
{
	assert( "jmacd-1098", coord != NULL );
	assert( "jmacd-1099", ( side == LEFT_SIDE ) || ( side == RIGHT_SIDE ) );

	if (side == LEFT_SIDE) {
		coord_first_unit (coord, node);
	} else {
		coord_last_unit (coord, node);
	}
}

/* return true if @coord is set after last unit in the item, 0 otherwise */
int coord_is_after_item (const tree_coord * coord)
{
	assert ("vs-261", coord_of_item (coord));
	if (coord->unit_pos == last_unit_pos (coord) + 1)
		return 1;
	return (coord->unit_pos == last_unit_pos (coord) &&
		coord->between == AFTER_UNIT);
}


int coord_is_before_item (const tree_coord * coord, unsigned item_pos UNUSED_ARG)
{
	/*
	 * it is not ready yet for other item_pos than 0
	 */
	assert ("vs-446", item_pos == 0);

	if (coord->item_pos == 0)
		return 1;
	if (coord->item_pos == 0 && coord->unit_pos == 0 &&
	    coord->between == BEFORE_UNIT)
		return 1;
	return 0;
}


/**
 * determine how @coord is located w.r.t. its node.
 */
coord_wrt_node coord_wrt( const tree_coord *coord )
{
	assert( "nikita-1713", coord != NULL );
	assert( "nikita-1714", coord -> node != NULL );

	if( coord_between_items( coord ) ) {
		if( node_is_empty( coord -> node ) )
			return COORD_ON_THE_RIGHT;
		if( left_item_pos( coord ) == -1 )
			return COORD_ON_THE_LEFT;
		if( right_item_pos( coord ) >= node_num_items( coord -> node ) )
			return COORD_ON_THE_RIGHT;
	}
	return COORD_INSIDE;
}

/** true if @c1 and @c2 points to the same place in a node */
int coord_eq( const tree_coord *c1, const tree_coord *c2 )
{
	assert( "nikita-1807", c1 != NULL );
	assert( "nikita-1808", c2 != NULL );

	if( ! memcmp( c1, c2, sizeof *c1 ) )
		return 1;
	if( c1 -> node != c2 -> node )
		return 0;
	switch( c1 -> between ) {
	default: wrong_return_value( "nikita-1809", "c1 -> between" );
	case AT_UNIT:
		return 0;
	case BEFORE_UNIT:
		if( c1 -> unit_pos > 0 )
			return 
				( c1 -> item_pos == c2 -> item_pos ) &&
				( c1 -> unit_pos == c2 -> unit_pos + 1 ) &&
				( c2 -> between == AFTER_UNIT );
		else {
			/*case BEFORE_ITEM:*/
			assert( "nikita-1810", c1 -> unit_pos == 0 );
			return 
				( c1 -> item_pos == c2 -> item_pos + 1 ) &&
				( c2 -> unit_pos == last_unit_pos( c2 ) ) &&
				( ( c1 -> between == AFTER_UNIT ) /*||
			        ( c1 -> between == AFTER_ITEM ) */);
		}
	case AFTER_UNIT:
		if( c1 -> unit_pos == last_unit_pos( c1 ) ) {
			/*case AFTER_ITEM:*/
			assert( "nikita-1811", 
				c1 -> unit_pos == last_unit_pos( c1 ) );
			return 
				( c1 -> item_pos + 1 == c2 -> item_pos ) &&
				( c2 -> unit_pos == 0 ) &&
				( ( c1 -> between == BEFORE_UNIT ) /*||
			        ( c1 -> between == BEFORE_ITEM ) */);
		} else
			return 
				( c1 -> item_pos == c2 -> item_pos ) &&
				( c1 -> unit_pos + 1 == c2 -> unit_pos ) &&
				( c2 -> between == BEFORE_UNIT );
	}
}

static const char * tween (between_enum n)
{
	switch (n) {
	case BEFORE_UNIT: return "before unit";
		/*case BEFORE_ITEM: return "before item";*/
	case AT_UNIT: return "at unit";
	case AFTER_UNIT: return "after unit";
		/*case AFTER_ITEM: return "after item";*/
	default: return "unknown";
	}
}


void print_coord (const char * mes, const tree_coord * coord,
		  int node)
{
	if( coord == NULL ) {
		info( "%s: null\n", mes );
		return;
	}
	info ("%s: item_pos = %d, unit_pos %d, tween=%s\n",
	      mes, coord->item_pos, coord->unit_pos, 
	      tween (coord->between));
	if (node)
		print_znode( "\tnode", coord -> node );
}


/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
