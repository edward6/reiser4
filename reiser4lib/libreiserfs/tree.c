/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <misc/misc.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/hack.h>

error_t reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_blk;

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = reiserfs_super_get_root(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid root block has been detected.");
	goto error_free_tree;
    }

/*    if (reiserfs_fs_format_plugin_id(fs) == 0x2) {
	if (!(fs->tree->root = reiserfs_node_open(fs->device, root_blk, 0x2)))
	    goto error_free_tree;
    } else {	
	if (!(fs->tree->root = reiserfs_node_open(fs->device, root_blk, REISERFS_GUESS_PLUGIN_ID)))
	    goto error_free_tree;
    }*/
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_tree_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t node_plugin_id) {
    blk_t root_blk;
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_id_t plugin_id;
    
    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("umka-130", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = hack_create_tree(fs, node_plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create b*tree.");
	goto error_free_tree;
    }	

/*    if (!(root_blk = reiserfs_alloc_find(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
    
    reiserfs_alloc_use(fs, root_blk);*/
    reiserfs_super_set_root(fs, root_blk);
    
    /* Here will be also allocated leaf node */
    
/*    if (!(fs->tree->root = reiserfs_node_create(fs->device, root_blk, 
	    node_plugin_id, REISERFS_ROOT_LEVEL))) 
	goto error_free_tree;*/
    

    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

error_t reiserfs_tree_create_2(reiserfs_fs_t *fs, reiserfs_default_plugin_t *default_plugins) {
    blk_t block_n;
    reiserfs_plugin_t *item_plugin;
    reiserfs_plugin_id_t item_plugin_id;
    reiserfs_key_t key;
    reiserfs_node_t *node;
    reiserfs_coord_t coord;
    reiserfs_item_info_t item;
    reiserfs_internal_item_info_t internal_info;

    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("umka-130", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;
    
    /* Create a root node */
    if (!(block_n = reiserfs_alloc_find(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
    
    reiserfs_alloc_use(fs, block_n);
    reiserfs_super_set_root(fs, block_n);
  
    if (!(node = reiserfs_node_create(fs->device, block_n, NULL, default_plugins->node, 
	REISERFS_LEAF_LEVEL + 1))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node.");
	goto error_free_tree;
    }
    
    if (!(block_n = reiserfs_alloc_find(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_node;
    }

    reiserfs_alloc_use(fs, block_n);

    internal_info.block = &block_n;
    
    /* Insert an internal item */
    set_key_type(&key, KEY_SD_MINOR);
    set_key_locality(&key, REISERFS_RESERVED_IDS + 1);
    set_key_objectid(&key, REISERFS_RESERVED_IDS + 2);

    item.info = &internal_info;

    coord.node = node;
    coord.item_pos = 0;
    coord.unit_pos = -1;

    if (reiserfs_item_estimate (&coord, &item, default_plugins->item.internal)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't prepare info about internal item to be inserted.");
	goto error_free_node;
    }
    
    /* Item create method will be called from the node create method */
    if (reiserfs_node_insert_item (&coord, &key, &item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(node->device, node->block));
	goto error_free_node;
    }

    /* free node, block, etc. about squeeze node */
    reiserfs_node_close (node);
    
    if (!(node = reiserfs_node_create (fs->device, block_n, node, default_plugins->node, 
	REISERFS_LEAF_LEVEL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a leaf node.");
	goto error_free_tree;
    } 
/*
    if (!reiserfs_direntry_create ()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create the root directory.");
	goto error_free_node;
    }
*/

    return 0;

error_free_node:
    reiserfs_node_close(node);
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}


error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-132", fs->tree != NULL, return -1);
    
    return 0/*reiserfs_node_sync(fs->tree->root)*/;
}

#endif

void reiserfs_tree_close(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);

//    reiserfs_node_close(fs->tree->root);
    
    aal_free(fs->tree);
    fs->tree = NULL;
}

int reiserfs_tree_lookup(reiserfs_fs_t *fs, blk_t from, 
    reiserfs_comp_func_t comp_func, reiserfs_key_t *key, 
    reiserfs_path_t *path) 
{
    int found = 0;
    reiserfs_node_t *node;
    reiserfs_coord_t coord;
    reiserfs_plugin_t *plugin;
    reiserfs_item_t item;
	
    aal_assert("umka-458", fs != NULL, return 0);
    aal_assert("umka-459", key != NULL, return 0);
    aal_assert("umka-473", comp_func != NULL, return 0);
	
    if (path)
	reiserfs_path_clear(path);
    
    while (1) {
	if (!(node = reiserfs_node_open(fs->device, from, NULL, REISERFS_GUESS_PLUGIN_ID))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't open node %llu.", from);
	    return 0;
	}
	
	coord.node = node;

	if (reiserfs_node_lookup(&coord, key) == -1)
	    return 0;

	if (path && !reiserfs_path_append(path, &coord))
	    return 0;
	
	if (reiserfs_item_init(&item, &coord)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Item %d in node %llu initialization failed.", coord.item_pos, from);
	    return 0;
	}

	if (!reiserfs_item_is_internal(&item))
	    return 1;
	
	if (!(from = reiserfs_item_down_link (&item))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get node in block %llu.", from);
	    return 0;
	}
    }
    return 0;
}

