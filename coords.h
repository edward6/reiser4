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
	BEFORE_UNIT,
	AT_UNIT,
	AFTER_UNIT,
	BEFORE_ITEM,
	AFTER_ITEM 
} between_enum;

struct tree_coord {
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

/*
 * this structure is used to pass both coord and lock handle from extent_read
 * down to extent_readpage via read_cache_page which can deliver to filler only
 * one parameter specified by its caller
 */
struct readpage_arg {
	tree_coord * coord;
	lock_handle * lh;
};

extern int init_coord( tree_coord *coord );
extern void dup_coord(tree_coord * new, const tree_coord * old);
extern int done_coord( tree_coord *coord );

extern int coord_correct (const tree_coord * coord);
extern int coord_of_item (const tree_coord * coord);
extern int coord_of_unit (const tree_coord * coord);
extern int coord_is_in_node (const tree_coord * coord);
extern int coord_next_unit (tree_coord * coord);
extern int coord_prev_unit (tree_coord * coord);
extern void coord_first_item_unit (tree_coord * coord);
extern void coord_last_item_unit (tree_coord * coord);
extern void coord_first_unit (tree_coord * coord, znode *node);
extern void coord_last_unit (tree_coord * coord, znode *node);
extern int coord_next_item (tree_coord * coord);
extern int coord_prev_item (tree_coord * coord);
extern int coord_set_to_left (tree_coord * coord);
extern int coord_set_to_right (tree_coord * coord);
extern void coord_print (char * mes, const tree_coord * coord);
extern unsigned coord_num_units (const tree_coord * coord);
extern unsigned coord_last_unit_pos (const tree_coord * coord);
extern int coord_between_items (const tree_coord * coord);
extern int coord_after_last (const tree_coord * coord);

extern coord_cmp compare_coords (tree_coord * c1, tree_coord * c2);

extern int coord_is_leftmost( const tree_coord *coord );
extern int coord_is_rightmost( const tree_coord *coord );
extern int coord_is_utmost( const tree_coord *coord, sideof side );
extern int coord_is_after_item( const tree_coord * coord );
extern int coord_is_before_item (const tree_coord * coord);
extern int coord_eq( const tree_coord *c1, const tree_coord *c2 );

extern int  coord_are_neighbors( tree_coord *c1, tree_coord *c2 );
extern void print_coord (const char * mes, const tree_coord * coord, int);

extern coord_wrt_node coord_wrt( const tree_coord *coord );

static inline sideof sideof_reverse (sideof side)
{
	return side == LEFT_SIDE ? RIGHT_SIDE : LEFT_SIDE;
}

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
