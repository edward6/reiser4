/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Coords
 */

#if !defined( __REISER4_COORDS_H__ )
#define __REISER4_COORDS_H__


/* insertions happen between coords in the tree, so we need some means
   of specifying the sense of betweenness.  */
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
};

typedef enum {
	LEFT_SIDE,
	RIGHT_SIDE
} sideof;

int coord_correct (const tree_coord * coord);
int coord_of_item (const tree_coord * coord);
int coord_of_unit (const tree_coord * coord);
int coord_is_in_node (const tree_coord * coord);
int coord_next (tree_coord * coord);
int coord_prev (tree_coord * coord);
void coord_first_unit (tree_coord * coord);
void coord_last_unit (tree_coord * coord);
int coord_set_to_left (tree_coord * coord);
int coord_set_to_right (tree_coord * coord);
void coord_print (char * mes, const tree_coord * coord);
unsigned num_items (const znode * node);
unsigned num_units (const tree_coord * coord);
unsigned last_unit_pos (const tree_coord * coord);
int coord_between_items (const tree_coord * coord);
int coord_after_last (const tree_coord * coord);

int left_item_pos (tree_coord * coord);
unsigned right_item_pos (tree_coord * coord);

typedef enum {
	COORD_CMP_SAME = 0, COORD_CMP_ON_LEFT = -1, COORD_CMP_ON_RIGHT = +1
} coord_cmp;
coord_cmp compare_coords (tree_coord * c1, tree_coord * c2);

extern int coord_is_leftmost( const tree_coord *coord );
extern int coord_is_rightmost( const tree_coord *coord );
extern int coord_is_utmost( const tree_coord *coord, sideof side );
extern int coord_is_after_item( const tree_coord * coord );

extern int  coord_are_neighbors( tree_coord *c1, tree_coord *c2 );
void print_coord (const char * mes, const tree_coord * coord, int);

/* The following are examples of things that you might or might not
   want when remembering state regarding where you are in the tree. */
#if 0
	/** item plugin */
	reiser4_plugin     *plugin;
	/** key of this point */
	reiser4_key        *key;
	/** where to store key if node/item implements some form of key
	    compression */
	reiser4_key         key_space;
	/** this is incremented by tree operation if data were loaded
	    into znode of this point as part of tree operation */
	int                 data_loaded;
	/** pointer to start of item in node. Call unpack_item( point )
	    before accessing this, and pack_item( point ) after */
	void               *item_data;
	/** length of item */
	int                 item_length;
	znode_lock_mode     lock_mode;
	union __validness {
		struct __bits {
			__u32               plugin_valid :1;
			__u32               key_valid    :1;
			__u32               item_valid   :1;
		} b;
		__u32 flat;
	} validness;

#endif


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
