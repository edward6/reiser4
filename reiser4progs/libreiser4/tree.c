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
	    REISERFS_GUESS_PLUGIN_ID, root_key->plugin->h.id)))
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
	profile->node, profile->key, REISERFS_LEAF_LEVEL + 1)))
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
    Makes search in the tree by specified key. Fills passed
    coord by coords of found item. 
*/
int reiserfs_tree_lookup(reiserfs_tree_t *tree, uint8_t stop,
    reiserfs_key_t *key, reiserfs_coord_t *coord) 
{
    int lookup;
    uint32_t pointer;
    reiserfs_node_t *node;
    reiserfs_key_t internal_key;

    aal_assert("umka-645", node != NULL, return -1);
    aal_assert("umka-742", key != NULL, return -1);
    aal_assert("umka-742", coord != NULL, return -1);
   
    coord->pos.item = 0;
    coord->pos.unit = -1;
    coord->node = node = tree->root_node;

    reiserfs_key_init(&internal_key, key->plugin);
    
    while (1) {
	if (reiserfs_node_count(node) == 0)
	    return 0;
	
	/* 
	    Looking up for key inside node. Result of lookuping will be stored
	    in &coord->pos.
	*/
	if ((lookup = reiserfs_node_lookup(node, key, &coord->pos)) == -1)
	    return -1;

	/* Checking for level */
	if (reiserfs_node_get_level(node) == stop)
	    return lookup;
       	
	/* 
	    Key not found, and pos->item points to item placed right after needed 
	    internal item. So, we need to decrease position.
	*/
	if (lookup == 0)
	    coord->pos.item--;
	
	if (!(pointer = reiserfs_node_get_item_pointer(node, 
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
	aal_memcpy(internal_key.body, reiserfs_node_item_key(coord->node, 
	    coord->pos.item), sizeof(internal_key.body));
	
	if (!(coord->node = reiserfs_node_find(node, &internal_key))) {
	    if (!(coord->node = reiserfs_node_open(node->device, pointer, 
		    REISERFS_GUESS_PLUGIN_ID, node->key_plugin->h.id))) 
		return -1;
	}
	
	if (reiserfs_node_add(node, coord->node))
	    return -1;

	node = coord->node;
    }
    return 0;
}

/* 
    Inserts new item described by item hint into the tree. If found leaf doesn't
    has enought free space for new item then creates new node and inserts it into
    the tree by reiserfs_tree_node_insert function. See bellow for details.
*/
errno_t reiserfs_tree_insert_item(reiserfs_tree_t *tree, 
    reiserfs_item_hint_t *item)
{
    int lookup;
    reiserfs_key_t *key;
    reiserfs_coord_t coord;

    key = (reiserfs_key_t *)&item->key;

    /* Looking up for target leaf */
    if ((lookup = reiserfs_tree_lookup(tree, REISERFS_LEAF_LEVEL, 
	key, &coord)) == 1)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in tree.", 
	    reiserfs_key_get_locality(key), reiserfs_key_get_type(key),
	    reiserfs_key_get_objectid(key), reiserfs_key_get_offset(key));
	return -1;
    }
  
    if (lookup == -1)
	return -1;
 
    /* Estimating found node */
    if (reiserfs_node_item_estimate(coord.node, item, &coord.pos))
        return -1;
 
    /* 
	Checking whether we should create new leaf or we can insert item into 
	existent one. This will be performed in the case we have found an internal
	node, or we have not enought free space in found leaf.
    */
    if (reiserfs_node_get_level(coord.node) > REISERFS_LEAF_LEVEL ||
	reiserfs_node_get_free_space(coord.node) < item->length) 
    {
	blk_t block_nr;
	reiserfs_node_t *leaf;

	/* Allocating the block */
	if (!(block_nr = reiserfs_alloc_alloc(tree->alloc))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't allocate block for leaf node.");
	    return -1;
	}
	
	/* Creating new leaf */
	if (!(leaf = reiserfs_node_create(tree->device, block_nr,
	    reiserfs_node_get_plugin_id(tree->root_node), 
	    tree->root_node->key_plugin->h.id, REISERFS_LEAF_LEVEL)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create a leaf node at %llu.", block_nr);
	    return -1;
	}
	coord.pos.item = 0;
	coord.pos.unit = -1;
    
	/* Inserting item into new leaf */
	if (reiserfs_node_insert(leaf, &coord.pos, key, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert item into the node %llu.", 
		aal_block_get_nr(coord.node->block));

	    reiserfs_node_close(coord.node);
	    return -1;
	}
	
	/* Inserting new leaf into the tree */
	if (reiserfs_tree_insert_node(tree, coord.node->parent ? 
	    coord.node->parent : tree->root_node, leaf)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert node %llu into the thee.", 
		aal_block_get_nr(leaf->block));

	    reiserfs_node_close(leaf);
	    return -1;
	}
    } else {
	/* Inserting item into existent one leaf */
	if (reiserfs_node_insert(coord.node, &coord.pos, key, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an internal item into the node %llu.", 
		aal_block_get_nr(coord.node->block));
	    return -1;
	}
    }
    return 0;
}

/* Removes item by specified key */
errno_t reiserfs_tree_remove_item(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

/* Inserts a node into the tree */
errno_t reiserfs_tree_insert_node(reiserfs_tree_t *tree, 
    reiserfs_node_t *parent, reiserfs_node_t *node) 
{
    int lookup;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;

    reiserfs_item_hint_t item_hint;
    reiserfs_internal_hint_t internal_hint;
    
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-646", parent != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);

    if (reiserfs_node_ldkey(node, &ldkey))
	return -1;

    if ((lookup = reiserfs_node_lookup(parent, &ldkey, &coord.pos)) == -1)
	return -1;
    
    if (lookup == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in tree.", 
	    reiserfs_key_get_locality(&ldkey), reiserfs_key_get_type(&ldkey),
	    reiserfs_key_get_objectid(&ldkey), reiserfs_key_get_offset(&ldkey));
	return -1;
    }
    
    aal_memset(&item_hint, 0, sizeof(item_hint));
    internal_hint.pointer = aal_block_get_nr(node->block);

    item_hint.hint = &internal_hint;
    item_hint.type = REISERFS_INTERNAL_ITEM;
    
    /* FIXME-UMKA: Hardcoded internal item id */
    if (!(item_hint.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 0x3)))
    	libreiser4_factory_failed(return -1, find, item, 0x3);
   
    /* Estimating found internal node */
    if (reiserfs_node_item_estimate(parent, &item_hint, &coord.pos))
	return -1;
 
    /* 
	Checking for free space inside found internal node. If it is enought for
	inserting one more internal item into it then we doing this. If no, we need 
	to splitt internal node and insert item into one of new nodes in corresponding 
	to key. After, we should insert new node into parent of found internal node by
	reiserfs_tree_insert_node. This may cause tree growing when recursion reach the
	root node.
    */
    if (reiserfs_node_get_free_space(parent) < item_hint.length) {
	/* 
	    Splitting and calling reiserfs_tree_insert_node for new block will 
	    be here.
	*/
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, splitting is not suported yet!");
	return -1;
    }
    
    coord.pos.unit = -1;
    /* Inserting item */
    if (reiserfs_node_insert(parent, &coord.pos, &ldkey, &item_hint)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an internal item into the node %llu.", 
	    aal_block_get_nr(parent->block));
	return -1;
    }

    /* Adding node to the tree cache */
    reiserfs_node_add(parent, node);
    
    return 0;
}

/* Removes node from tree by its left delimiting key */
errno_t reiserfs_tree_remove_node(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

