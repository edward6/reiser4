/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Coords
 */

#if !defined( __REISER4_COORDS_H__ )
#define __REISER4_COORDS_H__


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

/*
 * this structure is used to pass both coord and lock handle from extent_read
 * down to extent_readpage via read_cache_page which can deliver to filler only
 * one parameter specified by its caller
 */
struct readpage_arg {
	new_coord * coord;
	lock_handle * lh;
};

extern int init_coord( new_coord *coord );
extern void dup_coord(new_coord * new, const new_coord * old);
extern int done_coord( new_coord *coord );

int coord_correct (const new_coord * coord);
int coord_of_item (const new_coord * coord);
int coord_of_unit (const new_coord * coord);
int coord_is_in_node (const new_coord * coord);
int coord_next_unit (new_coord * coord);
int coord_prev_unit (new_coord * coord);
void coord_first_item_unit (new_coord * coord);
void coord_last_item_unit (new_coord * coord);
void coord_first_unit (new_coord * coord, znode *node);
void coord_last_unit (new_coord * coord, znode *node);
int coord_next_item (new_coord * coord);
int coord_prev_item (new_coord * coord);
int coord_set_to_left (new_coord * coord);
int coord_set_to_right (new_coord * coord);
void coord_print (char * mes, const new_coord * coord);
unsigned coord_num_units (const new_coord * coord);
unsigned last_unit_pos (const new_coord * coord);
int coord_between_items (const new_coord * coord);
int coord_after_last (const new_coord * coord);

int left_item_pos (const new_coord * coord);
unsigned right_item_pos (const new_coord * coord);

typedef enum {
	COORD_CMP_SAME = 0, COORD_CMP_ON_LEFT = -1, COORD_CMP_ON_RIGHT = +1
} coord_cmp;
coord_cmp compare_coords (new_coord * c1, new_coord * c2);

extern int coord_is_leftmost( const new_coord *coord );
extern int coord_is_rightmost( const new_coord *coord );
extern int coord_is_utmost( const new_coord *coord, sideof side );
extern int coord_is_after_item( const new_coord * coord );
int coord_is_before_item (const new_coord * coord, unsigned item_pos);
extern int coord_eq( const new_coord *c1, const new_coord *c2 );

extern int  coord_are_neighbors( new_coord *c1, new_coord *c2 );
void print_coord (const char * mes, const new_coord * coord, int);

/**
 * location of coord w.r.t. its node
 */
typedef enum {
	COORD_ON_THE_LEFT  = -1,
	COORD_ON_THE_RIGHT = +1,
	COORD_INSIDE       = 0
} coord_wrt_node;

extern coord_wrt_node coord_wrt( const new_coord *coord );

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
