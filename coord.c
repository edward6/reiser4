/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#define DONTUSE_COORD_FIELDS
#include "reiser4.h"

#if 1

/* Internal constructor. */
static inline void coord_init_values (tree_coord  *coord,
				      znode       *node,
				      pos_in_node  item_pos,
				      pos_in_item  unit_pos,
				      between_enum between)
{
	coord->node     = node;
	coord->item_pos = item_pos;
	coord->unit_pos = unit_pos;
	coord->between  = between;
}

/* Copy a coordinate. */
void coord_dup (tree_coord *new_coord, const tree_coord *old_coord)
{
	assert ("jmacd-9800", coord_check (old_coord));
	new_coord->node     = old_coord->node;
	new_coord->item_pos = old_coord->item_pos;
	new_coord->unit_pos = old_coord->unit_pos;
	new_coord->between  = old_coord->between;
}

/* Initialize an invalid coordinate. */
void coord_init_invalid (tree_coord *coord, znode *node)
{
	coord_init_values (coord, node, 0, 0, INVALID_COORD);
}

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
void coord_init_first_unit (tree_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	coord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : AT_UNIT));

	assert ("jmacd-9801", coord_check (coord));
}

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
void coord_init_last_unit  (tree_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	coord_init_values (coord, node, (is_empty ? 0 : node_num_items (node) - 1), 0, (is_empty ? EMPTY_NODE : AT_UNIT));

	assert ("jmacd-9802", coord_check (coord));
}

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
void coord_init_before_first_item (tree_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	coord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : BEFORE_UNIT));

	assert ("jmacd-9803", coord_check (coord));
}

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
void coord_init_after_last_item (tree_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	coord_init_values (coord, node, (is_empty ? 0 : node_num_items (node) - 1), 0, (is_empty ? EMPTY_NODE : AFTER_UNIT));

	assert ("jmacd-9804", coord_check (coord));
}

/* Return the number of items at the present node.  Asserts coord->node != NULL. */
unsigned coord_num_items (const tree_coord * coord)
{
	assert ("jmacd-9805", coord->node != NULL);

	return node_num_items (coord->node);
}

/* Return the number of units at the present item.  Asserts coord_is_existing_item(). */
unsigned coord_num_units (const tree_coord * coord)
{
	assert ("jmacd-9806", coord_is_existing_item (coord));

	return item_plugin_by_coord (coord)->common.nr_units (coord);
}

/* Return the last valid unit number at the present item (i.e., coord_num_units() - 1). */
unsigned coord_last_unit_pos (const tree_coord * coord)
{
	return coord_num_units (coord) - 1;
}

/* Returns true if the coord was initializewd by coord_init_invalid (). */
int coord_is_invalid (const tree_coord *coord)
{
	return coord->between == INVALID_COORD;
}

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not. */
int coord_is_existing_item (const tree_coord * coord)
{
	switch (coord->between) {
	case EMPTY_NODE:
	case BEFORE_ITEM:
	case AFTER_ITEM:
	case INVALID_COORD:
		return 0;

	case BEFORE_UNIT:
	case AT_UNIT:
	case AFTER_UNIT:
		return coord->item_pos < coord_num_items (coord);
	}

	impossible ("jmacd-9900", "unreachable");
}

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
int coord_is_existing_unit (const tree_coord *coord)
{
	switch (coord->between) {
	case EMPTY_NODE:
	case BEFORE_UNIT:
	case AFTER_UNIT:
	case BEFORE_ITEM:
	case AFTER_ITEM:
	case INVALID_COORD:
		return 0;

	case AT_UNIT:
		return (coord->item_pos < coord_num_items (coord) &&
			coord->unit_pos < coord_num_units (coord));
	}

	impossible ("jmacd-9902", "unreachable");
}

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
int coord_is_leftmost_unit (const tree_coord *coord)
{
	assert ("jmacd-9808", coord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == 0 &&
		coord->unit_pos == 0);
}

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
int coord_is_rightmost_unit (const tree_coord *coord)
{
	assert ("jmacd-9809", coord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == coord_num_units (coord) - 1 &&
		coord->unit_pos == coord_last_unit_pos (coord));
}

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
int coord_is_rightmost_item (const tree_coord *coord)
{
	assert ("jmacd-9820", coord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == coord_num_units (coord) - 1);
}

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
int coord_check (const tree_coord *coord)
{
	if (coord->node == NULL) { return 0; }

	switch (coord->between) {
	default:
	case INVALID_COORD:
		return 0;
	case EMPTY_NODE:
		if (! node_is_empty (coord->node)) {
			return 0;
		}
		return coord->item_pos == 0 && coord->unit_pos == 0;

	case BEFORE_UNIT:
	case AFTER_UNIT:
	case AT_UNIT:
		break;
	case AFTER_ITEM:
	case BEFORE_ITEM:
		/* before/after item should not set unit_pos. */
		if (coord->unit_pos != 0) {
			return 0;
		}
		break;
	}

	if (coord->item_pos >= node_num_items (coord->node)) {
		return 0;
	}

	if (coord->unit_pos > coord_last_unit_pos (coord)) {
		return 0;
	}

	return 1;
}
#endif

/* Adjust coordinate boundaries based on the number of items prior to coord_next/prev.
 * Returns 1 if the new position is does not exist. */
static int coord_adjust_items (tree_coord *coord, unsigned items, int is_next)
{
	/* If the node is invalid, leave it. */
	if (coord->between == INVALID_COORD) {
		return 1;
	}

	/* If the node is empty, set it appropriately. */
	if (items == 0) {
		coord->between  = EMPTY_NODE;
		coord->item_pos = 0;
		coord->unit_pos = 0;
		return 1;
	}

	/* If it was empty and it no longer is, set to BEFORE/AFTER_ITEM. */
	if (coord->between == EMPTY_NODE) {
		coord->between  = (is_next ? BEFORE_ITEM : AFTER_ITEM);
		coord->item_pos = 0;
		coord->unit_pos = 0;
		return 0;
	}

	/* If the item_pos is out-of-range, set it appropriatly. */
	if (coord->item_pos >= items) {
		coord->between  = AFTER_ITEM;
		coord->item_pos = items - 1;
		coord->unit_pos = 0;
		/* If is_next, return 1 (can't go any further). */
		return is_next;
	}

	return 0;
}

/* Advances the coordinate by one unit to the right.  If empty, no change.  If
 * coord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is an
 * existing unit. */
int coord_next_unit (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 1) == 1) { return 1; }

	switch (coord->between) {
	case BEFORE_UNIT:
		/* Now it is positioned at the same unit. */
		coord->between = AT_UNIT;
		return 0;

	case AFTER_UNIT:
	case AT_UNIT:
		/* If it was at or after a unit and there are more units in this item,
		 * advance to the next one. */
		if (coord->unit_pos < coord_last_unit_pos (coord)) {
			coord->unit_pos += 1;
			coord->between   = AT_UNIT;
			return 0;
		}

		/* Otherwise, it is crossing an item boundary and treated as if it was
		 * after the current item. */
		coord->between  = AFTER_ITEM;
		coord->unit_pos = 0;
		/* FALLTHROUGH */

	case AFTER_ITEM:
		/* Check for end-of-node. */
		if (coord->item_pos == items - 1) {
			return 1;
		}

		coord->item_pos += 1;
		coord->unit_pos  = 0;
		coord->between   = AT_UNIT;
		return 0;

	case BEFORE_ITEM:
		/* The adjust_items checks ensure that we are valid here. */
		coord->unit_pos = 0;
		coord->between  = AT_UNIT;
		return 0;

	case INVALID_COORD:
	case EMPTY_NODE:
		/* Handled in coord_adjust_items(). */
		break;
	}

	impossible ("jmacd-9902", "unreachable");
}

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * coord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
int coord_next_item (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 1) == 1) { return 1; }

	switch (coord->between) {
	case AFTER_UNIT:
	case AT_UNIT:
	case BEFORE_UNIT:
	case AFTER_ITEM:
		/* Check for end-of-node. */
		if (coord->item_pos == items - 1) {
			coord->between   = AFTER_ITEM;
			coord->unit_pos  = 0;
			return 1;
		}

		/* Anywhere in an item, go to the next one. */
		coord->between   = AT_UNIT;
		coord->item_pos += 1;
		coord->unit_pos  = 0;
		return 0;

	case BEFORE_ITEM:
		/* The out-of-range check ensures that we are valid here. */
		coord->unit_pos = 0;
		coord->between  = AT_UNIT;
		return 0;
	case INVALID_COORD:
	case EMPTY_NODE:
		/* Handled in coord_adjust_items(). */
		break;
	}

	impossible ("jmacd-9903", "unreachable");
}

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * coord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
int coord_prev_unit (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 0) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
	case BEFORE_UNIT:
		if (coord->unit_pos > 0) {
			coord->unit_pos -= 1;
			coord->between   = AT_UNIT;
			return 0;
		}

		if (coord->item_pos == 0) {
			coord->between = BEFORE_ITEM;
			return 1;
		}

		coord->item_pos -= 1;
		coord->unit_pos  = coord_last_unit_pos (coord);
		coord->between   = AT_UNIT;
		return 0;

	case AFTER_UNIT:
		/* What if unit_pos is out-of-range? */
		assert ("jmacd-5442", coord->unit_pos <= coord_last_unit_pos (coord));
		coord->between = AT_UNIT;
		return 0;

	case BEFORE_ITEM:
		if (coord->item_pos == 0) {
			return 1;
		}

		coord->item_pos -= 1;
		/* FALLTHROUGH */

	case AFTER_ITEM:
		coord->between  = AT_UNIT;
		coord->unit_pos = coord_last_unit_pos (coord);
		return 0;

	case INVALID_COORD:
	case EMPTY_NODE:
		break;
	}

	impossible ("jmacd-9904", "unreachable");
}

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * coord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
int coord_prev_item (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 0) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
	case AFTER_UNIT:
	case BEFORE_UNIT:
	case BEFORE_ITEM:

		if (coord->item_pos == 0) {
			coord->between  = BEFORE_ITEM;
			coord->unit_pos = 0;
			return 1;
		}

		coord->item_pos -= 1;
		coord->unit_pos  = 0;
		coord->between   = AT_UNIT;
		return 0;

	case AFTER_ITEM:
		coord->between  = AT_UNIT;
		coord->unit_pos = 0;
		return 0;

	case INVALID_COORD:
	case EMPTY_NODE:
		break;
	}

	impossible ("jmacd-9905", "unreachable");
}

/* Calls either coord_init_first_unit or coord_init_last_unit depending on sideof argument. */
void coord_init_sideof_unit (tree_coord *coord, znode *node, sideof dir)
{
	assert ("jmacd-9821", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		coord_init_first_unit (coord, node);
	} else {
		coord_init_last_unit (coord, node);
	}
}

/* Calls either coord_is_before_leftmost or coord_is_after_rightmost depending on sideof
 * argument. */
int coord_is_after_sideof_unit (tree_coord *coord, sideof dir)
{
	assert ("jmacd-9822", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		return coord_is_before_leftmost (coord);
	} else {
		return coord_is_after_rightmost (coord);
	}
}

/* Calls either coord_next_unit or coord_prev_unit depending on sideof argument. */
int coord_sideof_unit (tree_coord *coord, sideof dir)
{
	assert ("jmacd-9823", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		return coord_prev_unit (coord);
	} else {
		return coord_next_unit (coord);
	}
}

/* Returns true if two coordinates are consider equal.  Coordinates that are between units
 * or items are considered equal. */
int coord_eq (const tree_coord *c1, const tree_coord *c2)
{
	assert ("nikita-1807", c1 != NULL);
	assert ("nikita-1808", c2 != NULL);

	if (memcmp (c1, c2, sizeof (*c1)) == 0) {
		return 1;
	}
	if (c1->node != c2->node) {
		return 0;
	}

	switch (c1->between) {
	case INVALID_COORD:
	case EMPTY_NODE:
	case AT_UNIT:
		return 0;

	case BEFORE_UNIT:
		/* c2 must be after the previous unit. */
		return (c1->item_pos == c2->item_pos &&
			c2->between == AFTER_UNIT &&
			c2->unit_pos == c1->unit_pos - 1);

	case AFTER_UNIT:
		/* c2 must be before the next unit. */
		return (c1->item_pos == c2->item_pos &&
			c2->between == BEFORE_UNIT &&
			c2->unit_pos == c1->unit_pos + 1);

	case BEFORE_ITEM:
		/* c2 must be after the previous item. */
		return (c1->item_pos == c2->item_pos - 1 &&
			c2->between == AFTER_ITEM);

	case AFTER_ITEM:
		/* c2 must be before the next item. */
		return (c1->item_pos == c2->item_pos + 1 &&
			c2->between == BEFORE_ITEM);
	}

	impossible ("jmacd-9906", "unreachable");
}

/* If coord_is_after_rightmost return COORD_ON_THE_RIGHT, if coord_is_after_leftmost
 * return COORD_ON_THE_LEFT, otherwise return COORD_INSIDE. */
coord_wrt_node coord_wrt (const tree_coord *coord)
{
	if (coord_is_before_leftmost (coord)) {
		return COORD_ON_THE_LEFT;
	}

	if (coord_is_after_rightmost (coord)) {
		return COORD_ON_THE_RIGHT;
	}

	return COORD_INSIDE;
}

/* Returns true if the coordinate is positioned after the last item or after the last unit
 * of the last item or it is an empty node. */
int coord_is_after_rightmost (const tree_coord *coord)
{
	assert ("jmacd-7313", coord_check (coord));

	switch (coord->between) {
	case INVALID_COORD:
	case AT_UNIT:
	case BEFORE_UNIT:
	case BEFORE_ITEM:
		return 0;

	case EMPTY_NODE:
		return 1;

	case AFTER_ITEM:
		return (coord->item_pos == node_num_items (coord->node) - 1);

	case AFTER_UNIT:
		return ((coord->item_pos == node_num_items (coord->node) - 1) && 
			coord->unit_pos == coord_last_unit_pos (coord));
	}

	impossible ("jmacd-9908", "unreachable");
}

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
int coord_is_before_leftmost (const tree_coord *coord)
{
	assert ("jmacd-7313", coord_check (coord));

	switch (coord->between) {
	case INVALID_COORD:
	case AT_UNIT:
	case AFTER_ITEM:
	case AFTER_UNIT:
		return 0;

	case EMPTY_NODE:
		return 1;

	case BEFORE_ITEM:
	case BEFORE_UNIT:
		return (coord->item_pos == 0) && (coord->unit_pos == 0);
	}

	impossible ("jmacd-9908", "unreachable");
}

/* Returns true if the coordinate is positioned after a item, before a item, after the
 * last unit of an item, before the first unit of an item, or at an empty node. */
int coord_is_between_items (const tree_coord *coord)
{
	assert ("jmacd-7313", coord_check (coord));

	switch (coord->between) {
	case INVALID_COORD:
	case AT_UNIT:
		return 0;

	case AFTER_ITEM:
	case BEFORE_ITEM:
	case EMPTY_NODE:
		return 1;

	case BEFORE_UNIT:
		return coord->unit_pos == 0;

	case AFTER_UNIT:
		return coord->unit_pos == coord_last_unit_pos (coord);
	}

	impossible ("jmacd-9908", "unreachable");
}

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
int coord_are_neighbors (tree_coord *c1, tree_coord *c2)
{
	tree_coord *left;
	tree_coord *right;

	assert( "nikita-1241", c1 != NULL );
	assert( "nikita-1242", c2 != NULL );
	assert( "nikita-1243", c1 -> node == c2 -> node );
	assert( "nikita-1244", coord_is_existing_unit( c1 ) );
	assert( "nikita-1245", coord_is_existing_unit( c2 ) );

	switch( coord_compare( c1, c2 ) ) {
	case COORD_CMP_ON_LEFT:
		left  = c1;
		right = c2;
		break;
	case COORD_CMP_ON_RIGHT:
		left  = c2;
		right = c1;
		break;
	case COORD_CMP_SAME:
		return 0;
	default:
		wrong_return_value( "nikita-1246", "compare_coords()" );
	}
	if( left -> item_pos == right -> item_pos ) {
		return left -> unit_pos + 1 == right -> unit_pos;
	} else if( left -> item_pos + 1 == right -> item_pos ) {
		return ( left -> unit_pos == coord_last_unit_pos( left ) ) &&
			( right -> unit_pos == 0 );
	} else {
		return 0;
	}
}

/* Assuming two coordinates are positioned in the same node, return COORD_CMP_ON_RIGHT,
 * COORD_CMP_ON_LEFT, or COORD_CMP_SAME depending on c1's position relative to c2.  */
coord_cmp coord_compare (tree_coord * c1, tree_coord * c2)
{
	assert ("vs-209", c1->node == c2->node);
	assert ("vs-194", coord_is_existing_unit (c1) && coord_is_existing_unit (c2));

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

/* Returns true if coord is set to or before the first (if LEFT_SIDE) unit of the item and
 * to or after the last (if RIGHT_SIDE) unit of the item. */
int coord_is_delimiting (tree_coord *coord, sideof dir)
{
	assert ("jmacd-9824", dir == LEFT_SIDE || dir == RIGHT_SIDE);

	if (dir == LEFT_SIDE) {
		return coord_is_before_leftmost (coord);
	} else {
		return coord_is_after_rightmost (coord);
	}
}

/* If the coordinate is before an item/unit, set to next item/unit.  If the coordinate is
 * after an item/unit, set to the previous item/unit.  Returns 0 on success and non-zero
 * if there is no position (i.e., if the coordinate is empty). */
int coord_set_to_unit (tree_coord *coord)
{
	assert ("jmacd-7316", coord_check (coord));

	switch (coord->between) {
	case INVALID_COORD:
	case EMPTY_NODE:
		return 1;

	case AT_UNIT:
		return 0;

	case AFTER_ITEM:
	case BEFORE_ITEM:
	case BEFORE_UNIT:
	case AFTER_UNIT:
		coord->between = AT_UNIT;
		return 0;
	}

	impossible ("jmacd-9909", "unreachable");
}

/* If the coordinate is between items, shifts it to the right.  Returns 0 on success and
 * non-zero if there is no position to the right. */
int coord_set_to_right (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 1) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
		return 0;

	case BEFORE_ITEM:
	case BEFORE_UNIT:
		coord->between = AT_UNIT;
		return 0;

	case AFTER_UNIT:
		if (coord->unit_pos < coord_last_unit_pos (coord)) {
			coord->unit_pos += 1;
			coord->between   = AT_UNIT;
			return 0;
		} else {

			coord->unit_pos = 0;

			if (coord->item_pos == items - 1) {
				coord->between = AFTER_ITEM;
				return 1;
			}

			coord->item_pos += 1;
			coord->between   = AT_UNIT;
			return 0;
		}

	case AFTER_ITEM:
		if (coord->item_pos == items - 1) {
			return 1;
		}

		coord->item_pos += 1;
		coord->unit_pos  = 0;
		coord->between   = AT_UNIT;
		return 0;
		
	case INVALID_COORD:
	case EMPTY_NODE:
	break;
	}

	impossible ("jmacd-9920", "unreachable");
}

/* If the coordinate is between items, shifts it to the left.  Returns 0 on success and
 * non-zero if there is no position to the left. */
int coord_set_to_left (tree_coord *coord)
{
	unsigned items = coord_num_items (coord);

	if (coord_adjust_items (coord, items, 0) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
		return 0;

	case AFTER_UNIT:
		coord->between = AT_UNIT;
		return 0;

	case AFTER_ITEM:
		coord->unit_pos = coord_last_unit_pos (coord);
		coord->between  = AT_UNIT;
		return 0;

	case BEFORE_UNIT:
		if (coord->unit_pos > 0) {
			coord->unit_pos -= 1;
			coord->between   = AT_UNIT;
			return 0;
		} else {

			if (coord->item_pos == 0) {
				coord->between = BEFORE_ITEM;
				return 1;
			}

			coord->unit_pos  = coord_last_unit_pos (coord);
			coord->item_pos -= 1;
			coord->between   = AT_UNIT;
			return 0;
		}

	case BEFORE_ITEM:
		if (coord->item_pos == 0) {
			return 1;
		}

		coord->item_pos -= 1;
		coord->unit_pos  = coord_last_unit_pos (coord);
		coord->between   = AT_UNIT;
		return 0;
		
	case INVALID_COORD:
	case EMPTY_NODE:
	break;
	}

	impossible ("jmacd-9920", "unreachable");
}

#else

unsigned coord_num_units (const tree_coord * coord)
{
	assert ("vs-276", coord_of_item (coord));
	return item_plugin_by_coord (coord)->common.nr_units (coord);
}

unsigned coord_last_unit_pos (const tree_coord * coord)
{
	return coord_num_units (coord) - 1;
}


/* 1 is returned if @coord's fields are of reasonable values, 0 - otherwise */
int coord_correct (const tree_coord * coord)
{
	if (coord->node == NULL)
		return 0;
	if (coord->between != AFTER_UNIT && coord->between != AT_UNIT &&
	    coord->between != BEFORE_UNIT && coord->between != BEFORE_ITEM &&
	    coord->between != AFTER_ITEM)
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


/* 1 is returned if @coord is set to any existing unit within a node, 0 -
   otherwise */
int coord_of_unit (const tree_coord * coord)
{
	if (coord->between != AT_UNIT)
		return 0;
	if (node_is_empty (coord->node))
		return 0;
	if (coord->item_pos >= node_num_items (coord->node))
		return 0;
	if (coord->unit_pos > coord_last_unit_pos (coord))
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
		units = coord_last_unit_pos( coord ) + 1;
	else
		units = ~0u;
	if( ( pos == 0 ) && ( tweenness == BEFORE_ITEM ) )
		return 0;
	if( ( pos == 0 ) && ( unit == 0 ) && ( tweenness == BEFORE_UNIT ) )
		return 0;
	if( ( pos == items - 1 ) && ( tweenness == AFTER_ITEM ) )
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
	if (coord->unit_pos > coord_last_unit_pos (coord)) {
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
		coord->unit_pos = coord_last_unit_pos (coord);		
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
	coord->unit_pos = coord_last_unit_pos (coord);
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
	coord->unit_pos = coord_last_unit_pos (coord);
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
		if (coord->between == AFTER_ITEM || coord->between == BEFORE_ITEM)
			return 1;
		/* in node position is set to item within a node */
		if (coord->unit_pos <= coord_last_unit_pos (coord)) {
			/* in item position is set to unit within an item */
			if (coord->unit_pos == 0) {
				if (coord->between == BEFORE_UNIT)
					return 1;
			}
			if (coord->unit_pos == coord_last_unit_pos (coord)) {
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


/* this is supposed to be used when @coord is set between items. Return value
   is position of item which is left of those neighboring items */
static int coord_left_item_pos (const tree_coord * coord)
{
	assert ("vs-208", coord_correct (coord));
	assert ("vs-197", !node_is_empty (coord->node));
	assert ("vs-152", coord_between_items (coord) == 1);

	if ((coord->item_pos == 0) &&
	    (coord->between != AFTER_UNIT) && (coord->between != AFTER_ITEM))
		return -1;
	if (coord->item_pos == node_num_items (coord->node))
		return node_num_items (coord->node) - 1;

	if ((coord->unit_pos == coord_last_unit_pos (coord) &&
	     coord->between == AFTER_UNIT) ||
	    coord->between == AFTER_ITEM)
		return coord->item_pos;

	assert ("vs-159",
		(coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
		coord->between == BEFORE_ITEM);
	return coord->item_pos - 1;
}


/* @coord can be set between items and between units. This sets @coord to
   existing unit nearest to the left of @coord */
int coord_set_to_left (tree_coord * coord)
{
	assert ("vs-203", coord_correct (coord));

	if (coord_between_items (coord)) {
		if (coord_left_item_pos (coord) < 0)
			/* there is no units on the left of @coord */
			return 1;
		coord->item_pos = coord_left_item_pos (coord);
		coord->unit_pos = coord_last_unit_pos (coord);
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
static unsigned coord_right_item_pos (const tree_coord * coord)
{
	assert ("vs-204", coord_correct (coord));
	assert ("vs-198", !node_is_empty (coord->node));
	assert ("vs-153", coord_between_items (coord) == 1);

	if (coord->item_pos > node_num_items (coord->node) - 1)
		return coord->item_pos;
	if ((coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	    coord->between == BEFORE_ITEM)
		return coord->item_pos;

	assert ("vs-160",
		(coord->unit_pos == coord_last_unit_pos (coord) &&
		 coord->between == AFTER_UNIT) ||
		(coord->unit_pos == coord_num_units (coord) &&
		 coord->between == BEFORE_UNIT) ||
		coord->between == AFTER_ITEM);
	
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
		if (coord_right_item_pos (coord) + 1 > node_num_items (coord->node))
			/* there is no units on the right of @coord */
			return 1;
		coord->item_pos = coord_right_item_pos (coord);
		coord->unit_pos = 0;
	} else if (coord->between == AFTER_UNIT) {
		assert ("vs-157", 
			coord->unit_pos < coord_last_unit_pos (coord));
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
		return ( left -> unit_pos == coord_last_unit_pos( left ) ) &&
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
		( coord -> unit_pos == coord_last_unit_pos( coord ) );
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


/* return true if @coord is set after last unit in the item, 0 otherwise */
int coord_is_after_item (const tree_coord * coord)
{
	assert ("vs-261", coord_of_item (coord));
	if (coord->unit_pos == coord_last_unit_pos (coord) + 1)
		return 1;
	return (coord->unit_pos == coord_last_unit_pos (coord) &&
		coord->between == AFTER_UNIT);
}


int coord_is_before_item (const tree_coord * coord)
{
	assert ("vs-446", coord_of_item (coord));

	if (coord->item_pos == 0 && coord->between == BEFORE_ITEM)
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
		if( coord_left_item_pos( coord ) == -1 )
			return COORD_ON_THE_LEFT;
		if( coord_right_item_pos( coord ) >= node_num_items( coord -> node ) )
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
	case BEFORE_ITEM:
			assert( "nikita-1810", c1 -> unit_pos == 0 );
			return 
				( c1 -> item_pos == c2 -> item_pos + 1 ) &&
				( c2 -> unit_pos == coord_last_unit_pos( c2 ) ) &&
				( ( c1 -> between == AFTER_UNIT ) ||
				  ( c1 -> between == AFTER_ITEM ) );
		}
	case AFTER_UNIT:
		if( c1 -> unit_pos == coord_last_unit_pos( c1 ) ) {
	case AFTER_ITEM:
			assert( "nikita-1811", 
				c1 -> unit_pos == coord_last_unit_pos( c1 ) );
			return 
				( c1 -> item_pos + 1 == c2 -> item_pos ) &&
				( c2 -> unit_pos == 0 ) &&
				( ( c1 -> between == BEFORE_UNIT ) ||
				  ( c1 -> between == BEFORE_ITEM ) );
		} else
			return 
				( c1 -> item_pos == c2 -> item_pos ) &&
				( c1 -> unit_pos + 1 == c2 -> unit_pos ) &&
				( c2 -> between == BEFORE_UNIT );
	}
}

/** clear coord content */
int init_coord( tree_coord *coord /* coord to init */ )
{
	assert( "nikita-312", coord != NULL );
	trace_stamp( TRACE_TREE );

	xmemset( coord, 0, sizeof *coord );
	return 0;
}

/** correct tree_coord object duplication */
void dup_coord (tree_coord * new, const tree_coord * old)
{
	xmemcpy (new, old, sizeof (tree_coord));
	if (old->node != NULL) {
		/* FIXME-NIKITA nikita: done_coord() does nothing
		   zref(old->node); */
	}
	/* FIXME-NIKITA: d_count ? */
}

/** release all resources associated with "coord": zput coord's znode. */
/** FIXME: JMACD says: we shouldn't reference coord->node, the caller should
 * just ensure that the node is otherwise referenced.  This done_coord isn't
 * being used consistently now because it is not properly enforced yet.  If
 * there is a good reason to ref coord->node (i.e., dcount), then implement it
 * NOW.  My code doesn't call done_coord because I didn't know there was such
 * a method. */
int done_coord( tree_coord *coord UNUSED_ARG /* coord to finish with */ )
{
	assert( "nikita-313", coord != NULL );
	trace_stamp( TRACE_TREE );

	return 0;
}

#endif

static const char * coord_tween (between_enum n)
{
	switch (n) {
	case BEFORE_UNIT: return "before unit";
	case BEFORE_ITEM: return "before item";
	case AT_UNIT: return "at unit";
	case AFTER_UNIT: return "after unit";
	case AFTER_ITEM: return "after item";
	default: return "unknown";
	}
}


void coord_print (const char * mes, const tree_coord * coord, int node)
{
	if( coord == NULL ) {
		info( "%s: null\n", mes );
		return;
	}
	info ("%s: item_pos = %d, unit_pos %d, tween=%s\n",
	      mes, coord->item_pos, coord->unit_pos, 
	      coord_tween (coord->between));
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
