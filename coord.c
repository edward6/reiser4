/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/* Internal constructor. */
/* Audited by: green(2002.06.15) */
static inline void ncoord_init_values (coord_t  *coord,
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

/* after shifting coord previously set properly may become invalid. */
/* Audited by: green(2002.06.15) */
void ncoord_normalize (coord_t * coord)
{
	znode * node;

	node = coord->node;
	assert ("vs-683", node);

	if (node_is_empty (node)) {
		ncoord_init_first_unit (coord, node);
	} else if (coord->item_pos == ncoord_num_items (coord) &&
		   coord->between == BEFORE_ITEM) {
		coord->item_pos --;
		coord->between = AFTER_ITEM;
	} else if (coord->unit_pos == ncoord_num_units (coord) &&
		   coord->between == BEFORE_UNIT) {
		coord->unit_pos --;
		coord->between = AFTER_UNIT;
	} else if (coord->item_pos == ncoord_num_items (coord) &&
		   coord->unit_pos == 0 &&
		   coord->between == BEFORE_UNIT) {
		coord->item_pos --;
		coord->unit_pos = 0;
		coord->between = AFTER_ITEM;
	}
}

/* Copy a coordinate. */
/* Audited by: green(2002.06.15) */
void ncoord_dup (coord_t *coord, const coord_t *old_coord)
{
	assert ("jmacd-9800", ncoord_check (old_coord));
	ncoord_dup_nocheck (coord, old_coord);
}

/* Copy a coordinate without check. Useful when old_coord->node is not
 * loaded. As in cbk_tree_lookup -> connect_znode -> connect_one_side */
/* Audited by: green(2002.06.15) */
void ncoord_dup_nocheck (coord_t *coord, const coord_t *old_coord)
{
	coord->node     = old_coord->node;
	coord->item_pos = old_coord->item_pos;
	coord->unit_pos = old_coord->unit_pos;
	coord->between  = old_coord->between;
}


/* Initialize an invalid coordinate. */
/* Audited by: green(2002.06.15) */
void ncoord_init_invalid (coord_t *coord, znode *node)
{
	ncoord_init_values (coord, node, 0, 0, INVALID_COORD);
}

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
/* Audited by: green(2002.06.15) */
void ncoord_init_first_unit (coord_t *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : AT_UNIT));

	assert ("jmacd-9801", ncoord_check (coord));
}

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
/* Audited by: green(2002.06.15) */
void ncoord_init_last_unit  (coord_t *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node,
			    (is_empty ? 0 : node_num_items (node) - 1), 0,
			    (is_empty ? EMPTY_NODE : AT_UNIT));
	if (!is_empty)
		coord->unit_pos = ncoord_last_unit_pos (coord);
	assert ("jmacd-9802", ncoord_check (coord));
}

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
/* Audited by: green(2002.06.15) */
void ncoord_init_before_first_item (coord_t *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, 0, 0, (is_empty ? EMPTY_NODE : BEFORE_ITEM));

	assert ("jmacd-9803", ncoord_check (coord));
}

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
/* Audited by: green(2002.06.15) */
void ncoord_init_after_last_item (coord_t *coord, znode *node)
{
	int is_empty = node_is_empty (node);

	ncoord_init_values (coord, node, (is_empty ? 0 : node_num_items (node) - 1), 0, (is_empty ? EMPTY_NODE : AFTER_ITEM));

	assert ("jmacd-9804", ncoord_check (coord));
}

/* Initialize a parent hint pointer. (parent hint pointer is a field in znode,
 * look for comments there) */
/* Audited by: green(2002.06.15) */
void ncoord_init_parent_hint (coord_t *coord, znode *node)
{
	coord->node = node;
	coord->item_pos = ~0u;
}

/* Initialize a coordinate by 0s. Used in places where init_coord was used and
 * it was not clear how actually */
/* Audited by: green(2002.06.15) */
void ncoord_init_zero (coord_t *coord)
{
	memset (coord, 0, sizeof (*coord));
}


/* Return the number of items at the present node.  Asserts coord->node != NULL. */
/* Audited by: green(2002.06.15) */
unsigned ncoord_num_items (const coord_t * coord)
{
	assert ("jmacd-9805", coord->node != NULL);

	return node_num_items (coord->node);
}

/* Return the number of units at the present item.  Asserts ncoord_is_existing_item(). */
/* Audited by: green(2002.06.15) */
unsigned ncoord_num_units (const coord_t * coord)
{
	assert ("jmacd-9806", ncoord_is_existing_item (coord));

	return item_plugin_by_coord (coord)->common.nr_units (coord);
}

/* Return the last valid unit number at the present item (i.e., ncoord_num_units() - 1). */
/* Audited by: green(2002.06.15) */
unsigned ncoord_last_unit_pos (const coord_t * coord)
{
	return ncoord_num_units (coord) - 1;
}

/* Returns the current item positions.  Asserts non-empty. */
/* Audited by: green(2002.06.15) */
unsigned ncoord_item_pos (const coord_t *coord)
{
	assert ("jmacd-5512", coord->between != EMPTY_NODE && coord->between != INVALID_COORD);
	assert ("jmacd-5513", node_num_items (coord->node) != 0);
	return coord->item_pos;
}

/* Returns true if the coord was initializewd by ncoord_init_invalid (). */
/* Audited by: green(2002.06.15) */
int ncoord_is_invalid (const coord_t *coord)
{
	return coord->between == INVALID_COORD;
}

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not. */
/* Audited by: green(2002.06.15) */
int ncoord_is_existing_item (const coord_t * coord)
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
	return 0;
}

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
/* Audited by: green(2002.06.15) */
int ncoord_is_existing_unit (const coord_t *coord)
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
	return 0;
}

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
/* Audited by: green(2002.06.15) */
int ncoord_is_leftmost_unit (const coord_t *coord)
{
	return (coord->between == AT_UNIT &&
		coord->item_pos == 0 &&
		coord->unit_pos == 0);
}

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
/* Audited by: green(2002.06.15) */
int ncoord_is_rightmost_unit (const coord_t *coord)
{
	assert ("jmacd-9809", ncoord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == ncoord_num_units (coord) - 1 &&
		coord->unit_pos == ncoord_last_unit_pos (coord));
}

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
/* Audited by: green(2002.06.15) */
int ncoord_is_rightmost_item (const coord_t *coord)
{
	assert ("jmacd-9820", ncoord_is_existing_item (coord));
	return (coord->between == AT_UNIT &&
		coord->item_pos == ncoord_num_units (coord) - 1);
}

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
int ncoord_check (const coord_t *coord)
{
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

	/*
	 * FIXME-VS: we are going to check unit_pos. This makes no sense when
	 * between is set either AFTER_ITEM or BEFORE_ITEM
	 */
	if (coord->between == AFTER_ITEM || coord->between == BEFORE_ITEM)
		return 1;

	if (coord->unit_pos > 
	    item_plugin_by_coord (coord)->common.nr_units (coord) - 1) {
		return 0;
	}

	return 1;
}
#endif

/* Adjust coordinate boundaries based on the number of items prior to ncoord_next/prev.
 * Returns 1 if the new position is does not exist. */
/* Audited by: green(2002.06.15) */
static int ncoord_adjust_items (coord_t *coord, unsigned items, int is_next)
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
/* Audited by: green(2002.06.15) */
int ncoord_next_unit (coord_t *coord)
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
	return 0;
}

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
/* Audited by: green(2002.06.15) */
int ncoord_next_item (coord_t *coord)
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
	return 0;
}

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
/* Audited by: green(2002.06.15) */
int ncoord_prev_unit (coord_t *coord)
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
	return 0;
}

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
/* Audited by: green(2002.06.15) */
int ncoord_prev_item (coord_t *coord)
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
	return 0;
}

/* Calls either ncoord_init_first_unit or ncoord_init_last_unit depending on sideof argument. */
/* Audited by: green(2002.06.15) */
void ncoord_init_sideof_unit (coord_t *coord, znode *node, sideof dir)
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
/* Audited by: green(2002.06.15) */
int ncoord_is_after_sideof_unit (coord_t *coord, sideof dir)
{
	assert ("jmacd-9822", dir == LEFT_SIDE || dir == RIGHT_SIDE);
	if (dir == LEFT_SIDE) {
		return ncoord_is_before_leftmost (coord);
	} else {
		return ncoord_is_after_rightmost (coord);
	}
}

/* Calls either ncoord_next_unit or ncoord_prev_unit depending on sideof argument. */
/* Audited by: green(2002.06.15) */
int ncoord_sideof_unit (coord_t *coord, sideof dir)
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
/* Audited by: green(2002.06.15) */
int ncoord_eq (const coord_t *c1, const coord_t *c2)
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
	return 0;
}

/* If ncoord_is_after_rightmost return NCOORD_ON_THE_RIGHT, if ncoord_is_after_leftmost
 * return NCOORD_ON_THE_LEFT, otherwise return NCOORD_INSIDE. */
/* Audited by: green(2002.06.15) */
coord_wrt_node ncoord_wrt (const coord_t *coord)
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
/* Audited by: green(2002.06.15) */
int ncoord_is_after_rightmost (const coord_t *coord)
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
	return 0;
}

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
/* Audited by: green(2002.06.15) */
int ncoord_is_before_leftmost (const coord_t *coord)
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
	return 0;
}

/* Returns true if the coordinate is positioned after a item, before a item, after the
 * last unit of an item, before the first unit of an item, or at an empty node. */
/* Audited by: green(2002.06.15) */
int ncoord_is_between_items (const coord_t *coord)
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
	return 0;
}

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
/* Audited by: green(2002.06.15) */
int ncoord_are_neighbors (coord_t *c1, coord_t *c2)
{
	coord_t *left;
	coord_t *right;

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
/* Audited by: green(2002.06.15) */
coord_cmp ncoord_compare (coord_t * c1, coord_t * c2)
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
/* Audited by: green(2002.06.15) */
int ncoord_is_delimiting (coord_t *coord, sideof dir)
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
/* Audited by: green(2002.06.15) */
int ncoord_set_to_unit (coord_t *coord)
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
	return 0;
}

/* If the coordinate is between items, shifts it to the right.  Returns 0 on success and
 * non-zero if there is no position to the right. */
/* Audited by: green(2002.06.15) */
int ncoord_set_to_right (coord_t *coord)
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
	return 0;
}

/* If the coordinate is between items, shifts it to the left.  Returns 0 on success and
 * non-zero if there is no position to the left. */
/* Audited by: green(2002.06.15) */
int ncoord_set_to_left (coord_t *coord)
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
		coord->between  = AT_UNIT;
		coord->unit_pos = ncoord_last_unit_pos (coord);
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
	return 0;
}

/* return true if coord is set after last unit in an item. 0 - otherwise. It is
 * used mostly to avoid repeated tree traversals writing to a file
 * sequentially */
int ncoord_is_after_last_unit (coord_t *coord)
{
	assert ("vs-729", ncoord_check (coord));
	if (!ncoord_is_existing_item (coord))
		return 0;
	if (coord->between != AFTER_UNIT)
		return 0;
	if (coord->unit_pos != ncoord_last_unit_pos (coord))
		return 0;
	return 1;
}

/* Audited by: green(2002.06.15) */
static const char * ncoord_tween (between_enum n)
{
	switch (n) {
	case BEFORE_UNIT: return "before unit";
	case BEFORE_ITEM: return "before item";
	case AT_UNIT: return "at unit";
	case AFTER_UNIT: return "after unit";
	case AFTER_ITEM: return "after item";
	case EMPTY_NODE: return "empty node";
	case INVALID_COORD: return "invalid";
	default: return "unknown";
	}
}


/* Audited by: green(2002.06.15) */
void ncoord_print (const char * mes, const coord_t * coord, int node)
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

/* Hack. */
/* Audited by: green(2002.06.15) */
void ncoord_to_tcoord (coord_t *tcoord, const coord_t *ncoord)
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

/* Audited by: green(2002.06.15) */
void tcoord_to_ncoord (coord_t *ncoord, const coord_t *tcoord)
{
	unsigned items;

	if (tcoord->node == NULL) {
		ncoord_init_invalid (ncoord, NULL);
		return;
	}

	items = node_num_items (tcoord->node);

	if (items == 0) {
		ncoord->item_pos = 0;
		ncoord->unit_pos = 0;
		ncoord->node     = tcoord->node;
		ncoord->between  = EMPTY_NODE;
		return;
	}

	ncoord->item_pos = tcoord->item_pos;
	ncoord->unit_pos = tcoord->unit_pos;
	ncoord->node     = tcoord->node;
	ncoord->between  = tcoord->between;

	assert ("jmacd-5111", ncoord->between != INVALID_COORD && ncoord->between != EMPTY_NODE);
	assert ("jmacd-5112", ncoord->item_pos < items);
	assert ("jmacd-5113", ncoord->unit_pos <= ncoord_last_unit_pos (ncoord));
}

/*item_plugin* item_plugin_by_ncoord (const coord_t *ncoord)
{
	coord_t tcoord;
	ncoord_to_tcoord (& tcoord, ncoord);
	return item_plugin_by_coord (& tcoord);
}*/

int item_utmost_child_real_block (const coord_t *coord, sideof side, reiser4_block_nr *blk)
{
	return item_plugin_by_coord (coord)->common.utmost_child_real_block (coord, side, blk);
}

int item_utmost_child (const coord_t *coord, sideof side, jnode **child)
{
	return item_plugin_by_coord (coord)->common.utmost_child (coord, side, child);
}

int item_is_extent_n (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return item_is_extent (coord);
}

int item_is_internal_n (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return item_is_internal (coord);
}

void item_key_by_ncoord (const coord_t *coord, reiser4_key *key)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	item_key_by_coord (coord, key);
}

int item_length_by_ncoord (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return item_length_by_coord (coord);
}

znode* child_znode_n (const coord_t *coord, int set_delim)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return child_znode (coord, set_delim);
}

int cut_node_n (coord_t * from, coord_t * to,
		const reiser4_key * from_key,
		const reiser4_key * to_key,
		reiser4_key * smallest_removed, unsigned flags,
		znode * left)
{
	int ret;
/*	coord_t tfrom, tto;
	ncoord_to_tcoord (&tfrom, from);
	ncoord_to_tcoord (&tto, to);*/
	ret = cut_node (from, to, from_key, to_key, smallest_removed, flags, left);
/*	tcoord_to_ncoord (from, &tfrom);
	tcoord_to_ncoord (to, &tto);*/
	return ret;
}

int allocate_extent_item_in_place_n (coord_t * item, reiser4_blocknr_hint * preceder)
{
	int ret;
/*	coord_t titem;
	ncoord_to_tcoord (&titem, item);*/
	ret = allocate_extent_item_in_place (item, preceder);
/*	tcoord_to_ncoord (item, &titem);*/
	return ret;
}

int allocate_and_copy_extent_n (znode * left, coord_t * right,
				reiser4_blocknr_hint * preceder,
				reiser4_key * stop_key)
{
	int ret;
/*	coord_t tright;
	ncoord_to_tcoord (&tright, right);*/
	ret = allocate_and_copy_extent (left, right, preceder, stop_key);
/*	tcoord_to_ncoord (right, &tright);*/
	return ret;
}

int extent_is_allocated_n (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return extent_is_allocated (coord);
}

__u64 extent_unit_index_n (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return extent_unit_index (coord);
}

__u64 extent_unit_width_n (const coord_t *coord)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	return extent_unit_width (coord);
}

void extent_get_inode_n (const coord_t *coord, struct inode **inode)
{
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	extent_get_inode (coord, inode);
}

int node_shift_n (znode *pnode, coord_t *coord, znode *snode, sideof side,
		  int del_right,
		  int move_coord,
		  carry_level *todo)
{
	int ret;
/*	coord_t tcoord;
	ncoord_to_tcoord (&tcoord, coord);*/
	ret = node_plugin_by_node (pnode)->shift (coord, snode, side, del_right, move_coord, todo);
/*	tcoord_to_ncoord (coord, &tcoord);*/
	return ret;
}

lookup_result ncoord_by_key (reiser4_tree *tree, const reiser4_key *key,
			     coord_t *coord, lock_handle * handle,
			     znode_lock_mode lock, lookup_bias bias, 
			     tree_level lock_level, tree_level stop_level, 
			     __u32 flags )
{
	lookup_result ret;
/*	coord_t tcoord;*/
	//ncoord_to_tcoord (&tcoord, coord);
	ret = coord_by_key (tree, key, coord, handle, lock, bias, lock_level, stop_level, flags);
/*	tcoord_to_ncoord (coord, &tcoord);*/
	return ret;
}

int find_child_ptr_n (znode *parent, znode *child, coord_t *result)
{
	int ret;
	//coord_t tcoord;
	//ncoord_to_tcoord (&tcoord, coord);
	ret = find_child_ptr (parent, child, result);
/*	tcoord_to_ncoord (result, &tcoord);*/
	return ret;
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
