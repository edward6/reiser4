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
    reiserfs_alloc_t *alloc, blk_t root_blk, reiserfs_key_t *root_key) 
{
    reiserfs_tree_t *tree;
    reiserfs_coord_t coord;

    aal_assert("umka-128", device != NULL, return NULL);
    aal_assert("umka-737", alloc != NULL, return NULL);
    aal_assert("umka-748", root_key != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;
    
    tree->alloc = alloc;
    tree->device = device;
    tree->root_key = *root_key;
    
    if (!(tree->root_node = reiserfs_node_open(device, root_blk, 
	    REISERFS_GUESS_PLUGIN_ID)))
	goto error_free_tree;
    
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
    reiserfs_alloc_t *alloc, reiserfs_key_t *root_key, 
    reiserfs_profile_t *profile) 
{
    blk_t block_nr;
    reiserfs_tree_t *tree;

    aal_assert("umka-129", device != NULL, return NULL);
    aal_assert("umka-741", alloc != NULL, return NULL);
    aal_assert("umka-749", root_key != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;

    tree->root_key = *root_key;
    tree->alloc = alloc;
    tree->device = device;

    /* Create a root node */
    if (!(block_nr = reiserfs_alloc_alloc(alloc))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
  
    if (!(tree->root_node = reiserfs_node_create(device, block_nr,
	profile->node, REISERFS_LEAF_LEVEL + 1)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create a root node at block %llu.", block_nr);
	goto error_free_tree;
    }

    return tree;

error_free_tree:
    aal_free(tree);
    return NULL;
}

/* 
    Syncs whole the tree-cache and removes all nodes except 
    root node from the cache.
*/
errno_t reiserfs_tree_flush(reiserfs_tree_t *tree) {
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
errno_t reiserfs_tree_sync(reiserfs_tree_t *tree) {
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
    int lookup;
    uint32_t pointer;
    reiserfs_key_t internal_key;

    aal_assert("umka-645", node != NULL, return 0);
    aal_assert("umka-742", tree != NULL, return 0);
   
    coord->node = node;
    coord->pos.item = 0;
    coord->pos.unit = -1;

    reiserfs_key_init(&internal_key, key->plugin);
    
    while (1) {
	if (reiserfs_node_item_count(node) == 0)
	    return 0;

	if ((lookup = reiserfs_node_lookup(node, key, &coord->pos)) == -1)
	    return -1;

	if (reiserfs_node_get_level(node) == REISERFS_LEAF_LEVEL)
	    return lookup;
       	
	if (lookup == 0)
	    coord->pos.item--;
	
	if (!(pointer = reiserfs_node_item_get_pointer(node, 
	    coord->pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get pointer from internal item %u, node %llu.", 
		coord->pos.item, aal_block_get_nr(coord->node->block));
	    return -1;
	}
	
	/*
	    Checking whether node at "pointer" already exists 
	    in tree cache.
	*/
	aal_memcpy(internal_key.body, reiserfs_node_item_key_at(coord->node, 
	    coord->pos.item), sizeof(internal_key.body));
	
	if (!(coord->node = reiserfs_node_find(node, &internal_key))) {
	    if (!(coord->node = reiserfs_node_open(node->device, pointer, 
		    REISERFS_GUESS_PLUGIN_ID))) 
		return -1;
	}
	
	if (reiserfs_node_add(node, coord->node))
	    return -1;

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

errno_t reiserfs_tree_item_insert(reiserfs_tree_t *tree, 
    reiserfs_key_t *key, reiserfs_item_hint_t *hint)
{
    int lookup;
    reiserfs_coord_t coord;
    
    if ((lookup = reiserfs_tree_lookup(tree, key, &coord)) == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists.", 
	    reiserfs_key_get_locality(key), reiserfs_key_get_type(key),
	    reiserfs_key_get_objectid(key), reiserfs_key_get_offset(key));
	return -1;
    }
  
    if (lookup == -1)
	return -1;
  
    if (reiserfs_node_item_estimate(coord.node, hint, &coord.pos))
        return -1;
 
    if (reiserfs_node_get_level(coord.node) > REISERFS_LEAF_LEVEL ||
	reiserfs_node_get_free_space(coord.node) < hint->length) 
    {
	blk_t block_nr;
	reiserfs_node_t *leaf;

	if (!(block_nr = reiserfs_alloc_alloc(tree->alloc))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't allocate block for leaf node.");
	    return -1;
	}
	
	if (!(leaf = reiserfs_node_create(tree->device, block_nr,
	    reiserfs_node_get_plugin_id(tree->root_node), REISERFS_LEAF_LEVEL)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create a leaf node at %llu.", block_nr);
	    return -1;
	}
	coord.node = leaf;
	coord.pos.item = 0;
	coord.pos.unit = -1;
    
	if (reiserfs_node_item_insert(coord.node, &coord.pos, key, hint)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert item into the node %llu.", 
		aal_block_get_nr(coord.node->block));

	    reiserfs_node_close(coord.node);
	    return -1;
	}
	
	if (reiserfs_tree_node_insert(tree, coord.node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert node %llu into the thee.", 
		aal_block_get_nr(coord.node->block));

	    reiserfs_node_close(coord.node);
	    return -1;
	}
	
    } else {
	if (reiserfs_node_item_insert(coord.node, &coord.pos, key, hint)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an internal item into the node %llu.", 
		aal_block_get_nr(coord.node->block));
	    return -1;
	}
    }
    return 0;
}

/* Removes item by specified key */
errno_t reiserfs_tree_item_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

errno_t reiserfs_tree_node_insert(reiserfs_tree_t *tree, 
    reiserfs_node_t *node) 
{
    int lookup;
    reiserfs_key_t ld_key;
    reiserfs_coord_t coord;

    reiserfs_item_hint_t item_hint;
    reiserfs_internal_hint_t internal_hint;
    
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);

    /* Preparing left-delimiting key */
    reiserfs_key_init(&ld_key, tree->root_key.plugin);
    aal_memcpy(ld_key.body, reiserfs_node_item_key_at(node, 0), 
	sizeof(ld_key.body));
    
    if ((lookup = reiserfs_tree_lookup(tree, &ld_key, &coord)) == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists.", 
	    reiserfs_key_get_locality(&ld_key), reiserfs_key_get_type(&ld_key),
	    reiserfs_key_get_objectid(&ld_key), reiserfs_key_get_offset(&ld_key));
	return -1;
    }
  
    if (lookup == -1)
	return -1;

    aal_memset(&item_hint, 0, sizeof(item_hint));
    internal_hint.pointer = aal_block_get_nr(node->block);

    item_hint.hint = &internal_hint;
    item_hint.type = REISERFS_INTERNAL_ITEM;
    
    /* FIXME-UMKA: Hardcoded internal item id */
    if (!(item_hint.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 0x3)))
    	libreiser4_factory_failed(return -1, find, item, 0x3);
   
    if (reiserfs_node_item_estimate(coord.node, &item_hint, &coord.pos))
	return -1;
 
    if (reiserfs_node_get_free_space(coord.node) < item_hint.length) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, repacking is not supported yet!");
	return -1;
    }
    
    if (reiserfs_node_item_insert(coord.node, &coord.pos, &ld_key, &item_hint)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_block_get_nr(coord.node->block));
	return -1;
    }
    reiserfs_node_add(coord.node, node);
    
    return 0;
}

/* Removes node from tree by its left delimiting key */
errno_t reiserfs_tree_node_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

