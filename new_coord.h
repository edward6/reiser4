/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Coords
 */

#if !defined( __REISER4_NEW_COORD_H__ )
#define __REISER4_NEW_COORD_H__

struct new_coord {
	/* node in a tree */
	znode *node;

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
 * But I think the chosen names are decent the way they are.
 */

/*****************************************************************************************/
/*				    COORD INITIALIZERS                                   */
/*****************************************************************************************/

/* Hack. */
extern void ncoord_to_tcoord (tree_coord *tcoord, const new_coord *ncoord);
extern void tcoord_to_ncoord (new_coord *ncoord, const tree_coord *tcoord);

extern int          item_utmost_child_real_block (const new_coord *coord, sideof side, reiser4_block_nr *blk);
extern int          item_utmost_child            (const new_coord *coord, sideof side, jnode **child);
extern int          item_is_extent_n             (const new_coord *coord);
extern int          item_is_internal_n           (const new_coord *coord);
extern void         item_key_by_ncoord           (const new_coord *coord, reiser4_key *key);
extern int          item_length_by_ncoord        (const new_coord *coord);

extern znode*       child_znode_n                (const new_coord *coord, int set_delim);
extern int          cut_node_n                   (new_coord * from, new_coord * to,
						  const reiser4_key * from_key,
						  const reiser4_key * to_key,
						  reiser4_key * smallest_removed, unsigned flags,
						  znode * left);
extern int allocate_extent_item_in_place_n       (new_coord * item, reiser4_blocknr_hint * preceder);
extern int allocate_and_copy_extent_n            (znode * left, new_coord * right,
						  reiser4_blocknr_hint * preceder,
						  reiser4_key * stop_key);

extern int   extent_is_allocated_n               (const new_coord *item);
extern __u64 extent_unit_index_n                 (const new_coord *item);
extern __u64 extent_unit_width_n                 (const new_coord *item);
extern void  extent_get_inode_n                  (const new_coord *item, struct inode **inode);

extern int   node_shift_n (znode *pnode, new_coord *coord, znode *snode, sideof side,
			   int del_right,
			   int move_coord,
			   carry_level *todo);
extern lookup_result ncoord_by_key( reiser4_tree *tree, const reiser4_key *key,
				    new_coord *coord, lock_handle * handle,
				    znode_lock_mode lock, lookup_bias bias, 
				    tree_level lock_level, tree_level stop_level, 
				    __u32 flags );

extern int find_child_ptr_n( znode *parent, znode *child, new_coord *result );

/* Initialize an invalid coordinate. */
extern void ncoord_init_invalid (new_coord *coord, znode *node);

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void ncoord_init_first_unit (new_coord *coord, znode *node);

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void ncoord_init_last_unit (new_coord *coord, znode *node);

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
extern void ncoord_init_before_first_item (new_coord *coord, znode *node);

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
extern void ncoord_init_after_last_item (new_coord *coord, znode *node);

/* Calls either ncoord_init_first_unit or ncoord_init_last_unit depending on sideof argument. */
extern void ncoord_init_sideof_unit (new_coord *coord, znode *node, sideof dir);

/*****************************************************************************************/
/*				      COORD METHODS                                      */
/*****************************************************************************************/

/* Copy a coordinate. */
extern void ncoord_dup (new_coord *coord, const new_coord *old_coord);

/* Return the number of items at the present node.  Asserts coord->node != NULL. */
extern unsigned ncoord_num_items (const new_coord *coord);

/* Return the number of units at the present item.  Asserts ncoord_is_existing_item(). */
extern unsigned ncoord_num_units (const new_coord *coord);

/* Return the last valid unit number at the present item (i.e., ncoord_num_units() - 1). */
extern unsigned ncoord_last_unit_pos (const new_coord *coord);

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
extern int  ncoord_check (const new_coord *coord);
#endif

/* Returns true if two coordinates are consider equal.  Coordinates that are between units
 * or items are considered equal. */
extern int ncoord_eq (const new_coord *c1, const new_coord *c2);

/* For debugging, error messages. */
extern void ncoord_print (const char * mes, const new_coord * coord, int print_node);

/* If ncoord_is_after_rightmost return NCOORD_ON_THE_RIGHT, if ncoord_is_after_leftmost
 * return NCOORD_ON_THE_LEFT, otherwise return NCOORD_INSIDE. */
extern coord_wrt_node ncoord_wrt (const new_coord *coord);

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
extern int  ncoord_are_neighbors (new_coord *c1, new_coord *c2);

/* Assuming two coordinates are positioned in the same node, return NCOORD_CMP_ON_RIGHT,
 * NCOORD_CMP_ON_LEFT, or NCOORD_CMP_SAME depending on c1's position relative to c2.  */
extern coord_cmp ncoord_compare (new_coord * c1, new_coord * c2);
/* Returns the current item positions.  Asserts non-empty. */
extern unsigned ncoord_item_pos (const new_coord *coord);

/*****************************************************************************************/
/*				     COORD PREDICATES                                    */
/*****************************************************************************************/

/* Returns true if the coord was initializewd by ncoord_init_invalid (). */
extern int ncoord_is_invalid (const new_coord *coord);

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not.  If this is true you can call methods of the item plugin.  */
extern int ncoord_is_existing_item (const new_coord *coord);

/* Returns true if the coordinate is positioned after a item, before a item, after the
 * last unit of an item, before the first unit of an item, or at an empty node. */
extern int ncoord_is_between_items (const new_coord *coord);

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
extern int ncoord_is_existing_unit (const new_coord *coord);

/* Returns true if the coordinate is positioned at an empty node. */
extern int ncoord_is_empty (const new_coord *coord);

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
extern int ncoord_is_leftmost_unit (const new_coord *coord);

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
extern int ncoord_is_rightmost_unit (const new_coord *coord);

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
extern int ncoord_is_rightmost_item (const new_coord *coord);

/* Returns true if the coordinate is positioned after the last item or after the last unit
 * of the last item or it is an empty node. */
extern int ncoord_is_after_rightmost (const new_coord *coord);

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
extern int ncoord_is_before_leftmost (const new_coord *coord);

/* Calls either ncoord_is_before_leftmost or ncoord_is_after_rightmost depending on sideof
 * argument. */
extern int ncoord_is_after_sideof_unit (new_coord *coord, sideof dir);

/* Returns true if coord is set to or before the first (if LEFT_SIDE) unit of the item and
 * to or after the last (if RIGHT_SIDE) unit of the item. */
extern int ncoord_is_delimiting (new_coord *coord, sideof dir);

/*****************************************************************************************/
/* 				      COORD MODIFIERS                                    */
/*****************************************************************************************/

/* Advances the coordinate by one unit to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing unit. */
extern int ncoord_next_unit (new_coord *coord);

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
extern int ncoord_next_item (new_coord *coord);

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
extern int ncoord_prev_unit (new_coord *coord);

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
extern int ncoord_prev_item (new_coord *coord);

/* If the coordinate is between items, shifts it to the right.  Returns 0 on success and
 * non-zero if there is no position to the right. */
extern int ncoord_set_to_right (new_coord *coord);

/* If the coordinate is between items, shifts it to the left.  Returns 0 on success and
 * non-zero if there is no position to the left. */
extern int ncoord_set_to_left (new_coord *coord);

/* If the coordinate is before an item/unit, set to next item/unit.  If the coordinate is
 * after an item/unit, set to the previous item/unit.  Returns 0 on success and non-zero
 * if there is no position (i.e., if the coordinate is empty). */
extern int ncoord_set_to_unit (new_coord *coord);

/* If the coordinate is at an existing unit, set to after that unit.  Returns 0 on success
 * and non-zero if the unit did not exist. */
extern int ncoord_set_after_unit (new_coord *coord);

/* Calls either ncoord_next_unit or ncoord_prev_unit depending on sideof argument. */
extern int ncoord_sideof_unit (new_coord *coord, sideof dir);

/* __REISER4_NEW_COORD_H__ */
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
