/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Coords
 */

#if !defined( __REISER4_NEW_COORD_H__ )
#define __REISER4_NEW_COORD_H__

/**
 * insertions happen between coords in the tree, so we need some means
 * of specifying the sense of betweenness.
 */
typedef enum {
	BEFORE_UNIT, /* Note: we/init_coord depends on this value being zero. */
	AT_UNIT,
	AFTER_UNIT,
	BEFORE_ITEM,
	AFTER_ITEM ,
	INVALID_COORD,
	EMPTY_NODE,
} between_enum;

/**
 * location of coord w.r.t. its node
 */
typedef enum {
	COORD_ON_THE_LEFT  = -1,
	COORD_ON_THE_RIGHT = +1,
	COORD_INSIDE       = 0
} coord_wrt_node;

typedef enum {
	COORD_CMP_SAME = 0, COORD_CMP_ON_LEFT = -1, COORD_CMP_ON_RIGHT = +1
} coord_cmp;

struct coord {
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
/*
extern void ncoord_to_tcoord (coord_t *tcoord, const coord_t *ncoord);
extern void tcoord_to_ncoord (coord_t *ncoord, const coord_t *tcoord);
*/
extern int          item_utmost_child_real_block (const coord_t *coord, sideof side, reiser4_block_nr *blk);
extern int          item_utmost_child            (const coord_t *coord, sideof side, jnode **child);
extern int          item_is_extent_n             (const coord_t *coord);
extern int          item_is_internal_n           (const coord_t *coord);
extern void         item_key_by_ncoord           (const coord_t *coord, reiser4_key *key);
extern int          item_length_by_ncoord        (const coord_t *coord);

extern znode*       child_znode_n                (const coord_t *coord, int set_delim);
extern int          cut_node_n                   (coord_t * from, coord_t * to,
						  const reiser4_key * from_key,
						  const reiser4_key * to_key,
						  reiser4_key * smallest_removed, unsigned flags,
						  znode * left);
extern int allocate_extent_item_in_place_n       (coord_t * item, reiser4_blocknr_hint * preceder);
extern int allocate_and_copy_extent_n            (znode * left, coord_t * right,
						  reiser4_blocknr_hint * preceder,
						  reiser4_key * stop_key);

extern int   extent_is_allocated_n               (const coord_t *item);
extern __u64 extent_unit_index_n                 (const coord_t *item);
extern __u64 extent_unit_width_n                 (const coord_t *item);
extern void  extent_get_inode_n                  (const coord_t *item, struct inode **inode);

extern int   node_shift_n (znode *pnode, coord_t *coord, znode *snode, sideof side,
			   int del_right,
			   int move_coord,
			   carry_level *todo);
extern lookup_result ncoord_by_key( reiser4_tree *tree, const reiser4_key *key,
				    coord_t *coord, lock_handle * handle,
				    znode_lock_mode lock, lookup_bias bias, 
				    tree_level lock_level, tree_level stop_level, 
				    __u32 flags );

extern int find_child_ptr_n( znode *parent, znode *child, coord_t *result );

/* Initialize an invalid coordinate. */
extern void ncoord_init_invalid (coord_t *coord, znode *node);

/* Initialize a coordinate to point at the first unit of the first item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void ncoord_init_first_unit (coord_t *coord, znode *node);

/* Initialize a coordinate to point at the last unit of the last item.  If the node is
 * empty, it is positioned at the EMPTY_NODE. */
extern void ncoord_init_last_unit (coord_t *coord, znode *node);

/* Initialize a coordinate to before the first item.  If the node is empty, it is
 * positioned at the EMPTY_NODE. */
extern void ncoord_init_before_first_item (coord_t *coord, znode *node);

/* Initialize a coordinate to after the last item.  If the node is empty, it is positioned
 * at the EMPTY_NODE. */
extern void ncoord_init_after_last_item (coord_t *coord, znode *node);

/* Calls either ncoord_init_first_unit or ncoord_init_last_unit depending on sideof argument. */
extern void ncoord_init_sideof_unit (coord_t *coord, znode *node, sideof dir);

/* Initialize a parent hint pointer. (parent hint pointer is a field in znode,
 * look for comments there)
 * FIXME-VS: added by vs (2002, june, 8) */
extern void ncoord_init_parent_hint (coord_t *coord, znode *node);

/* Initialize a coordinate by 0s. Used in places where init_coord was used and
 * it was not clear how actually 
 * FIXME-VS: added by vs (2002, june, 8) */
extern void ncoord_init_zero (coord_t *coord);

/*****************************************************************************************/
/*				      COORD METHODS                                      */
/*****************************************************************************************/

/* */
void ncoord_normalize (coord_t * coord);

/* Copy a coordinate. */
extern void ncoord_dup (coord_t *coord, const coord_t *old_coord);

/* Copy a coordinate without check. */
void ncoord_dup_nocheck (coord_t *coord, const coord_t *old_coord);

/* Return the number of items at the present node.  Asserts coord->node != NULL. */
extern unsigned ncoord_num_items (const coord_t *coord);

/* Return the number of units at the present item.  Asserts ncoord_is_existing_item(). */
extern unsigned ncoord_num_units (const coord_t *coord);

/* Return the last valid unit number at the present item (i.e., ncoord_num_units() - 1). */
extern unsigned ncoord_last_unit_pos (const coord_t *coord);

#if REISER4_DEBUG
/* For assertions only, checks for a valid coordinate. */
extern int  ncoord_check (const coord_t *coord);
#endif

/* Returns true if two coordinates are consider equal.  Coordinates that are between units
 * or items are considered equal. */
extern int ncoord_eq (const coord_t *c1, const coord_t *c2);

/* For debugging, error messages. */
extern void ncoord_print (const char * mes, const coord_t * coord, int print_node);

/* If ncoord_is_after_rightmost return NCOORD_ON_THE_RIGHT, if ncoord_is_after_leftmost
 * return NCOORD_ON_THE_LEFT, otherwise return NCOORD_INSIDE. */
extern coord_wrt_node ncoord_wrt (const coord_t *coord);

/* Returns true if the coordinates are positioned at adjacent units, regardless of
 * before-after or item boundaries. */
extern int  ncoord_are_neighbors (coord_t *c1, coord_t *c2);

/* Assuming two coordinates are positioned in the same node, return NCOORD_CMP_ON_RIGHT,
 * NCOORD_CMP_ON_LEFT, or NCOORD_CMP_SAME depending on c1's position relative to c2.  */
extern coord_cmp ncoord_compare (coord_t * c1, coord_t * c2);
/* Returns the current item positions.  Asserts non-empty. */
extern unsigned ncoord_item_pos (const coord_t *coord);

/*****************************************************************************************/
/*				     COORD PREDICATES                                    */
/*****************************************************************************************/

/* Returns true if the coord was initializewd by ncoord_init_invalid (). */
extern int ncoord_is_invalid (const coord_t *coord);

/* Returns true if the coordinate is positioned at an existing item, not before or after
 * an item.  It may be placed at, before, or after any unit within the item, whether
 * existing or not.  If this is true you can call methods of the item plugin.  */
extern int ncoord_is_existing_item (const coord_t *coord);

/* Returns true if the coordinate is positioned after a item, before a item, after the
 * last unit of an item, before the first unit of an item, or at an empty node. */
extern int ncoord_is_between_items (const coord_t *coord);

/* Returns true if the coordinate is positioned at an existing unit, not before or after a
 * unit. */
extern int ncoord_is_existing_unit (const coord_t *coord);

/* Returns true if the coordinate is positioned at an empty node. */
extern int ncoord_is_empty (const coord_t *coord);

/* Returns true if the coordinate is positioned at the first unit of the first item.  Not
 * true for empty nodes nor coordinates positioned before the first item. */
extern int ncoord_is_leftmost_unit (const coord_t *coord);

/* Returns true if the coordinate is positioned at the last unit of the last item.  Not
 * true for empty nodes nor coordinates positioned after the last item. */
extern int ncoord_is_rightmost_unit (const coord_t *coord);

/* Returns true if the coordinate is positioned at any unit of the last item.  Not true
 * for empty nodes nor coordinates positioned after the last item. */
extern int ncoord_is_rightmost_item (const coord_t *coord);

/* Returns true if the coordinate is positioned after the last item or after the last unit
 * of the last item or it is an empty node. */
extern int ncoord_is_after_rightmost (const coord_t *coord);

/* Returns true if the coordinate is positioned before the first item or it is an empty
 * node. */
extern int ncoord_is_before_leftmost (const coord_t *coord);

/* Calls either ncoord_is_before_leftmost or ncoord_is_after_rightmost depending on sideof
 * argument. */
extern int ncoord_is_after_sideof_unit (coord_t *coord, sideof dir);

/* Returns true if coord is set to or before the first (if LEFT_SIDE) unit of the item and
 * to or after the last (if RIGHT_SIDE) unit of the item. */
extern int ncoord_is_delimiting (coord_t *coord, sideof dir);

/* determine how @coord is located w.r.t. its node.
 * FIXME-VS: added by vs (2002, june, 8) */
extern coord_wrt_node ncoord_wrt (const coord_t *coord);

/*****************************************************************************************/
/* 				      COORD MODIFIERS                                    */
/*****************************************************************************************/

/* Advances the coordinate by one unit to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing unit. */
extern int ncoord_next_unit (coord_t *coord);

/* Advances the coordinate by one item to the right.  If empty, no change.  If
 * ncoord_is_rightmost_unit, advances to AFTER THE LAST ITEM.  Returns 0 if new position is
 * an existing item. */
extern int ncoord_next_item (coord_t *coord);

/* Advances the coordinate by one unit to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing unit. */
extern int ncoord_prev_unit (coord_t *coord);

/* Advances the coordinate by one item to the left.  If empty, no change.  If
 * ncoord_is_leftmost_unit, advances to BEFORE THE FIRST ITEM.  Returns 0 if new position
 * is an existing item. */
extern int ncoord_prev_item (coord_t *coord);

/* If the coordinate is between items, shifts it to the right.  Returns 0 on success and
 * non-zero if there is no position to the right. */
extern int ncoord_set_to_right (coord_t *coord);

/* If the coordinate is between items, shifts it to the left.  Returns 0 on success and
 * non-zero if there is no position to the left. */
extern int ncoord_set_to_left (coord_t *coord);

/* If the coordinate is before an item/unit, set to next item/unit.  If the coordinate is
 * after an item/unit, set to the previous item/unit.  Returns 0 on success and non-zero
 * if there is no position (i.e., if the coordinate is empty). */
extern int ncoord_set_to_unit (coord_t *coord);

/* If the coordinate is at an existing unit, set to after that unit.  Returns 0 on success
 * and non-zero if the unit did not exist. */
extern int ncoord_set_after_unit (coord_t *coord);

/* Calls either ncoord_next_unit or ncoord_prev_unit depending on sideof argument. */
extern int ncoord_sideof_unit (coord_t *coord, sideof dir);

/** iterate over all units in @node */
#define for_all_units( coord, node )					\
	for( ncoord_init_before_first_item( ( coord ), ( node ) ) ; 	\
	     ncoord_next_unit( coord ) == 0 ; )

/** iterate over all items in @node */
#define for_all_items( coord, node )					\
	for( ncoord_init_before_first_item( ( coord ), ( node ) ) ; 	\
	     ncoord_next_item( coord ) == 0 ; )


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
