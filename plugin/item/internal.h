/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Internal item contains down-link to the child of the internal/twig
 * node in a tree. It is internal items that are actually used during
 * tree traversal.
 */

#if !defined( __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__ )
#define __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__

/** operations specific to internal item. Used in
    fs/reiser4/plugin/item/item.h:item_ops */
typedef struct {
	/** all tree traversal want to know from internal item is where
	    to go next. */
	void ( *down_link )( const tree_coord *coord, 
			     const reiser4_key *key, 
			     reiser4_disk_addr *block );
	/** check that given internal item contains given pointer. */
	int ( *has_pointer_to )( const tree_coord *coord, 
				 const reiser4_disk_addr *block );
} internal_item_ops;

/** on-disk layout of internal item */
typedef struct internal_item_layout {
	/*  0 */ dblock_nr pointer;
	/*  4 */
} internal_item_layout;

int internal_mergeable (const tree_coord * p1, const tree_coord * p2);
lookup_result internal_lookup (const reiser4_key * key, lookup_bias bias,
			       tree_coord * coord);
/** store pointer from internal item into "block". Implementation of
    ->down_link() method */
extern void internal_down_link    ( const tree_coord *coord, 
				    const reiser4_key *key, 
				    reiser4_disk_addr *block );
extern int internal_has_pointer_to( const tree_coord *coord, 
				    const reiser4_disk_addr *block );
extern int internal_create_hook   ( const tree_coord *item, void *arg );
extern int internal_kill_hook     ( const tree_coord *item, 
				    unsigned from, unsigned count );
extern int internal_shift_hook    ( const tree_coord *item, 
				    unsigned from, unsigned count, 
				    znode *old_node );
extern void internal_print        ( const char *prefix, tree_coord *coord );

#define item_is_internal(coord) \
(item_plugin_id (item_plugin_by_coord (coord)) == INTERNAL_ITEM_ID)

/* __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__ */
#endif

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
