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
        reiserfs_node_get_plugin_id(tree->cache->node), 
        tree->cache->node->key_plugin->h.id, level);
}

reiserfs_tree_t *reiserfs_tree_open(reiserfs_fs_t *fs) {
    reiserfs_tree_t *tree;
    reiserfs_node_t *node;

    aal_assert("umka-737", fs != NULL, return NULL);

    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;
    
    tree->fs = fs;
    
    if (!(node = reiserfs_node_open(fs->host_device, 
	    reiserfs_format_get_root(fs->format), 
	    REISERFS_GUESS_PLUGIN_ID, fs->key.plugin->h.id)))
	goto error_free_tree;
    
    if (!(tree->cache = reiserfs_cache_create(node)))
	goto error_free_node;
    
    return tree;

error_free_node:
    reiserfs_node_close(node);
error_free_tree:
    aal_free(tree);
    return NULL;
}

reiserfs_cache_t *reiserfs_tree_root(reiserfs_tree_t *tree) {
    aal_assert("umka-738", tree != NULL, return NULL);
    return tree->cache;
}

#ifndef ENABLE_COMPACT

reiserfs_tree_t *reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile) 
{
    blk_t block_nr;
    reiserfs_node_t *node;
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

    if (!(node = reiserfs_node_create(fs->host_device, block_nr,
        profile->node, profile->key, reiserfs_format_get_height(fs->format))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create root node.");
	goto error_free_tree;
    }

    if (!(tree->cache = reiserfs_cache_create(node)))
	goto error_free_node;
    
    return tree;

error_free_node:
    reiserfs_node_close(node);
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
    
    reiserfs_cache_sync(tree->cache);
    
    if (tree->cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, tree->cache->list)
	    reiserfs_cache_close((reiserfs_cache_t *)walk->item);
	
	tree->cache->list = NULL;
    }

    return 0;
}

/* Syncs whole the tree-cache */
errno_t reiserfs_tree_sync(reiserfs_tree_t *tree) {
    aal_assert("umka-560", tree != NULL, return -1);
    
    return reiserfs_cache_sync(tree->cache);
}

#endif

void reiserfs_tree_close(reiserfs_tree_t *tree) {
    aal_assert("umka-134", tree != NULL, return);
    
    reiserfs_cache_close(tree->cache);
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
    reiserfs_key_t ikey;
    reiserfs_cache_t *parent;

    aal_assert("umka-742", key != NULL, return -1);
    aal_assert("umka-742", coord != NULL, return -1);
  
    reiserfs_coord_init(coord, tree->cache, 0, 0xffff);
    while (1) {
	/* 
	    Looking up for key inside node. Result of lookuping will be stored
	    in &coord->pos.
	*/
	if ((lookup = reiserfs_node_lookup(coord->cache->node, 
		key, &coord->pos)) == -1)
	    return -1;

	/* Checking for level */
	if (reiserfs_node_get_level(coord->cache->node) == stop)
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
	if (!(block_nr = reiserfs_node_get_pointer(coord->cache->node, 
	    coord->pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get pointer from internal item %u, node %llu.", 
		coord->pos.item, aal_block_get_nr(coord->cache->node->block));
	    return -1;
	}
	
	parent = coord->cache;
	
	/* 
	    Check whether specified node already in cache. If so, we use node
	    from the cache.
	*/
	reiserfs_key_init(&ikey, reiserfs_node_item_key(coord->cache->node, 
	    coord->pos.item), key->plugin);

	if (!(coord->cache = reiserfs_cache_find(parent, &ikey))) {
	    reiserfs_node_t *node;
	    /* 
		Node was not found in the cache, we open it and register in the 
		cache.
	    */
	    if (!(node = reiserfs_node_open(parent->node->block->device, 
		    block_nr, REISERFS_GUESS_PLUGIN_ID, parent->node->key_plugin->h.id))) 
		return -1;
	    
	    if (!(coord->cache = reiserfs_cache_create(node))) {
		reiserfs_node_close(node);
		return -1;
	    }
	    
	    /* Registering node in tree cache */
	    if (reiserfs_cache_register(parent, coord->cache)) {
		reiserfs_cache_close(coord->cache);
		return -1;
	    }
	}
    }
    
    return 0;
}

#ifndef ENABLE_COMPACT

static errno_t reiserfs_tree_relocate(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src, int remove) 
{
    int res;
    reiserfs_id_t plugin_id;
    reiserfs_item_hint_t item;
    
    reiserfs_node_t *src_node;
    reiserfs_node_t *dst_node;
    
    aal_memset(&item, 0, sizeof(item));
    
    dst_node = dst->cache->node;
    src_node = src->cache->node;
    
    item.data = reiserfs_node_item_body(src_node, src->pos.item);
    item.length = reiserfs_node_item_length(src_node, src->pos.item);
    
    /* Getting the key of item that going to be copied */
    reiserfs_key_init((reiserfs_key_t *)&item.key, 
	reiserfs_node_item_key(src_node, src->pos.item), 
	src_node->key_plugin);
	
    plugin_id = reiserfs_node_get_item_plugin_id(src_node, src->pos.item);
	
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, plugin_id)))
	libreiser4_factory_failed(return -1, find, item, plugin_id);

    /* FIXME-UMKA: Here should be copied also enties of cache */
    
    /* Insering item into new location */
    if ((res = reiserfs_node_insert(dst_node, &dst->pos,
	    (reiserfs_key_t *)&item.key, &item)))
	return res;
    
    /* Remove src item if remove flag is turned on */
    if (remove)
	res = reiserfs_node_remove(src_node, &src->pos);
    
    return res;
}

errno_t reiserfs_tree_move(reiserfs_coord_t *dst, reiserfs_coord_t *src) {
    return reiserfs_tree_relocate(dst, src, 1);
}

errno_t reiserfs_tree_copy(reiserfs_coord_t *dst, reiserfs_coord_t *src) {
    return reiserfs_tree_relocate(dst, src, 0);
}

#endif

static reiserfs_cache_t *reiserfs_tree_neighbour(reiserfs_cache_t *cache, 
    int direction) 
{
    blk_t block_nr;
    int res, item_pos;
    reiserfs_pos_t pos;
    reiserfs_key_t ldkey;
    
    reiserfs_node_t *node;
    reiserfs_node_t *parent;
    
    aal_assert("umka-776", cache != NULL, return NULL);
  
    if (!(parent = cache->parent->node))
	return NULL;
    
    node = cache->node;
    
    reiserfs_node_ldkey(node, &ldkey);
    if ((res = reiserfs_node_lookup(parent, &ldkey, &pos)) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find left delimiting key of node %llu.", 
	    aal_block_get_nr(node->block));
	return NULL;
    }

    item_pos = pos.item + (direction == LEFT ? -1 : 1);

    if (item_pos < 0 || item_pos > reiserfs_node_count(parent) - 1)
	return NULL;
	    
    if (!(block_nr = reiserfs_node_get_pointer(parent, item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get pointer to %s neighbour.",
	    (direction == LEFT ? "left" : "right"));
	return NULL;
    }

    if (!(node = reiserfs_node_open(node->block->device, block_nr, 
	REISERFS_GUESS_PLUGIN_ID, node->key_plugin->h.id)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open node %llu.", block_nr);
	return NULL;
    }
    
    return reiserfs_cache_create(node);
}

reiserfs_cache_t *reiserfs_tree_lneighbour(reiserfs_cache_t *cache) {
    return reiserfs_tree_neighbour(cache, LEFT);
}

reiserfs_cache_t *reiserfs_tree_rneighbour(reiserfs_cache_t *cache) {
    return reiserfs_tree_neighbour(cache, RIGHT);
}

#ifndef ENABLE_COMPACT

errno_t reiserfs_tree_shift(reiserfs_coord_t *old, reiserfs_coord_t *new, 
    uint32_t needed)
{
    int point = 0;
    int count, moved = 0;
    
    reiserfs_pos_t pos;
    reiserfs_key_t key;
    reiserfs_cache_t *left;
    reiserfs_cache_t *right;
    reiserfs_coord_t src, dst;

    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    
    /* Checking for the root node which has not parent and has not any neighbours */
    if (!old->cache->parent)
	return 0;
    
    /* 
	Checking the left neighbour and loading if it doesn't exists. Both neighbour
	nodes are needed to perform the shift of items from target node in order
	to free enoguh free space for inserting new item.
    */
    if (!(left = old->cache->left)) {
	if ((left = reiserfs_tree_lneighbour(old->cache))) {
	    if (reiserfs_cache_register(old->cache->parent, left)) {
		reiserfs_cache_close(left);
		return -1;
	    }
	}
    }
    
    /* 
	Checking the right neighbour and loading if it doesn't exists. The same 
	as previous one.
    */
    if (!(right = old->cache->right)) {
	if ((right = reiserfs_tree_rneighbour(old->cache))) {
	    if (reiserfs_cache_register(old->cache->parent, right)) {
		reiserfs_cache_close(right);
		return -1;
	    }
	}
    }
   
    /* 
	Getting the target node position in its parent. This will be used bellow
	for updating left delimiting keys after shift will be complete.
    */
    if (reiserfs_node_ldkey(old->cache->node, &key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get left delimiting key of node %llu.",
	    aal_block_get_nr(old->cache->node->block));
	return -1;
    }

    if (reiserfs_node_lookup(old->cache->node, &key, &pos) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find left delimiting key of node %llu.",
	    aal_block_get_nr(old->cache->node->block));
	return -1;
    }
    
    *new = *old;
    point = old->pos.item;
    
    /* Trying to move items into the left neighbour */
    if (left) {
	uint32_t item_len = reiserfs_node_item_length(old->cache->node, 0) + 
	    reiserfs_node_item_overhead(old->cache->node);
	
	while (point >= 0 && reiserfs_node_count(old->cache->node) > 0 && 
	    reiserfs_node_get_free_space(left->node) >= item_len)
	{
	    if (point == 0) {
		if (reiserfs_node_get_free_space(left->node) - item_len < needed)
		    break;
	    }
	    reiserfs_coord_init(&src, old->cache, 0, 0xffff);
	    reiserfs_coord_init(&dst, left, reiserfs_node_count(left->node), 0xffff);
	
	    if (reiserfs_tree_move(&dst, &src)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Left shifting failed. Can't move item.");
		return -1;
	    }
	    
	    item_len = reiserfs_node_item_length(old->cache->node, 0) + 
		reiserfs_node_item_overhead(old->cache->node);
	    
	    point--;
	}
    }
    
    if (point < 0)
	reiserfs_coord_init(new, left, reiserfs_node_count(left->node) - 1, 0xffff);
    
    count = reiserfs_node_count(old->cache->node);
   
    /* Trying to move items into the right neghbour */
    if (right && count > 0) {
	
	uint32_t item_len = reiserfs_node_item_length(old->cache->node, 
	    reiserfs_node_count(old->cache->node) - 1) + 
	    reiserfs_node_item_overhead(old->cache->node);
	    
	while (reiserfs_node_count(old->cache->node) > 0 && 
	    reiserfs_node_get_free_space(right->node) >= item_len)
	{
	    /* 
		Checking for the case when insertion point is almost shifted 
		into right neighbour.
	    */
	    if (moved >= count - new->pos.item) {
		if ((reiserfs_node_get_free_space(right->node) - item_len) < needed)
		    break;
	    }
	    reiserfs_coord_init(&src, old->cache, 
		reiserfs_node_count(old->cache->node) - 1, 0xffff);

	    reiserfs_coord_init(&dst, right, 0, 0xffff);
	
	    if (reiserfs_tree_move(&dst, &src)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Right shifting failed. Can't move item.");
		return -1;
	    }
	    
	    item_len = reiserfs_node_item_length(old->cache->node, 
		reiserfs_node_count(old->cache->node) - 1) + 
		reiserfs_node_item_overhead(old->cache->node);
	    
	    moved++;
	}
    }
    
    if (moved > count - new->pos.item) {
	reiserfs_coord_init(new, right,
	    moved - (count - new->pos.item), 0xffff);
    }
    
    /* Updating internal key for shifted node */
    if (left && old->pos.item != new->pos.item) {
	reiserfs_node_ldkey(old->cache->node, &key);
	if (reiserfs_node_embed_key(old->cache->parent->node, pos.item, &key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't update left delimiting key for shifted node %llu.",
		aal_block_get_nr(old->cache->node->block));
	    return -1;
	}
    }

    /* Updating ldkey for left neighbour */
    if (right && count != reiserfs_node_count(old->cache->node)) {
	reiserfs_node_ldkey(right->node, &key);
	if (reiserfs_node_embed_key(right->node, pos.item + 1, &key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't update left delimiting key for right neighbour block %llu.",
		aal_block_get_nr(right->node->block));
	    return -1;
	}
    }

    /* 
	Checking whether target node still contains any items. If no then we 
	should delete it from the tree.
    */
    if (reiserfs_node_count(old->cache->node) == 0) {
	reiserfs_cache_unregister(old->cache->parent, old->cache);
	reiserfs_node_remove(old->cache->parent->node, &pos);
    }

    return 0;
}

#endif

/* Inserts a node into the tree */
static errno_t reiserfs_tree_insert_node(reiserfs_tree_t *tree, 
    reiserfs_cache_t *parent, reiserfs_cache_t *cache)
{
    int lookup;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;

    reiserfs_item_hint_t item;
    reiserfs_internal_hint_t internal;
    
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-647", cache != NULL, return -1);
    aal_assert("umka-796", parent != NULL, return -1);

    if (reiserfs_node_ldkey(cache->node, &ldkey))
	return -1;
    
    coord.cache = parent;
    
    if ((lookup = reiserfs_node_lookup(parent->node, &ldkey, &coord.pos)) == -1)
	return -1;
    
    if (lookup == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in block %llu.", 
	    reiserfs_key_get_locality(&ldkey), reiserfs_key_get_type(&ldkey),
	    reiserfs_key_get_objectid(&ldkey), reiserfs_key_get_offset(&ldkey),
	    aal_block_get_nr(parent->node->block));
	return -1;
    }
    
    aal_memset(&item, 0, sizeof(item));
    internal.pointer = aal_block_get_nr(cache->node->block);

    item.hint = &internal;
    item.type = REISERFS_INTERNAL_ITEM;
    
    /* FIXME-UMKA: Hardcoded internal item id */
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 0x3)))
    	libreiser4_factory_failed(return -1, find, item, 0x3);
   
    /* Estimating found internal node */
    if (reiserfs_node_item_estimate(parent->node, &item, &coord.pos))
	return -1;
 
    /* 
	Checking for free space inside found internal node. If it is enought 
	for inserting one more internal item into it then we doing this. If no, 
	we need to split internal node and insert item into right of new nodes.
	After, we should insert right node into parent of found internal node 
	by reiserfs_tree_insert_node. This may cause tree growing when recursion 
	reach the root node.
    */
    if (reiserfs_node_get_free_space(parent->node) < item.length) {
	reiserfs_coord_t insert;
	
	/* Shift target node */
	aal_memset(&insert, 0, sizeof(insert));
	
	if (reiserfs_tree_shift(&coord, &insert, item.length))
	    return -1;
		
	if (reiserfs_node_get_free_space(insert.cache->node) < item.length) {
	    reiserfs_node_t *right;
	    
	    if (!(right = reiserfs_tree_alloc_node(tree, REISERFS_LEAF_LEVEL + 1))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't allocate new internal node.");
		return -1;
	    }

	    if (reiserfs_node_split(insert.cache->node, right)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't split node %llu.", 
		    aal_block_get_nr(insert.cache->node->block));
		return -1;
	    }

	    /* Checking for the root node. Here we increase the height of the tree */
	    if (!insert.cache->parent) {
		reiserfs_node_t *root;

		/* Allocating new root node */
		if (!(root = reiserfs_tree_alloc_node(tree, 
		    reiserfs_format_get_height(tree->fs->format)))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't allocate root node.");
		    return -1;
		}
		
		if (!(tree->cache = reiserfs_cache_create(root)))
		    return -1;
		
		/* Registering insert point node in the new root node  */
		if (reiserfs_tree_insert_node(tree, tree->cache, insert.cache)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert node %llu into the tree.", 
			aal_block_get_nr(insert.cache->node->block));
		    return -1;
		}
		
		/* Updating the tree height and tree root values in disk format */
		reiserfs_format_set_height(tree->fs->format, 
		    reiserfs_node_get_level(tree->cache->node));
		
		reiserfs_format_set_root(tree->fs->format, 
		    aal_block_get_nr(tree->cache->node->block));
	    }
	   
	    /* Inserting right node into parent of insert point node */ 
	    if (reiserfs_tree_insert_node(tree, insert.cache->parent, 
		reiserfs_cache_create(right))) 
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert node %llu into the tree.", 
		    aal_block_get_nr(right->block));
		return -1;
	    }
	} else {
	    if (reiserfs_node_insert(parent->node, &coord.pos, &ldkey, &item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert an internal item into the node %llu.", 
		    aal_block_get_nr(parent->node->block));
		return -1;
	    }
	}
    } else {
    
	coord.pos.unit = 0xffff;

	/* Inserting item */
	if (reiserfs_node_insert(parent->node, &coord.pos, &ldkey, &item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an internal item into the node %llu.", 
		aal_block_get_nr(parent->node->block));
	    return -1;
	}
    }
    
    /* Adding node to the tree cache */
    reiserfs_cache_register(parent, cache);
    
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
	    aal_block_get_nr(coord.cache->node->block));
	return -1;
    }
  
    if (lookup == -1)
	return -1;
 
    /* Estimating item in order to insert it into found node */
    if (reiserfs_node_item_estimate(coord.cache->node, item, &coord.pos))
        return -1;
 
    /* 
	Checking whether we should create new leaf or we can insert item into 
	existent one. This will be performed in the case we have found an internal
	node, or we have not enought free space in found leaf.
    */
    if (reiserfs_node_get_level(coord.cache->node) > REISERFS_LEAF_LEVEL ||
	reiserfs_node_get_free_space(coord.cache->node) < item->length)
    {
	blk_t block_nr;
	reiserfs_node_t *leaf;

	if (reiserfs_node_get_level(coord.cache->node) > REISERFS_LEAF_LEVEL) {
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
		    aal_block_get_nr(coord.cache->node->block));

		reiserfs_node_close(leaf);
		return -1;
	    }
	
	    /* Inserting new leaf into the tree */
	    if (reiserfs_tree_insert_node(tree, coord.cache, 
		reiserfs_cache_create(leaf))) 
	    {
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
	    
	    if (reiserfs_tree_shift(&coord, &insert, item->length))
		return -1;    
	    
	    if (reiserfs_node_get_free_space(insert.cache->node) < item->length) {
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
			aal_block_get_nr(coord.cache->node->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	
		/* Inserting new leaf into the tree */
		if (reiserfs_tree_insert_node(tree, 
		    coord.cache->parent, reiserfs_cache_create(leaf))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert node %llu into the thee.", 
			aal_block_get_nr(leaf->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	    } else {
		    
		/* Inserting item into found after shifting point */
		if (reiserfs_node_insert(insert.cache->node, &insert.pos, key, item)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert an internal item into the node %llu.", 
			aal_block_get_nr(coord.cache->node->block));
		    return -1;
		}
	    }
	}
    } else {
	    
	/* Inserting item into existent one leaf */
	if (reiserfs_node_insert(coord.cache->node, &coord.pos, key, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an internal item into the node %llu.", 
		aal_block_get_nr(coord.cache->node->block));
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

