/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/* Hack. */
void ncoord_to_tcoord (tree_coord *tcoord, const new_coord *ncoord)
{
	tcoord->item_pos = ncoord->item_pos;
	tcoord->unit_pos = ncoord->unit_pos;
	tcoord->node     = ncoord->node;

	switch (ncoord->between) {
	case INVALID_COORD:
	case EMPTY_NODE:
		tcoord->between = AT_UNIT;
		break;
	case BEFORE_UNIT:
	case AT_UNIT:
	case AFTER_UNIT:
	case BEFORE_ITEM:
	case AFTER_ITEM:
		tcoord->between = ncoord->between;
		break;
	}
}

/* Internal constructor. */
static inline void ncoord_init_values (new_coord  *coord,
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
void ncoord_dup (new_coord *coord, const new_coord *old_coord)
{
	assert ("jmacd-9800", ncoord_check (old_coord));
	coord->node     = old_coord->node;
	coord->item_pos = old_coord->item_pos;
	coord->unit_pos = old_coord->unit_pos;
	coord->between  = old_coord->between;
}

/* Initialize an invalid coordinate. */
void ncoord_init_invalid (new_coord *coord, znode *node)
{
	ncoord_init_values (coord, node, 0, 0, INVALID_COORD);
}

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
void ncoord_init_first_unit (new_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : AT_UNIT));

	assert ("jmacd-9801", ncoord_check (coord));
}

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
void ncoord_init_last_unit  (new_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, (is_empty ? 0 : node_num_items (node) - 1), 0, (is_empty ? EMPTY_NODE : AT_UNIT));

	assert ("jmacd-9802", ncoord_check (coord));
}

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
void ncoord_init_before_first_item (new_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : BEFORE_UNIT));

	assert ("jmacd-9803", ncoord_check (coord));
}

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
void ncoord_init_after_last_item (new_coord *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, (is_empty ? 0 : node_num_items (node) - 1), 0, (is_empty ? EMPTY_NODE : AFTER_UNIT));

	assert ("jmacd-9804", ncoord_check (coord));
}

/* Return the number of items at the present node.  Asserts coord->node != NULL. */
unsigned ncoord_num_items (const new_coord * coord)
{
	assert ("jmacd-9805", coord->node != NULL);

	return node_num_items (coord->node);
}

/* Return the number of units at the present item.  Asserts ncoord_is_existing_item(). */
unsigned ncoord_num_units (const new_coord * coord)
{
	tree_coord tcoord;

	assert ("jmacd-9806", ncoord_is_existing_item (coord));

	ncoord_to_tcoord (& tcoord, coord);

	return item_plugin_by_coord (& tcoord)->common.nr_units (& tcoord);
}

/* Return the last valid unit number at the present item (i.e., ncoord_num_units() - 1). */
unsigned ncoord_last_unit_pos (const new_coord * coord)
{
	return ncoord_num_units (coord) - 1;
}

/* Returns the current item positions.  Asserts non-empty. */
unsigned ncoord_item_pos (const new_coord *coord)
{
	assert ("jmacd-5512", coord->between != EMPTY_NODE && coord->between != INVALID_COORD);
	assert ("jmacd-5513", node_num_items (coord->node) != 0);
	return coord->item_pos;
}

/* Returns true if the coord was initializewd by ncoord_init_invalid (). */
int ncoord_is_invalid (const new_coord *coord)
{
	return coord->between == INVALID_COORD;
}

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not. */
int ncoord_is_existing_item (const new_coord * coord)
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
		return coord->item_pos < ncoord_num_items (coord);
	}

	impossible ("jmacd-9900", "unreachable");
}

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
int ncoord_is_existing_unit (const new_coord *coord)
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
		return (coord->item_pos < ncoord_num_items (coord) &&
			coord->unit_pos < ncoord_num_units (coord));
	}

	impossible ("jmacd-9902", "unreachable");
}

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
int ncoord_is_leftmost_unit (const new_coord *coord)
{
	return (coord->between == AT_UNIT &&
		coord->item_pos == 0 &&
		coord->unit_pos == 0);
}

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
int ncoord_is_rightmost_unit (const new_coord *coord)
{
	assert ("jmacd-9809", ncoord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == ncoord_num_units (coord) - 1 &&
		coord->unit_pos == ncoord_last_unit_pos (coord));
}

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
int ncoord_is_rightmost_item (const new_coord *coord)
{
	assert ("jmacd-9820", ncoord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == ncoord_num_units (coord) - 1);
}

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
int ncoord_check (const new_coord *coord)
{
	tree_coord tcoord;

	if (coord->node == NULL) {
		return 0; 
	}
	if (znode_above_root (coord->node))
		return 1;

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
		if (node_is_empty (coord->node) && 
		    (coord->item_pos == 0) &&
		    (coord->unit_pos == 0))
			return 1;
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

	ncoord_to_tcoord (& tcoord, coord);

	if (coord->unit_pos > 
	    item_plugin_by_coord (& tcoord)->common.nr_units (& tcoord) - 1) {
		return 0;
	}

	return 1;
}
#endif

/* Adjust coordinate boundaries based on the number of items prior to ncoord_next/prev.
 * Returns 1 if the new position is does not exist. */
static int ncoord_adjust_items (new_coord *coord, unsigned items, int is_next)
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
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is an
 * existing unit. */
int ncoord_next_unit (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 1) == 1) { return 1; }

	switch (coord->between) {
	case BEFORE_UNIT:
		/* Now it is positioned at the same unit. */
		coord->between = AT_UNIT;
		return 0;

	case AFTER_UNIT:
	case AT_UNIT:
		/* If it was at or after a unit and there are more units in this item,
		 * advance to the next one. */
		if (coord->unit_pos < ncoord_last_unit_pos (coord)) {
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
		/* Handled in ncoord_adjust_items(). */
		break;
	}

	impossible ("jmacd-9902", "unreachable");
}

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
int ncoord_next_item (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 1) == 1) { return 1; }

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
		/* Handled in ncoord_adjust_items(). */
		break;
	}

	impossible ("jmacd-9903", "unreachable");
}

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
int ncoord_prev_unit (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 0) == 1) { return 1; }

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
		coord->unit_pos  = ncoord_last_unit_pos (coord);
		coord->between   = AT_UNIT;
		return 0;

	case AFTER_UNIT:
		/* What if unit_pos is out-of-range? */
		assert ("jmacd-5442", coord->unit_pos <= ncoord_last_unit_pos (coord));
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
		coord->unit_pos = ncoord_last_unit_pos (coord);
		return 0;

	case INVALID_COORD:
	case EMPTY_NODE:
		break;
	}

	impossible ("jmacd-9904", "unreachable");
}

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
int ncoord_prev_item (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 0) == 1) { return 1; }

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

/* Calls either ncoord_init_first_unit or ncoord_init_last_unit depending on sideof argument. */
void ncoord_init_sideof_unit (new_coord *coord, znode *node, sideof dir)
{
	assert ("jmacd-9821", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		ncoord_init_first_unit (coord, node);
	} else {
		ncoord_init_last_unit (coord, node);
	}
}

/* Calls either ncoord_is_before_leftmost or ncoord_is_after_rightmost depending on sideof
 * argument. */
int ncoord_is_after_sideof_unit (new_coord *coord, sideof dir)
{
	assert ("jmacd-9822", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		return ncoord_is_before_leftmost (coord);
	} else {
		return ncoord_is_after_rightmost (coord);
	}
}

/* Calls either ncoord_next_unit or ncoord_prev_unit depending on sideof argument. */
int ncoord_sideof_unit (new_coord *coord, sideof dir)
{
	assert ("jmacd-9823", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		return ncoord_prev_unit (coord);
	} else {
		return ncoord_next_unit (coord);
	}
}

/* Returns true if two coordinates are consider equal.  Coordinates that are between units
 * or items are considered equal. */
int ncoord_eq (const new_coord *c1, const new_coord *c2)
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

/* If ncoord_is_after_rightmost return NCOORD_ON_THE_RIGHT, if ncoord_is_after_leftmost
 * return NCOORD_ON_THE_LEFT, otherwise return NCOORD_INSIDE. */
coord_wrt_node ncoord_wrt (const new_coord *coord)
{
	if (ncoord_is_before_leftmost (coord)) {
		return COORD_ON_THE_LEFT;
	}

	if (ncoord_is_after_rightmost (coord)) {
		return COORD_ON_THE_RIGHT;
	}

	return COORD_INSIDE;
}

/* Returns true if the coordinate is positioned after the last item or after the last unit
 * of the last item or it is an empty node. */
int ncoord_is_after_rightmost (const new_coord *coord)
{
	assert ("jmacd-7313", ncoord_check (coord));

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
			coord->unit_pos == ncoord_last_unit_pos (coord));
	}

	impossible ("jmacd-9908", "unreachable");
}

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
int ncoord_is_before_leftmost (const new_coord *coord)
{
	assert ("jmacd-7313", ncoord_check (coord));

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
int ncoord_is_between_items (const new_coord *coord)
{
	assert ("jmacd-7313", ncoord_check (coord));

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
		return coord->unit_pos == ncoord_last_unit_pos (coord);
	}

	impossible ("jmacd-9908", "unreachable");
}

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
int ncoord_are_neighbors (new_coord *c1, new_coord *c2)
{
	new_coord *left;
	new_coord *right;

	assert( "nikita-1241", c1 != NULL );
	assert( "nikita-1242", c2 != NULL );
	assert( "nikita-1243", c1 -> node == c2 -> node );
	assert( "nikita-1244", ncoord_is_existing_unit( c1 ) );
	assert( "nikita-1245", ncoord_is_existing_unit( c2 ) );

	switch( ncoord_compare( c1, c2 ) ) {
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
		return ( left -> unit_pos == ncoord_last_unit_pos( left ) ) &&
			( right -> unit_pos == 0 );
	} else {
		return 0;
	}
}

/* Assuming two coordinates are positioned in the same node, return COORD_CMP_ON_RIGHT,
 * COORD_CMP_ON_LEFT, or COORD_CMP_SAME depending on c1's position relative to c2.  */
coord_cmp ncoord_compare (new_coord * c1, new_coord * c2)
{
	assert ("vs-209", c1->node == c2->node);
	assert ("vs-194", ncoord_is_existing_unit (c1) && ncoord_is_existing_unit (c2));

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
int ncoord_is_delimiting (new_coord *coord, sideof dir)
{
	assert ("jmacd-9824", dir == LEFT_SIDE || dir == RIGHT_SIDE);

	if (dir == LEFT_SIDE) {
		return ncoord_is_before_leftmost (coord);
	} else {
		return ncoord_is_after_rightmost (coord);
	}
}

/* If the coordinate is before an item/unit, set to next item/unit.  If the coordinate is
 * after an item/unit, set to the previous item/unit.  Returns 0 on success and non-zero
 * if there is no position (i.e., if the coordinate is empty). */
int ncoord_set_to_unit (new_coord *coord)
{
	assert ("jmacd-7316", ncoord_check (coord));

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
int ncoord_set_to_right (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 1) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
		return 0;

	case BEFORE_ITEM:
	case BEFORE_UNIT:
		coord->between = AT_UNIT;
		return 0;

	case AFTER_UNIT:
		if (coord->unit_pos < ncoord_last_unit_pos (coord)) {
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
int ncoord_set_to_left (new_coord *coord)
{
	unsigned items = ncoord_num_items (coord);

	if (ncoord_adjust_items (coord, items, 0) == 1) { return 1; }

	switch (coord->between) {
	case AT_UNIT:
		return 0;

	case AFTER_UNIT:
		coord->between = AT_UNIT;
		return 0;

	case AFTER_ITEM:
		coord->unit_pos = ncoord_last_unit_pos (coord);
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

			coord->unit_pos  = ncoord_last_unit_pos (coord);
			coord->item_pos -= 1;
			coord->between   = AT_UNIT;
			return 0;
		}

	case BEFORE_ITEM:
		if (coord->item_pos == 0) {
			return 1;
		}

		coord->item_pos -= 1;
		coord->unit_pos  = ncoord_last_unit_pos (coord);
		coord->between   = AT_UNIT;
		return 0;
		
	case INVALID_COORD:
	case EMPTY_NODE:
	break;
	}

	impossible ("jmacd-9920", "unreachable");
}

static const char * ncoord_tween (between_enum n)
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


void ncoord_print (const char * mes, const new_coord * coord, int node)
{
	if( coord == NULL ) {
		info( "%s: null\n", mes );
		return;
	}
	info ("%s: item_pos = %d, unit_pos %d, tween=%s\n",
	      mes, coord->item_pos, coord->unit_pos, 
	      ncoord_tween (coord->between));
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
