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

error_t reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_blk;
    reiserfs_key_t key;
    reiserfs_coord_t coord;
    reiserfs_plugin_t *key_plugin;

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->format != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = reiserfs_format_get_root(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid root block has been detected.");
	goto error_free_tree;
    }

    if (!(fs->tree->root_node = reiserfs_node_open(fs->host_device, 
	    root_blk, REISERFS_GUESS_PLUGIN_ID)))
	goto error_free_tree;
    
    /* All the stuff bellow will be in dir API when ready */
    
    /* FIXME-UMKA: Here should be not hardcoded dir plugin id */
    if (!(fs->tree->dir_plugin = libreiser4_factory_find_by_coord(REISERFS_DIR_PLUGIN, 0x0)))
    	libreiser4_factory_find_failed(REISERFS_DIR_PLUGIN, 0x0, goto error_free_tree);

    /* FIXME-UMKA: Here should be not hardcoded key plugin id */
    if (!(key_plugin = libreiser4_factory_find_by_coord(REISERFS_KEY_PLUGIN, 0x0)))
    	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, 0x0, goto error_free_tree);
    
    /* 
	Finding the root directory stat data.
	FIXME-UMKA: Here should be not key40 stat data type, but
	key format independent stat data type.
    */
    reiserfs_key_init(&key, key_plugin);
    reiserfs_key_clean(&key);
    reiserfs_key_build_file_key(&key, KEY40_STATDATA_MINOR, 
	reiserfs_oid_root_parent_objectid(fs), reiserfs_oid_root_objectid(fs), 0);
    
    if (!reiserfs_tree_lookup(fs, &key, &coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find root directory stat data.");
	goto error_free_tree;
    }
    
    /* Creating root directory */
    if (!(fs->tree->root_dir = libreiser4_plugin_call(goto error_free_tree, 
	fs->tree->dir_plugin->dir, open, coord.node->block, coord.pos.item_pos)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open root directory.");
	goto error_free_tree;
    }
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    blk_t block_nr;
    reiserfs_key_t key;
    reiserfs_unit_coord_t coord;
    reiserfs_node_t *squeeze, *leaf;
    reiserfs_plugin_t *key_plugin, *dir_plugin;
    
    reiserfs_item_info_t item_info;
    reiserfs_internal_info_t internal_info;

    reiserfs_opaque_t *root_dir;

    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("vpf-115", profile != NULL, return -1);
    aal_assert("umka-130", fs->format != NULL, return -1);

    if (!(key_plugin = libreiser4_factory_find_by_coord(REISERFS_KEY_PLUGIN, profile->key))) 
    	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, profile->key, return -1);
    
    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    /* Create a root node */
    if (!(block_nr = reiserfs_alloc_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
    reiserfs_format_set_root(fs, block_nr);
  
    if (!(squeeze = reiserfs_node_create(fs->host_device, block_nr,
	profile->node, REISERFS_LEAF_LEVEL + 1)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node at block %llu.", block_nr);
	goto error_free_tree;
    }
    fs->tree->root_node = squeeze;

    if (!(block_nr = reiserfs_alloc_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for leaf node.");
	goto error_free_squeeze;
    }

    /* Initialize internal item. */
    internal_info.blk = block_nr;
    
    reiserfs_key_init(&key, key_plugin);
    reiserfs_key_clean(&key);

    reiserfs_key_build_file_key(&key, KEY40_STATDATA_MINOR, reiserfs_oid_root_parent_objectid(fs),
	reiserfs_oid_root_objectid(fs), 0);
    
    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &internal_info;
    
    if (!(item_info.plugin = libreiser4_factory_find_by_coord(REISERFS_ITEM_PLUGIN, 
	profile->item.internal))) 
    {
    	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, profile->item.internal,
	    goto error_free_squeeze);
    }
    
    coord.item_pos = 0;
    coord.unit_pos = -1;

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method. 
    */
    if (reiserfs_node_item_insert(squeeze, &coord, &key, &item_info)) {
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
    if (!(leaf = reiserfs_node_create(fs->host_device, block_nr,
	profile->node, REISERFS_LEAF_LEVEL)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a leaf node at %llu.", block_nr);
	goto error_free_leaf;
    }

    if (!(dir_plugin = libreiser4_factory_find_by_coord(REISERFS_DIR_PLUGIN, 
	profile->dir)))
    {
    	libreiser4_factory_find_failed(REISERFS_DIR_PLUGIN, profile->dir,
	    goto error_free_leaf);
    }

    /* Creating root directory */    
    if (!(fs->tree->root_dir = libreiser4_plugin_call(goto error_free_leaf, 
	dir_plugin->dir, create, leaf->block, 0, profile->key, profile->item.statdata, 
	profile->item.direntry, profile->oid, profile->node)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create root directory.");
	goto error_free_leaf;
    }
    fs->tree->dir_plugin = dir_plugin;
    
    reiserfs_node_add_children(fs->tree->root_node, leaf);
    return 0;

error_free_leaf:
    reiserfs_node_close(leaf);
error_free_squeeze:
    reiserfs_node_close(squeeze);
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

/* 
    Syncs whole the tree-cache and removes all node except 
    root node from the cache.
*/
error_t reiserfs_tree_flush(reiserfs_fs_t *fs) {
    aal_assert("umka-572", fs != NULL, return -1);
    aal_assert("umka-573", fs->tree != NULL, return -1);
    
    if (fs->tree->root_node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, fs->tree->root_node->children) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->data))
		return -1;
	}
	aal_list_free(fs->tree->root_node->children);
	fs->tree->root_node->children = NULL;
    }

    return 0;
}

/* Syncs whole the tree-cache */
error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-560", fs->tree != NULL, return -1);

    return reiserfs_node_sync(fs->tree->root_node);
}

#endif

void reiserfs_tree_close(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);

    if (fs->tree->root_dir && fs->tree->dir_plugin) {
	/* Here will be calling dir API method */    
	libreiser4_plugin_call(goto error_free_root_node, fs->tree->dir_plugin->dir, 
	    close, fs->tree->root_dir);
    }

error_free_root_node:    
    reiserfs_node_close(fs->tree->root_node);
    aal_free(fs->tree);
}

/*
    Makes search for specified node in the tree. Caches all
    nodes, search goes through.
*/
static int reiserfs_tree_node_lookup(reiserfs_fs_t *fs, reiserfs_node_t *node, 
    reiserfs_key_t *key, reiserfs_coord_t *coord) 
{
    int found = 0;
    uint8_t level;
    uint32_t pointer;

    aal_assert("umka-645", node != NULL, return 0);
    
    while (1) {
	if ((level = reiserfs_node_get_level(node)) > 
	    reiserfs_format_get_height(fs))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
		"Invalid node level. Found %d, expected less than %d.", 
		level, reiserfs_format_get_height(fs));
	    return 0;
	}
	
	if ((found = reiserfs_node_lookup(node, key, &coord->pos)) == -1)
	    return -1;
	
	if (level == REISERFS_LEAF_LEVEL)
	    return found;

	if (!(pointer = reiserfs_node_item_get_pointer(node, coord->pos.item_pos))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get pointer from item %u.", coord->pos.item_pos);
	    return 0;
	}
	
	if (!(coord->node = reiserfs_node_open(fs->host_device, pointer, 
		REISERFS_GUESS_PLUGIN_ID))) 
	    return 0;
	
	reiserfs_node_add_children(node, coord->node);
	node = coord->node;
    }
	
    return 0;
}

/* 
    Makes search in the tree by specified key. Fills passed
    coord by coords of found item. 
*/
int reiserfs_tree_lookup(reiserfs_fs_t *fs, reiserfs_key_t *key, 
    reiserfs_coord_t *coord) 
{
    aal_assert("umka-642", fs != NULL, return 0);
    aal_assert("umka-643", key != NULL, return 0);
    aal_assert("umka-644", coord != NULL, return 0);
    
    if (reiserfs_format_get_height(fs) < 2)
	return 0;

    return reiserfs_tree_node_lookup(fs, fs->tree->root_node, 
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
error_t reiserfs_tree_item_insert(reiserfs_fs_t *fs, reiserfs_key_t *key, 
    reiserfs_item_info_t *item_info)
{
    return -1;
}

/* Removes item by specified key */
error_t reiserfs_tree_item_remove(reiserfs_fs_t *fs, reiserfs_key_t *key) {
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
error_t reiserfs_tree_node_insert(reiserfs_fs_t *fs, reiserfs_node_t *node) {
    reiserfs_key_t key;
    reiserfs_coord_t coord;
    
    aal_assert("umka-646", fs != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);
    
    return -1;
}

/* Removes node from tree by its left delimiting key */
error_t reiserfs_tree_node_remove(reiserfs_fs_t *fs, reiserfs_key_t *key) {
    return -1;
}

