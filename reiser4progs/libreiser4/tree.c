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

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->format != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = reiserfs_format_get_root(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid root block has been detected.");
	goto error_free_tree;
    }

    if (!(fs->tree->root = reiserfs_node_open(fs->host_device, 
	    root_blk, REISERFS_GUESS_PLUGIN_ID)))
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
    Tree in its creation time should create root node (squeeze) and leaf.
    Then dir object plugin should create directory, what leads to item 
    insertion.
    
    FIXME-VITALY: as dir object plugin does not exist for know this contains
    all item creation. To be changed when ready.
*/
error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    void *key;
    blk_t block_nr;
    reiserfs_coord_t coord;
    reiserfs_plugin_t *plugin;
    reiserfs_node_t *squeeze, *leaf;
    
    reiserfs_item_info_t item_info;
    reiserfs_internal_info_t internal_info;
    reiserfs_stat_info_t stat_info;
    reiserfs_direntry_info_t direntry_info;

    /* 
	FIXME-VITALY: Directory object plugin should define what will be 
	created in the empty directory. Move it there when ready. 
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
  
    if (!(squeeze = reiserfs_node_create(fs->host_device, block_nr,
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
    internal_info.blk = block_nr;
   
    if (!(plugin = libreiser4_plugins_find_by_coords(REISERFS_KEY_PLUGIN, 0x0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find key plugin by its id %x.", 0x0);
	goto error_free_squeeze;
    }
    
    if (!(key = libreiser4_plugins_call(goto error_free_squeeze, plugin->key, 
	create, KEY40_SD_MINOR, reiserfs_oid_root_parent_objectid(fs), 
	reiserfs_oid_root_objectid(fs), 0)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create stat-data key.");
	goto error_free_squeeze;
    }
    
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
    if (reiserfs_node_item_insert(squeeze, &coord, key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_block_get_nr(squeeze->block));
	goto error_free_squeeze;
    }

    /* Create the leaf */
    if (!(leaf = reiserfs_node_create(fs->host_device, block_nr,
	profile->node, REISERFS_LEAF_LEVEL)))
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
    if (reiserfs_node_item_insert(leaf, &coord, key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_block_get_nr(leaf->block));
	goto error_free_leaf;
    }
   
    direntry_info.count = 2;
    direntry_info.entry = entry;

    libreiser4_plugins_call(goto error_free_leaf, plugin->key, 
	clean, key);
    
    libreiser4_plugins_call(goto error_free_leaf, plugin->key, 
	set_type, key, KEY40_FILE_NAME_MINOR);
    
    libreiser4_plugins_call(goto error_free_leaf, plugin->key, 
	set_locality, key, reiserfs_oid_root_objectid(fs));
    
    /* 
	FIXME-VITALY: This should go into directory object API.
	FIXME-UMKA: We can't access key's elements directly, we
	know nothing about key structure here.
    */
    build_entryid_by_info((reiserfs_entryid_t *)(key + sizeof(uint64_t)), &entry[0]);

    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &direntry_info;

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
    if (reiserfs_node_item_insert(leaf, &coord, key, &item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert direntry item into the node %llu.", 
	    aal_block_get_nr(leaf->block));
	goto error_free_leaf;
    }

    libreiser4_plugins_call(goto error_free_leaf, plugin->key, 
	close, key);
    
    reiserfs_node_add_children(fs->tree->root, leaf);
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

void reiserfs_tree_close(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);
    
    reiserfs_node_close(fs->tree->root);
    aal_free(fs->tree);
}

/*
    Makes search for specified node in the tree. Caches all
    nodes, search goes through.
*/
static int reiserfs_tree_node_lookup(reiserfs_fs_t *fs, reiserfs_node_t *node, 
    void *key, reiserfs_coord_t *coord) 
{
    uint8_t level;
    int found = 0;
    reiserfs_node_t *children;

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
	
	if ((found = reiserfs_node_lookup(node, key, coord)) == -1)
	    return -1;
	
	coord->node = node->block;
	
	if (level == REISERFS_LEAF_LEVEL)
	    return found;
			
	if (!(children = reiserfs_node_open(fs->host_device, 
		reiserfs_node_item_get_pointer(node, coord->item_pos), 
		REISERFS_GUESS_PLUGIN_ID))) 
	    return 0;
	
	reiserfs_node_add_children(node, children);
	node = children;
    }
	
    return 0;
}

/* 
    Makes search in the tree by specified key. Fills passed
    coord by coords of found item. 
*/
int reiserfs_tree_lookup(reiserfs_fs_t *fs, void *key, 
    reiserfs_coord_t *coord) 
{
    aal_assert("umka-642", fs != NULL, return 0);
    aal_assert("umka-643", key != NULL, return 0);
    aal_assert("umka-644", coord != NULL, return 0);
    
    if (reiserfs_format_get_height(fs) < 2)
	return 0;

    return reiserfs_tree_node_lookup(fs, fs->tree->root, 
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
error_t reiserfs_tree_item_insert(reiserfs_fs_t *fs, void *key, 
    reiserfs_item_info_t *item_info)
{
    return -1;
}

/* Removes item by specified key */
error_t reiserfs_tree_item_remove(reiserfs_fs_t *fs, void *key) {
    return -1;
}

/* 
    First makes search for correct place where new node should be 
    inserted. This is an internal node. Then inserts new internal 
    item into found node and sets it up for correct pointing to new
    node. Addes node into corresponding node cache.

    FIXME-UMKA: I foresee some problems here concerned with difference
    beetwen internal items in reiser3 and reiser4. Internal item in
    reiser3 is array of pointers whereas in reiser4 it contains of one 
    pointer. So in the first case we need performs "paste" operation
    in order to insert pointer to new node, whereas in second case we 
    should perform "insert" operation.
*/
error_t reiserfs_tree_node_insert(reiserfs_fs_t *fs, reiserfs_node_t *node) {
    void *key;
    reiserfs_coord_t coord;
    
    aal_assert("umka-646", fs != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);
    
    /* Getting left delimiting key */
    key = reiserfs_node_item_key_at(node, 0);
    
    if (reiserfs_tree_lookup(fs, key, &coord) == -1)
	return -1;
    
    return -1;
}

/* Removes node from tree by its left delimiting key */
error_t reiserfs_tree_node_remove(reiserfs_fs_t *fs, void *key) {
    return -1;
}

