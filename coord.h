/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Coords
 */

#if !defined( __REISER4_COORDS_H__ )
#define __REISER4_COORDS_H__

/**
 * location of coord w.r.t. its node
 */
typedef enum {
	COORD_ON_THE_LEFT  = -1,
	COORD_ON_THE_RIGHT = +1,
	COORD_INSIDE       = 0
} coord_wrt_node;

/**
 * result of compare_coords
 */
typedef enum {
	COORD_CMP_SAME = 0,
	COORD_CMP_ON_LEFT = -1,
	COORD_CMP_ON_RIGHT = +1,
} coord_cmp;

/**
 * insertions happen between coords in the tree, so we need some means
 * of specifying the sense of betweenness.
 */
typedef enum {
	INVALID_COORD,
	EMPTY_NODE,
	BEFORE_UNIT,
	AT_UNIT,
	AFTER_UNIT,
	BEFORE_ITEM,
	AFTER_ITEM 
} between_enum;

struct tree_coord {
	/* node in a tree */
	znode *node;

#if 1 /* def DONTUSE_COORD_FIELDS*/
	/* position of item within node */
	pos_in_node  item_pos;
	/* position of unit within item */
	pos_in_item  unit_pos;
	/** 
	 * position of coord w.r.t. to neighboring items and/or units. 
	 * Values are taken from &between_enum above.
	 */
	between_enum  between;
	/*
	 * FIXME-NIKITA possible future optimization: store plugin id of item
	 * here. Profiling shows that node40_plugin_by_coord() is top CPU
	 * user.
	 */
#endif
};

/*
 * this structure is used to pass both coord and lock handle from extent_read
 * down to extent_readpage via read_cache_page which can deliver to filler only
 * one parameter specified by its caller
 */
struct readpage_arg {
	tree_coord * coord;
	lock_handle * lh;
};

/* Reverse a direction. */
static inline sideof sideof_reverse (sideof side)
{
	return side == LEFT_SIDE ? RIGHT_SIDE : LEFT_SIDE;
}

/* NOTE: There is a somewhat odd mixture of the following opposed terms:
 *
 * "first" and "last"
 * "next" and "prev"
 * "before" and "after"
 * "leftmost" and "rightmost"
 *
 * But I the chosen names are decent the way they are.
 */

/*****************************************************************************************/
/*				    COORD INITIALIZERS                                   */
/*****************************************************************************************/

/* Initialize an invalid coordinate. */
extern void coord_init_invalid (tree_coord *coord, znode *node);

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void coord_init_first_unit (tree_coord *coord, znode *node);

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void coord_init_last_unit (tree_coord *coord, znode *node);

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
extern void coord_init_before_first_item (tree_coord *coord, znode *node);

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
extern void coord_init_after_last_item (tree_coord *coord, znode *node);

/* Calls either coord_init_first_unit or coord_init_last_unit depending on sideof argument. */
extern void coord_init_sideof_unit (tree_coord *coord, znode *node, sideof dir);

/*****************************************************************************************/
/*				      COORD METHODS                                      */
/*****************************************************************************************/

/* Copy a coordinate. */
extern void coord_dup (tree_coord *new_coord, const tree_coord *old_coord);

/* Return the number of items at the present node.  Asserts coord->node != NULL. */
extern unsigned coord_num_items (const tree_coord *coord);

/* Return the number of units at the present item.  Asserts coord_is_existing_item(). */
extern unsigned coord_num_units (const tree_coord *coord);

/* Return the last valid unit number at the present item (i.e., coord_num_units() - 1). */
extern unsigned coord_last_unit_pos (const tree_coord *coord);

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
extern int  coord_check (const tree_coord *coord);
#endif

/* Returns true if two coordinates are consider equal.  Coordinates that are between units
 * or items are considered equal. */
extern int coord_eq (const tree_coord *c1, const tree_coord *c2);

/* For debugging, error messages. */
extern void coord_print (const char * mes, const tree_coord * coord, int print_node);

/* If coord_is_after_rightmost return COORD_ON_THE_RIGHT, if coord_is_after_leftmost
 * return COORD_ON_THE_LEFT, otherwise return COORD_INSIDE. */
extern coord_wrt_node coord_wrt (const tree_coord *coord);

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
extern int  coord_are_neighbors (tree_coord *c1, tree_coord *c2);

/* Assuming two coordinates are positioned in the same node, return COORD_CMP_ON_RIGHT,
 * COORD_CMP_ON_LEFT, or COORD_CMP_SAME depending on c1's position relative to c2.  */
extern coord_cmp coord_compare (tree_coord * c1, tree_coord * c2);

/* Returns the current item positions.  Asserts non-empty. */
extern unsigned coord_item_pos (const tree_coord *coord);

/*****************************************************************************************/
/*				     COORD PREDICATES                                    */
/*****************************************************************************************/

/* Returns true if the coord was initializewd by coord_init_invalid (). */
extern int coord_is_invalid (const tree_coord *coord);

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not.  If this is true you can call methods of the item plugin.  */
extern int coord_is_existing_item (const tree_coord *coord);

/* Returns true if the coordinate is positioned after a item, before a item, after the
 * last unit of an item, before the first unit of an item, or at an empty node. */
extern int coord_is_between_items (const tree_coord *coord);

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
extern int coord_is_existing_unit (const tree_coord *coord);

/* Returns true if the coordinate is positioned at an empty node. */
extern int coord_is_empty (const tree_coord *coord);

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
extern int coord_is_leftmost_unit (const tree_coord *coord);

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
extern int coord_is_rightmost_unit (const tree_coord *coord);

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
extern int coord_is_rightmost_item (const tree_coord *coord);

/* Returns true if the coordinate is positioned after the last item or after the last unit
 * of the last item or it is an empty node. */
extern int coord_is_after_rightmost (const tree_coord *coord);

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
extern int coord_is_before_leftmost (const tree_coord *coord);

/* Calls either coord_is_before_leftmost or coord_is_after_rightmost depending on sideof
 * argument. */
extern int coord_is_after_sideof_unit (tree_coord *coord, sideof dir);

/* Returns true if coord is set to or before the first (if LEFT_SIDE) unit of the item and
 * to or after the last (if RIGHT_SIDE) unit of the item. */
extern int coord_is_delimiting (tree_coord *coord, sideof dir);

/*****************************************************************************************/
/* 				      COORD MODIFIERS                                    */
/*****************************************************************************************/

/* Advances the coordinate by one unit to the right.  If empty, no change.  If
 * coord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing unit. */
extern int coord_next_unit (tree_coord *coord);

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * coord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
extern int coord_next_item (tree_coord *coord);

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * coord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
extern int coord_prev_unit (tree_coord *coord);

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * coord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
extern int coord_prev_item (tree_coord *coord);

/* If the coordinate is between items, shifts it to the right.  Returns 0 on success and
 * non-zero if there is no position to the right. */
extern int coord_set_to_right (tree_coord *coord);

/* If the coordinate is between items, shifts it to the left.  Returns 0 on success and
 * non-zero if there is no position to the left. */
extern int coord_set_to_left (tree_coord *coord);

/* If the coordinate is before an item/unit, set to next item/unit.  If the coordinate is
 * after an item/unit, set to the previous item/unit.  Returns 0 on success and non-zero
 * if there is no position (i.e., if the coordinate is empty). */
extern int coord_set_to_unit (tree_coord *coord);

/* If the coordinate is at an existing unit, set to after that unit.  Returns 0 on success
 * and non-zero if the unit did not exist. */
extern int coord_set_after_unit (tree_coord *coord);

/* Calls either coord_next_unit or coord_prev_unit depending on sideof argument. */
extern int coord_sideof_unit (tree_coord *coord, sideof dir);

/* __REISER4_COORDS_H__ */
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
