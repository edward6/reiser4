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

error_t reiserfs_tree_init(reiserfs_fs_t *fs) {
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

    if (!(fs->tree->root = reiserfs_node_init(fs->host_device, 
	    root_blk, NULL, REISERFS_GUESS_PLUGIN_ID)))
	goto error_free_tree;
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

/*
    Tree in its creation time should just create root node (squeeze).
    That is all. The leaf node, root directory lies in must create 
    directory plugin by tree API (inserting items).
*/
error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    int size;
    blk_t block_nr;
    reiserfs_key_t key;
    reiserfs_item_coord_t coord;
    reiserfs_node_t *squeeze, *leaf;
    
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
	    .parent_id = reiserfs_oid_root_parent_objectid(fs),
	    .object_id = reiserfs_oid_root_objectid(fs),
	    "."
	}, 
	[1] = {
	    .parent_id = reiserfs_oid_root_parent_locality(fs),
	    .object_id = reiserfs_oid_root_parent_objectid(fs),
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
  
    if (!(squeeze = reiserfs_node_create(fs->host_device, block_nr, NULL, 
	profile->node, REISERFS_LEAF_LEVEL + 1)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node at block %llu.", block_nr);
	goto error_free_tree;
    }
    fs->tree->root = squeeze;

    if (!(block_nr = reiserfs_alloc_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for leaf node.");
	goto error_free_squeeze;
    }

    /* Initialize internal item. */
    internal_info.blk = &block_nr;
   
    reiserfs_key_init(&key);
    set_key_type(&key, KEY_SD_MINOR);
    set_key_locality(&key, reiserfs_oid_root_parent_objectid(fs));
    set_key_objectid(&key, reiserfs_oid_root_objectid(fs));

    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &internal_info;
    
    if (!(item_info.plugin = libreiser4_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, 
	profile->item.internal))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find internal item plugin by its id %x.", 
	    profile->item.internal);
	goto error_free_squeeze;
    }
    
    coord.item_pos = 0;
    coord.unit_pos = -1;

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method. 
    */
    if (reiserfs_node_insert_item(squeeze, &coord, &key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(squeeze->device, squeeze->block));
	goto error_free_squeeze;
    }

    /* Create the leaf */
    if (!(leaf = reiserfs_node_create(fs->host_device, block_nr,
	squeeze, profile->node, REISERFS_LEAF_LEVEL)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a leaf node at %llu.", block_nr);
	goto error_free_leaf;
    } 

    /* Initialize stat_info */
    stat_info.mode = S_IFDIR | 0755;
    stat_info.extmask = 0;
    stat_info.nlink = 2;
    stat_info.size = 0;
   
    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &stat_info;
    
    if (!(item_info.plugin = libreiser4_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, 
	profile->item.statdata))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find stat data item plugin by its id %x.", 
	    profile->item.statdata);
	goto error_free_leaf;
    }

    /* Insert the stat data. */
    if (reiserfs_node_insert_item(leaf, &coord, &key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_device_get_block_nr(leaf->device, leaf->block));
	goto error_free_leaf;
    }
   
    /* 
	Initializing direntry.
	
	FIXME-UMKA: This should be in directory object plugin. It should 
	decide what is configuration of empty dir. It's possible that in the
	future will be empty directories with more than two entries.
    */
    dir_info.count = 2;
    dir_info.entry = entry;

    reiserfs_key_init(&key);
    set_key_type(&key, KEY_FILE_NAME_MINOR);
    set_key_locality(&key, reiserfs_oid_root_objectid(fs));

    /* FIXME-VITALY: This should go into directory object API. */
    build_entryid_by_entry_info((reiserfs_entryid_t *)key.el + 1, &entry[0]);

    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &dir_info;

    coord.item_pos = 1;
    coord.unit_pos = -1;

    if (!(item_info.plugin = libreiser4_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, 
	profile->item.direntry))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find direntry item plugin by its id %x.", 
	    profile->item.direntry);
	goto error_free_leaf;
    }

    /* 
	Insert an internal item. Item will be created automatically from 
	the node insert API method.
    */
    if (reiserfs_node_insert_item(leaf, &coord, &key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert direntry item into the node %llu.", 
	    aal_device_get_block_nr(leaf->device, leaf->block));
	goto error_free_leaf;
    }

    reiserfs_node_add_children(fs->tree->root, leaf);
    
    return 0;

error_free_leaf:
    reiserfs_node_fini(leaf);
error_free_squeeze:
    reiserfs_node_fini(squeeze);
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
    reiserfs_node_t *root;
    
    aal_assert("umka-572", fs != NULL, return -1);
    aal_assert("umka-573", fs->tree != NULL, return -1);
    aal_assert("umka-574", fs->tree->root != NULL, return -1);
    
    root = fs->tree->root;
    if (root->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, root->children) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->data))
		return -1;
	}
	aal_list_free(root->children);
	root->children = NULL;
    }

    return 0;
}

/* Syncs whole the tree-cache */
error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-560", fs->tree != NULL, return -1);
    aal_assert("umka-576", fs->tree->root != NULL, return -1);

    return reiserfs_node_sync(fs->tree->root);
}

#endif

void reiserfs_tree_fini(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);
    
    reiserfs_node_fini(fs->tree->root);
    aal_free(fs->tree);
}

