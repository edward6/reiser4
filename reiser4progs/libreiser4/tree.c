/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <sys/stat.h>
#endif

#include <misc/misc.h>
#include <reiser4/reiser4.h>

reiserfs_tree_t *reiserfs_tree_open(aal_device_t *device, 
    blk_t root_blk, reiserfs_key_t *root_key)
{
    reiserfs_tree_t *tree;
    reiserfs_coord_t coord;

    aal_assert("umka-128", device != NULL, return NULL);
    aal_assert("umka-737", root_key != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;

    if (!(tree->root_node = reiserfs_node_open(device, 
	    root_blk, REISERFS_GUESS_PLUGIN_ID)))
	goto error_free_tree;
    
    tree->root_key = root_key;
    
/*    if (!(tree->dir_plugin = libreiser4_factory_find_by_coord(REISERFS_DIR_PLUGIN, 
	dir_plugin_id)))
    {
    	libreiser4_factory_find_failed(REISERFS_DIR_PLUGIN, 
	    dir_plugin_id, goto error_free_tree);
    }
    
    if (!reiserfs_tree_lookup(tree, root_key, &coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find root directory.");
	goto error_free_tree;
    }
    
    if (!(tree->root_dir = libreiser4_plugin_call(goto error_free_tree, 
	tree->dir_plugin->dir, open, coord.node->block, coord.pos.item_pos)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open root directory.");
	goto error_free_tree;
    }*/
    
    return tree;

error_free_tree:
    aal_free(tree);
    return NULL;
}

reiserfs_node_t *reiserfs_tree_root_node(reiserfs_tree_t *tree) {
    aal_assert("umka-738", tree != NULL, return NULL);
    return tree->root_node;
}

#ifndef ENABLE_COMPACT

reiserfs_tree_t *reiserfs_tree_create(aal_device_t *device, 
    reiserfs_alloc_t *alloc, reiserfs_oid_t *oid, 
    reiserfs_plugin_id_t node_plugin_id, 
    reiserfs_plugin_id_t internal_plugin_id) 
{
    blk_t block_nr;
    reiserfs_tree_t *tree;
    reiserfs_unit_coord_t coord;
    reiserfs_node_t *squeeze, *leaf;
    
    reiserfs_item_info_t item_info;
    reiserfs_internal_info_t internal_info;

    aal_assert("umka-129", device != NULL, return NULL);
    aal_assert("umka-741", alloc != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;

    tree->root_key = reiserfs_oid_root_key(oid);

    /* Create a root node */
    if (!(block_nr = reiserfs_alloc_alloc(alloc))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
  
    if (!(squeeze = reiserfs_node_create(device, block_nr,
	node_plugin_id, REISERFS_LEAF_LEVEL + 1)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node at block %llu.", block_nr);
	goto error_free_tree;
    }
    tree->root_node = squeeze;

    if (!(block_nr = reiserfs_alloc_alloc(alloc))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for leaf node.");
	goto error_free_squeeze;
    }

    /* Initialize internal item. */
    aal_memset(&item_info, 0, sizeof(item_info));
    internal_info.blk = block_nr;
    item_info.info = &internal_info;
    
    if (!(item_info.plugin = libreiser4_factory_find_by_coord(REISERFS_ITEM_PLUGIN, 
	internal_plugin_id))) 
    {
    	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, internal_plugin_id,
	    goto error_free_squeeze);
    }
    
    coord.item_pos = 0;
    coord.unit_pos = -1;

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method. 
    */
    if (reiserfs_node_item_insert(squeeze, &coord, tree->root_key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_block_get_nr(squeeze->block));
	goto error_free_squeeze;
    }

    /* 
	Create the leaf. For awhile dir plugin will take block
	where it should place directory. But later it should be
	fixed for more smart behavior.
    */
    if (!(leaf = reiserfs_node_create(device, block_nr,
	node_plugin_id, REISERFS_LEAF_LEVEL)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a leaf node at %llu.", block_nr);
	goto error_free_leaf;
    }

    reiserfs_node_add(tree->root_node, leaf);
    return tree;

error_free_leaf:
    reiserfs_node_close(leaf);
error_free_squeeze:
    reiserfs_node_close(squeeze);
error_free_tree:
    aal_free(tree);
    return NULL;
}

/* 
    Syncs whole the tree-cache and removes all node except 
    root node from the cache.
*/
error_t reiserfs_tree_flush(reiserfs_tree_t *tree) {
    aal_assert("umka-573", tree != NULL, return -1);
    
    if (tree->root_node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, tree->root_node->children) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->item))
		return -1;
	}
	aal_list_free(tree->root_node->children);
	tree->root_node->children = NULL;
    }

    return 0;
}

/* Syncs whole the tree-cache */
error_t reiserfs_tree_sync(reiserfs_tree_t *tree) {
    aal_assert("umka-560", tree != NULL, return -1);
    return reiserfs_node_sync(tree->root_node);
}

#endif

void reiserfs_tree_close(reiserfs_tree_t *tree) {
    aal_assert("umka-134", tree != NULL, return);
    reiserfs_node_close(tree->root_node);
    aal_free(tree);
}

/*
    Makes search for specified node in the tree. Caches all
    nodes, search goes through.
*/
static int reiserfs_tree_node_lookup(reiserfs_tree_t *tree, 
    reiserfs_node_t *node, reiserfs_key_t *key, reiserfs_coord_t *coord) 
{
    int found = 0;
    uint8_t level;
    uint32_t pointer;

    aal_assert("umka-645", node != NULL, return 0);
    aal_assert("umka-742", tree != NULL, return 0);
    
    while (1) {
/*	if ((level = reiserfs_node_get_level(node)) > 
	    reiserfs_format_get_height(fs->format))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
		"Invalid node level. Found %d, expected less than %d.", 
		level, reiserfs_format_get_height(fs->format));
	    return 0;
	}*/
	
	if ((found = reiserfs_node_lookup(node, key, &coord->pos)) == -1)
	    return -1;
	
	if (level == REISERFS_LEAF_LEVEL)
	    return found;

	if (!(pointer = reiserfs_node_item_get_pointer(node, coord->pos.item_pos))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get pointer from item %u.", coord->pos.item_pos);
	    return 0;
	}
	
	if (!(coord->node = reiserfs_node_open(node->device, pointer, 
		REISERFS_GUESS_PLUGIN_ID))) 
	    return 0;
	
	reiserfs_node_add(node, coord->node);
	node = coord->node;
    }
    return 0;
}

/* 
    Makes search in the tree by specified key. Fills passed
    coord by coords of found item. 
*/
int reiserfs_tree_lookup(reiserfs_tree_t *tree, 
    reiserfs_key_t *key, reiserfs_coord_t *coord) 
{
    aal_assert("umka-642", tree != NULL, return 0);
    aal_assert("umka-643", key != NULL, return 0);
    aal_assert("umka-644", coord != NULL, return 0);
    
    return reiserfs_tree_node_lookup(tree, tree->root_node, 
	key, coord);
}

/*
    First makes search for correct place where new item should 
    be inserted. There may be two general cases:
    
    (1) If found node hasn't enought free space, then allocates 
    new  node and inserts item into new node and addes new node 
    into tree cache.
    
    (2) If found node has free space enought for new item, then 
    inserts item into found node.
    
    All insert operations shall be performed by calling node API 
    functions.
*/
error_t reiserfs_tree_item_insert(reiserfs_tree_t *tree, 
    reiserfs_key_t *key, reiserfs_item_info_t *item_info)
{
    return -1;
}

/* Removes item by specified key */
error_t reiserfs_tree_item_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

/* 
    First makes search for correct place where new node should be 
    inserted. This is an internal node. Then inserts new internal 
    item into found node and sets it up for correct pointing to new
    node. Addes node into corresponding node cache.

    FIXME-UMKA: I foresee some problems here concerned with difference
    beetwen internal item format in reiser3 and reiser4. Internal item in
    reiser3 is array of pointers whereas in reiser4 it contains just one 
    pointer. So in the first case we need performs "paste" operation
    in order to insert pointer to new node, whereas in second case we 
    should perform "insert" operation.
*/
error_t reiserfs_tree_node_insert(reiserfs_tree_t *tree, 
    reiserfs_node_t *node) 
{
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);
    
    return -1;
}

/* Removes node from tree by its left delimiting key */
error_t reiserfs_tree_node_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

