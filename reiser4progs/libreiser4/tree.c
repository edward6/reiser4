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

/* Requests block allocator for new blockand creates empty node on it */
static reiserfs_node_t *reiserfs_tree_alloc_node(reiserfs_tree_t *tree, 
    uint8_t level) 
{
    blk_t block_nr;
    
    aal_assert("umka-756", tree != NULL, return NULL);
    
    /* Allocating the block */
    if (!(block_nr = reiserfs_alloc_alloc(tree->fs->alloc))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't allocate block for a node.");
	return NULL;
    }

    return reiserfs_node_create(tree->fs->host_device, block_nr,
        reiserfs_node_get_plugin_id(tree->root), 
        tree->root->key_plugin->h.id, level);
}

reiserfs_tree_t *reiserfs_tree_open(reiserfs_fs_t *fs) {
    reiserfs_tree_t *tree;

    aal_assert("umka-737", fs != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;
    
    tree->fs = fs;
    
    if (!(tree->root = reiserfs_node_open(fs->host_device, 
	    reiserfs_format_get_root(fs->format), 
	    REISERFS_GUESS_PLUGIN_ID, fs->key.plugin->h.id)))
	goto error_free_tree;
    
    return tree;

error_free_tree:
    aal_free(tree);
    return NULL;
}

reiserfs_node_t *reiserfs_tree_root_node(reiserfs_tree_t *tree) {
    aal_assert("umka-738", tree != NULL, return NULL);
    return tree->root;
}

#ifndef ENABLE_COMPACT

reiserfs_tree_t *reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    blk_t block_nr;
    reiserfs_tree_t *tree;

    aal_assert("umka-741", fs != NULL, return NULL);
    aal_assert("umka-749", profile != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;

    tree->fs = fs;

    if (!(block_nr = reiserfs_alloc_alloc(fs->alloc))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't allocate block for the root node.");
	goto error_free_tree;
    }

    if (!(tree->root = reiserfs_node_create(fs->host_device, block_nr,
        profile->node, profile->key, reiserfs_format_get_height(fs->format))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create root node.");
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
    
    if (tree->root->cache) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, tree->root->cache) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->item))
		return -1;
	}
	aal_list_free(tree->root->cache);
	tree->root->cache = NULL;
    }

    return 0;
}

/* Syncs whole the tree-cache */
errno_t reiserfs_tree_sync(reiserfs_tree_t *tree) {
    aal_assert("umka-560", tree != NULL, return -1);
    return reiserfs_node_sync(tree->root);
}

#endif

void reiserfs_tree_close(reiserfs_tree_t *tree) {
    aal_assert("umka-134", tree != NULL, return);
    reiserfs_node_close(tree->root);
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
    blk_t block_nr;
    reiserfs_node_t *parent;
    reiserfs_key_t internal_key;

    aal_assert("umka-742", key != NULL, return -1);
    aal_assert("umka-742", coord != NULL, return -1);
  
    reiserfs_coord_init(coord, tree->root, 0, 0xffff);
    while (1) {
	/* 
	    Looking up for key inside node. Result of lookuping will be stored
	    in &coord->pos.
	*/
	if ((lookup = reiserfs_node_lookup(coord->node, key, &coord->pos)) == -1)
	    return -1;

	/* Checking for level */
	if (reiserfs_node_get_level(coord->node) == stop)
	    return lookup;
       	
	/* 
	    Key not found, and pos->item points to item placed right after needed 
	    internal item. So, we need to decrease position.
	*/
	if (lookup == 0) {
	    
	    /* Nothing found, probably node was empty */
	    if (coord->pos.item == 0)
		return lookup;
	    
	    coord->pos.item--;
	}
	
	/* Getting the node pointer from internal item */
	if (!(block_nr = reiserfs_node_get_pointer(coord->node, 
	    coord->pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get pointer from internal item %u, node %llu.", 
		coord->pos.item, aal_block_get_nr(coord->node->block));
	    return -1;
	}
	
	parent = coord->node;
	
	/* 
	    Check whether specified node already in cache. If so, we use node
	    from the cache.
	*/
	reiserfs_key_init(&internal_key, reiserfs_node_item_key(coord->node, 
	    coord->pos.item), key->plugin);

	if (!(coord->node = reiserfs_node_find(parent, &internal_key))) {

	    /* 
		Node was not found in the cache, we open it and register in the 
		cache.
	    */
	    if (!(coord->node = reiserfs_node_open(parent->block->device, 
		    block_nr, REISERFS_GUESS_PLUGIN_ID, parent->key_plugin->h.id))) 
		return -1;
	
	    /* Registering node in tree cache */
	    if (reiserfs_node_register(parent, coord->node))
		return -1;
	}
    }
    return 0;
}

/* Inserts a node into the tree */
static errno_t reiserfs_tree_insert_node(reiserfs_tree_t *tree, 
    reiserfs_node_t *parent, reiserfs_node_t *node)
{
    int lookup;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;

    reiserfs_item_hint_t item;
    reiserfs_internal_hint_t internal;
    
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-647", node != NULL, return -1);
    aal_assert("umka-796", parent != NULL, return -1);

    if (reiserfs_node_ldkey(node, &ldkey))
	return -1;
    
    coord.node = parent;
    
    if ((lookup = reiserfs_node_lookup(parent, &ldkey, &coord.pos)) == -1)
	return -1;
    
    if (lookup == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in block %llu.", 
	    reiserfs_key_get_locality(&ldkey), reiserfs_key_get_type(&ldkey),
	    reiserfs_key_get_objectid(&ldkey), reiserfs_key_get_offset(&ldkey),
	    aal_block_get_nr(parent->block));
	return -1;
    }
    
    aal_memset(&item, 0, sizeof(item));
    internal.pointer = aal_block_get_nr(node->block);

    item.hint = &internal;
    item.type = REISERFS_INTERNAL_ITEM;
    
    /* FIXME-UMKA: Hardcoded internal item id */
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 0x3)))
    	libreiser4_factory_failed(return -1, find, item, 0x3);
   
    /* Estimating found internal node */
    if (reiserfs_node_item_estimate(parent, &item, &coord.pos))
	return -1;
 
    /* 
	Checking for free space inside found internal node. If it is enought 
	for inserting one more internal item into it then we doing this. If no, 
	we need to split internal node and insert item into right of new nodes.
	After, we should insert right node into parent of found internal node 
	by reiserfs_tree_insert_node. This may cause tree growing when recursion 
	reach the root node.
    */
    if (reiserfs_node_get_free_space(parent) < item.length) {
	reiserfs_coord_t insert;
	
	/* Shift target node */
	aal_memset(&insert, 0, sizeof(insert));
	
	if (reiserfs_node_shift(&coord, &insert, item.length))
	    return -1;
		
	if (reiserfs_node_get_free_space(insert.node) < item.length) {
	    reiserfs_node_t *right;
	    
	    if (!(right = reiserfs_tree_alloc_node(tree, REISERFS_LEAF_LEVEL + 1))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't allocate new internal node.");
		return -1;
	    }

	    if (reiserfs_node_split(insert.node, right)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't split node %llu.", aal_block_get_nr(insert.node->block));
		return -1;
	    }

	    /* Checking for the root node. Here we increase the height of the tree */
	    if (!insert.node->parent) {

		/* Allocating new root node */
		if (!(tree->root = reiserfs_tree_alloc_node(tree, 
		    reiserfs_format_get_height(tree->fs->format)))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't allocate root node.");
		    return -1;
		}
		
		/* Registering insert point node in the new root node  */
		if (reiserfs_tree_insert_node(tree, tree->root, insert.node)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert node %llu into the tree.", 
			aal_block_get_nr(right->block));
		    return -1;
		}
		
		/* Updating the tree height and tree root values in disk format */
		reiserfs_format_set_height(tree->fs->format, 
		    reiserfs_node_get_level(tree->root));
		
		reiserfs_format_set_root(tree->fs->format, 
		    aal_block_get_nr(tree->root->block));
	    }
	   
	    /* Inserting right node into parent of insert point node */ 
	    if (reiserfs_tree_insert_node(tree, insert.node->parent, right)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert node %llu into the tree.", 
		    aal_block_get_nr(right->block));
		return -1;
	    }
	} else {
	    if (reiserfs_node_insert(parent, &coord.pos, &ldkey, &item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert an internal item into the node %llu.", 
		    aal_block_get_nr(parent->block));
		return -1;
	    }
	}
    } else {
    
	coord.pos.unit = 0xffff;

	/* Inserting item */
	if (reiserfs_node_insert(parent, &coord.pos, &ldkey, &item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an internal item into the node %llu.", 
		aal_block_get_nr(parent->block));
	    return -1;
	}
    }
    
    /* Adding node to the tree cache */
    reiserfs_node_register(parent, node);
    
    return 0;
}

/* 
    Inserts new item described by item hint into the tree. If found leaf doesn't
    has enought free space for new item then creates new node and inserts it into
    the tree by reiserfs_tree_node_insert function. See bellow for details.
*/
errno_t reiserfs_tree_insert(reiserfs_tree_t *tree, reiserfs_item_hint_t *item) {
    int lookup;
    reiserfs_key_t *key;
    reiserfs_coord_t coord;

    aal_assert("umka-779", tree != NULL, return -1);
    aal_assert("umka-779", item != NULL, return -1);
    
    key = (reiserfs_key_t *)&item->key;

    /* Looking up for target leaf */
    if ((lookup = reiserfs_tree_lookup(tree, REISERFS_LEAF_LEVEL + 
	(item->type == REISERFS_INTERNAL_ITEM), key, &coord)) == 1)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in block %llu.", 
	    reiserfs_key_get_locality(key), reiserfs_key_get_type(key),
	    reiserfs_key_get_objectid(key), reiserfs_key_get_offset(key),
	    aal_block_get_nr(coord.node->block));
	return -1;
    }
  
    if (lookup == -1)
	return -1;
 
    /* Estimating item in order to insert it into found node */
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

	if (reiserfs_node_get_level(coord.node) > REISERFS_LEAF_LEVEL) {
	    /* 
		Found node is the an internal node. This may happen when tree is 
		empty, that is consists of root node only. In this case we allocate 
		new leaf and insert it into tree.
	    */
	    if (!(leaf = reiserfs_tree_alloc_node(tree, REISERFS_LEAF_LEVEL))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't allocate a leaf node.");
		return -1;
	    }
	
	    coord.pos.item = 0;
	    coord.pos.unit = 0xffff;
    
	    /* Inserting item into new leaf */
	    if (reiserfs_node_insert(leaf, &coord.pos, key, item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert item into the node %llu.", 
		    aal_block_get_nr(coord.node->block));

		reiserfs_node_close(leaf);
		return -1;
	    }
	
	    /* Inserting new leaf into the tree */
	    if (reiserfs_tree_insert_node(tree, coord.node, leaf)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert node %llu into the thee.", 
		    aal_block_get_nr(leaf->block));

		reiserfs_node_close(leaf);
		return -1;
	    }
	} else {
	    reiserfs_coord_t insert;
	    
	    /* 
		This is the case when no enought free space in found node. Here we 
		should check whether free space in left or right neighborhoods is
		and if so, to make shift. Then we can insert new item into found
		node. In the case no free space found in both neightbors of found
		node, we should allocate new leaf and insert item into it.
	    */
	    aal_memset(&insert, 0, sizeof(insert));
	    
	    if (reiserfs_node_shift(&coord, &insert, item->length))
		return -1;    
	    
	    if (reiserfs_node_get_free_space(insert.node) < item->length) {
		/* 
		    Node was unable to shift own content into left or right neighbor.
		    Allocating the new leaf is needed.
		*/
		if (!(leaf = reiserfs_tree_alloc_node(tree, REISERFS_LEAF_LEVEL))) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't allocate a leaf node.");
		    return -1;
		}
	
		coord.pos.item = 0;
		coord.pos.unit = 0xffff;
    
		/* Inserting item into new leaf */
		if (reiserfs_node_insert(leaf, &coord.pos, key, item)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert item into the node %llu.", 
			aal_block_get_nr(coord.node->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	
		/* Inserting new leaf into the tree */
		if (reiserfs_tree_insert_node(tree, coord.node->parent, leaf)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert node %llu into the thee.", 
			aal_block_get_nr(leaf->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	    } else {
		    
		/* Inserting item into found after shifting point */
		if (reiserfs_node_insert(insert.node, &insert.pos, key, item)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert an internal item into the node %llu.", 
			aal_block_get_nr(coord.node->block));
		    return -1;
		}
	    }
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
errno_t reiserfs_tree_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key) 
{
    return -1;
}

