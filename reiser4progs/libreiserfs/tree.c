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
#include <sys/stat.h>

error_t reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_blk;

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->format != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = reiserfs_format_get_root(fs))) {
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

/*
    FIXME-UMKA: Probably not the tree should concerning about creating 
    root directory. On my own it is directory object plugin/API job.
    Tree in its creation time should just create root node (squeeze).
    That is all. The leaf node root directory lies in must create 
    directory plugin by tree API (inserting items).
*/
error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    int size;
    blk_t block_nr;
    reiserfs_plugin_t *item_plugin;
    reiserfs_plugin_id_t item_plugin_id;
    reiserfs_key_t key;
    reiserfs_node_t squeeze, leaf;
    reiserfs_coord_t coord;
    reiserfs_item_info_t item_info;
    reiserfs_internal_info_t internal_info;
    reiserfs_stat_info_t stat_info;
    reiserfs_dir_info_t dir_info;

    /* 
	FIXME-VITALY: Directory object plugin should define what will be created in 
	the empty directory. Move it there when ready. 
    */
    reiserfs_entry_info_t entry[2] = {
	[0] = {
	    .parent_id = reiserfs_oid_root_self_locality(fs),
	    .object_id = reiserfs_oid_root(fs),
	    "."
	}, 
	[1] = {
	    .parent_id = reiserfs_oid_root_parent_locality(fs),
	    .object_id = reiserfs_oid_root_self_locality(fs),
	    ".." 
	}
    };

    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("vpf-115", profile != NULL, return -1);
    aal_assert("umka-130", fs->format != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;
    
    /* Create a root node */
    if (!(block_nr = reiserfs_alloc_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
    reiserfs_format_set_root(fs, block_nr);
  
    if (reiserfs_node_create(&squeeze, fs->host_device, block_nr, NULL, 
	profile->node, REISERFS_LEAF_LEVEL + 1)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node.");
	goto error_free_tree;
    }

    if (!(block_nr = reiserfs_alloc_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_close_squeese;
    }

    /* Initialize internal item. */
    internal_info.blk = &block_nr;
   
    reiserfs_key_init(&key);
    set_key_type(&key, KEY_SD_MINOR);
    set_key_locality(&key, reiserfs_oid_root_self_locality(fs));
    set_key_objectid(&key, reiserfs_oid_root(fs));

    reiserfs_init_item_info(&item_info);
    item_info.info = &internal_info;
    coord.node = &squeeze;
    coord.item_pos = 0;
    coord.unit_pos = -1;

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method. 
    */
    if (reiserfs_node_insert_item(&coord, &key, &item_info, 
	profile->item.internal)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(squeeze.device, squeeze.block));
	goto error_close_squeese;
    }

    /*
	We cannot just close the squeeze node here and create a leaf node in 
	the same object, because node plugin will probably want to update 
	the parent and we should provide it the whole path up to the root 
    */
    
    /* Create the leaf */
    if (reiserfs_node_create(&leaf, fs->host_device, block_nr, 
	&squeeze, profile->node, REISERFS_LEAF_LEVEL)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a leaf node.");
	goto error_close_leaf;
    } 

    /* Initialize stat_info */
    stat_info.mode = S_IFDIR | 0755;
    stat_info.extmask = 0;
    stat_info.nlink = 2;
    stat_info.size = 0;
   
    reiserfs_init_item_info(&item_info);
    item_info.info = &stat_info;
    coord.node = &leaf;

    /* Insert the stat data. */
    if (reiserfs_node_insert_item (&coord, &key, &item_info, profile->item.statdata)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(leaf.device, leaf.block));
	goto error_close_leaf;
    }
   
    /* 
	Initialize dir_entry.
	FIXME-UMKA: This should be in directory object plugin. It should 
	decide what is configuration of empty dir. Its possible that in the
	future will be directories like this
	
	.   points to current dir
	..  points to parent dir
	... points to parent of parent dir
    */
    dir_info.count = 2;
    dir_info.entry = entry;

    reiserfs_key_init(&key);
    set_key_type(&key, KEY_FILE_NAME_MINOR);
    set_key_locality(&key, reiserfs_oid_root(fs));

    /* FIXME-VITALY: this should go into directory object API. */
    build_entryid_by_entry_info((reiserfs_entryid_t *)key.el + 1, &entry[0]);

    reiserfs_init_item_info(&item_info);
    item_info.info = &dir_info;

    coord.item_pos = 1;
    coord.unit_pos = -1;

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method.
    */
    if (reiserfs_node_insert_item (&coord, &key, &item_info, 
	profile->item.direntry)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(squeeze.device, squeeze.block));
	goto error_close_leaf;
    }

    /*
	FIXME-UMKA: Created nodes should be added to tree-cache (reiserfs_tree_t) and 
	stored onto device during its synchronize call.
    */   
    reiserfs_node_sync(&squeeze);
    reiserfs_node_sync(&leaf);
    
    reiserfs_node_close(&squeeze);
    reiserfs_node_close(&leaf);

    return 0;

error_close_leaf:
    reiserfs_node_close(&leaf);
error_close_squeese:
    reiserfs_node_close(&leaf);
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

/*
    FIXME-UMKA: This method should synchronize tree cache onto device.
    Probably it should call "sync" method for root node and then root
    node should sync all childrens in recursively maner.
*/
error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-132", fs->tree != NULL, return -1);
    
    return 0;
}

#endif

/*
    FIXME-UMKA: This method should free tree-cache and all
    assosiated with it memory.
*/
void reiserfs_tree_close(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);

    aal_free(fs->tree);
    fs->tree = NULL;
}

/*
    FIXME-UMKA: This method should bring up all node on search 
    path into tree-cache and return pointer (something ala coords)
    to leaf node. Probably we should add some modification of this 
    method, which will make lookup for internal node (leaf_level - 1).
    It may be usefull for inserting leaves.
*/
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
	if (!(node = reiserfs_node_alloc())) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't allocate memory for a node.");
	    return -1; 
	}
	
	if (reiserfs_node_open(node, fs->host_device, from, NULL, 
	    REISERFS_GUESS_PLUGIN_ID)) 
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

	item.coord = &coord;
	if (reiserfs_item_open(&item)) {
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

