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

#ifndef ENABLE_COMPACT

/* Requests block allocator for new block and creates empty node in it */
static reiserfs_node_t *reiserfs_tree_alloc_node(
    reiserfs_tree_t *tree,	/* tree for operating on */
    uint8_t level		/* level of new node */
) {
    blk_t block_nr;
    
    aal_assert("umka-756", tree != NULL, return NULL);
    
    /* Allocating the block */
    if (!(block_nr = reiserfs_alloc_alloc(tree->fs->alloc))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't allocate block for a node.");
	return NULL;
    }

    /* Creating new node */
    return reiserfs_node_create(tree->fs->host_device, block_nr,
        reiserfs_node_get_pid(tree->cache->node), level);
}

#endif

/* Setting up the tree cache limit */
static errno_t reiserfs_tree_setup(reiserfs_tree_t *tree) {
    aal_assert("umka-859", tree != NULL, return -1);

    tree->limit.cur = 0;

    if (libreiser4_mlimit_get() > 0)
	tree->limit.max = libreiser4_mlimit_get();
    else {
	/* FIXME-UMKA: Here limit should be calculated by libreiser4 itself */
	tree->limit.max = 1000;
    }
    
    tree->limit.enabled = 1;
    
    return 0;
}

/* Opens balanced tree (that is tree cache) on specified filesystem */
reiserfs_tree_t *reiserfs_tree_open(reiserfs_fs_t *fs) {
    reiserfs_tree_t *tree;
    reiserfs_node_t *node;

    aal_assert("umka-737", fs != NULL, return NULL);

    /* Allocating memory for the tree instance */
    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;
    
    tree->fs = fs;
    
    /* Opening root node */
    if (!(node = reiserfs_node_open(fs->host_device, 
	    reiserfs_format_get_root(fs->format))))
	goto error_free_tree;
    
    /* Creating cache for root node */
    if (!(tree->cache = reiserfs_cache_create(node)))
	goto error_free_node;
    
    tree->cache->tree = tree;
    
    /* Setting up tree cachee limits */
    if (reiserfs_tree_setup(tree)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
	    "Can't initialize cache limits. Cache limit spying will be disables.");
	tree->limit.enabled = 0;
    }
    
    return tree;

error_free_node:
    reiserfs_node_close(node);
error_free_tree:
    aal_free(tree);
    return NULL;
}

/* Returns tree root cache */
reiserfs_cache_t *reiserfs_tree_root(reiserfs_tree_t *tree) {
    aal_assert("umka-738", tree != NULL, return NULL);
    return tree->cache;
}

#ifndef ENABLE_COMPACT

/* Creates new balanced tree on specified filesystem */
reiserfs_tree_t *reiserfs_tree_create(
    reiserfs_fs_t *fs,		    /* filesystem new tree will be created on */
    reiserfs_profile_t *profile	    /* profile to be used */
) {
    blk_t block_nr;
    reiserfs_node_t *node;
    reiserfs_tree_t *tree;

    aal_assert("umka-741", fs != NULL, return NULL);
    aal_assert("umka-749", profile != NULL, return NULL);

    /* Allocating memory needed for tree instance */
    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;

    tree->fs = fs;

    /* Getting free block from block allocator for place root block in it */
    if (!(block_nr = reiserfs_alloc_alloc(fs->alloc))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't allocate block for the root node.");
	goto error_free_tree;
    }

    /* Creating root node */
    if (!(node = reiserfs_node_create(fs->host_device, block_nr,
        profile->node, reiserfs_format_get_height(fs->format))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create root node.");
	goto error_free_tree;
    }

    /* Creating cache for root node */
    if (!(tree->cache = reiserfs_cache_create(node)))
	goto error_free_node;
    
    tree->cache->tree = tree;
    
    /* Setting up tree cahce limits */
    if (reiserfs_tree_setup(tree)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
	    "Can't initialize cache limits. Cache limit spying will be disables.");
	tree->limit.enabled = 0;
    }
    
    return tree;

error_free_node:
    reiserfs_node_close(node);
error_free_tree:
    aal_free(tree);
    return NULL;
}

/* 
    Syncs whole tree cache and removes all nodes except root node from the 
    cache.
*/
errno_t reiserfs_tree_flush(reiserfs_tree_t *tree) {

    aal_assert("umka-573", tree != NULL, return -1);
    
    reiserfs_tree_sync(tree);
    
    if (tree->cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, tree->cache->list)
	    reiserfs_cache_close((reiserfs_cache_t *)walk->item);
	
	tree->cache->list = NULL;
    }

    return 0;
}

/* Syncs whole tree cache */
errno_t reiserfs_tree_sync(reiserfs_tree_t *tree) {
    aal_assert("umka-560", tree != NULL, return -1);
    
    return reiserfs_cache_sync(tree->cache);
}

#endif

/* Closes specified tree (frees all assosiated memory) */
void reiserfs_tree_close(reiserfs_tree_t *tree) {
    aal_assert("umka-134", tree != NULL, return);
    
    reiserfs_cache_close(tree->cache);
    aal_free(tree);
}

/* 
    Makes search in the tree by specified key. Fills passed coord by coords of 
    found item. 
*/
int reiserfs_tree_lookup(
    reiserfs_tree_t *tree,	/* tree to be grepped */
    uint8_t level,		/* stop level for search */
    reiserfs_key_t *key,	/* key to be find */
    reiserfs_coord_t *coord	/* coord of found item */
) {
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
	if (reiserfs_node_get_level(coord->cache->node) <= level)
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

	if (!reiserfs_node_item_internal(coord->cache->node, coord->pos.item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Not internal item was found on" 
		" the twig level. Sorry, drilling doesn't supported yet!");
	    return 0;
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
	reiserfs_node_get_key(coord->cache->node, coord->pos.item, &ikey);

	if (!(coord->cache = reiserfs_cache_find(parent, &ikey))) {
	    reiserfs_node_t *node;
	    /* 
		Node was not found in the cache, we open it and register in the 
		cache.
	    */
	    if (!(node = reiserfs_node_open(parent->node->block->device, block_nr))) 
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

/* Moves item specified by src into place specified by dst */
errno_t reiserfs_tree_move(
    reiserfs_coord_t *dst,	    /* destination coord of item*/
    reiserfs_coord_t *src	    /* source coord of item */
) {
    if (src->cache->parent)
	reiserfs_cache_unregister(src->cache->parent, src->cache);
    
    if (dst->cache->parent) {
	if (reiserfs_cache_register(dst->cache->parent, src->cache))
	    return -1;
    }
    
    return reiserfs_node_move(dst->cache->node, &dst->pos, 
	src->cache->node, &src->pos);
}

/* 
    The central packing on insert function. It shifts some number of items to left
    or right neightbor in order to release "needed" space in specified by "old"
    node. As insertion point may be shifted to one of neighbors, new insertion 
    point position is stored in "new" coord.
*/
errno_t reiserfs_tree_shift(
    reiserfs_coord_t *old,	    /* old coord of insertion point */
    reiserfs_coord_t *new,	    /* new coord will be stored here */
    uint32_t needed		    /* amount of space that should be freed */
) {
    int32_t point;
    reiserfs_pos_t pos;
    uint32_t count, moved = 0;
    
    reiserfs_key_t key;
    reiserfs_coord_t src, dst;
    reiserfs_cache_t *left = NULL;
    reiserfs_cache_t *right = NULL;

    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    
    /* Checking for the root node which has not parent and has not any neighbours */
    if (!old->cache->parent)
	return 0;
    
    /* 
	Checking both neighbours and loading them if needed in order to perform
	the shifting of items from target node into neighbours.
    */
    if (reiserfs_cache_raise(old->cache)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't raise up neighbours of node %llu.", 
	    aal_block_get_nr(old->cache->node->block));
	return -1;
    }
	    
    /* 
	Getting the target node position in its parent. This will be used bellow
	for updating left delimiting keys after shift will be complete.
    */
    if (reiserfs_cache_pos(old->cache, &pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find left delimiting key of node %llu.",
	    aal_block_get_nr(old->cache->node->block));
	return -1;
    }
    
    *new = *old;
    point = pos.item;
    
    /* Trying to move items into the left neighbour */
    if (left) {
	uint32_t item_len = reiserfs_node_item_len(old->cache->node, 0) + 
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
	    
	    item_len = reiserfs_node_item_len(old->cache->node, 0) + 
		reiserfs_node_item_overhead(old->cache->node);
	    
	    point--;
	}
    }
    
    if (point < 0)
	reiserfs_coord_init(new, left, reiserfs_node_count(left->node) - 1, 0xffff);
    
    count = reiserfs_node_count(old->cache->node);
   
    /* Trying to move items into the right neghbour */
    if (right && count > 0) {
	
	uint32_t item_len = reiserfs_node_item_len(old->cache->node, 
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
	    
	    item_len = reiserfs_node_item_len(old->cache->node, 
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
	if (reiserfs_node_set_key(old->cache->parent->node, pos.item, &key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't update left delimiting key for shifted node %llu.",
		aal_block_get_nr(old->cache->node->block));
	    return -1;
	}
    }

    /* Updating internal key for left neighbour */
    if (right && count != reiserfs_node_count(old->cache->node)) {
	reiserfs_node_ldkey(right->node, &key);
	if (reiserfs_node_set_key(right->node, pos.item + 1, &key)) {
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

/* Adds passed cached node to tree */
errno_t reiserfs_tree_add(
    reiserfs_tree_t *tree,	    /* tree we will operate on */
    reiserfs_cache_t *parent,	    /* cached node we will insert in */
    reiserfs_cache_t *cache	    /* cached node to be inserted */
) {
    int lookup;
    uint32_t needed;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;

    reiserfs_item_hint_t item;
    reiserfs_internal_hint_t internal;
    
    aal_assert("umka-646", tree != NULL, return -1);
    aal_assert("umka-647", cache != NULL, return -1);
    aal_assert("umka-796", parent != NULL, return -1);

    /* Getting left delimiting key from passed node to be inserted */
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

    /* Preparing internal item hint */
    aal_memset(&item, 0, sizeof(item));
    internal.pointer = aal_block_get_nr(cache->node->block);

    reiserfs_key_init(&item.key, ldkey.plugin, ldkey.body);
    
    item.hint = &internal;
    item.type = REISERFS_INTERNAL_ITEM;
    
    /* FIXME-UMKA: Hardcoded internal item id */
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, REISERFS_INTERNAL_ITEM)))
    	libreiser4_factory_failed(return -1, find, item, REISERFS_INTERNAL_ITEM);
   
    /* Estimating found internal node */
    if (reiserfs_node_item_estimate(parent->node, &coord.pos, &item))
	return -1;
 
    needed = item.len + reiserfs_node_item_overhead(parent->node);
    
    /* 
	Checking for free space inside found internal node. If it is enought 
	for inserting one more internal item into it then we doing that. If no, 
	we need to split internal node and insert item into right of new nodes.
	After, we should insert right node into parent of found internal node 
	by reiserfs_tree_add. This may cause tree growing when recursion reach 
	the root node.
    */
    if (reiserfs_node_get_free_space(parent->node) < needed) {
	reiserfs_coord_t insert;
	
	/* Shift target node */
	aal_memset(&insert, 0, sizeof(insert));
	
	if (reiserfs_tree_shift(&coord, &insert, needed))
	    return -1;
		
	if (reiserfs_node_get_free_space(insert.cache->node) < needed) {
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
		if (reiserfs_tree_add(tree, tree->cache, insert.cache)) {
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
	    if (reiserfs_tree_add(tree, insert.cache->parent, 
		reiserfs_cache_create(right))) 
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert node %llu into the tree.", 
		    aal_block_get_nr(right->block));
		return -1;
	    }
	} else {
	    if (reiserfs_node_insert(parent->node, &coord.pos, &item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert an internal item into the node %llu.", 
		    aal_block_get_nr(parent->node->block));
		return -1;
	    }
	}
    } else {
	/* Inserting item */
	if (reiserfs_node_insert(parent->node, &coord.pos, &item)) {
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
    the tree by reiserfs_tree_add function. See bellow for details.
*/
errno_t reiserfs_tree_insert(
    reiserfs_tree_t *tree,	    /* tree new item will be inserted in */
    reiserfs_item_hint_t *item	    /* item hint to be inserted */
) {
    int lookup;
    uint32_t needed;
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
 
    if (coord.pos.unit != 0xffff)
	coord.pos.unit++;
    
    /* Estimating item in order to insert it into found node */
    if (reiserfs_node_item_estimate(coord.cache->node, &coord.pos, item))
        return -1;
 
    needed = item->len + reiserfs_node_item_overhead(coord.cache->node);
    
    /* 
	Checking whether we should create new leaf or we can insert item into 
	existent one. This will be performed in the case we have found an internal
	node, or we have not enought free space in found leaf.
    */
    if (reiserfs_node_get_level(coord.cache->node) > REISERFS_LEAF_LEVEL ||
	reiserfs_node_get_free_space(coord.cache->node) < needed)
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
	    if (reiserfs_node_insert(leaf, &coord.pos, item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert item into the node %llu.", 
		    aal_block_get_nr(coord.cache->node->block));

		reiserfs_node_close(leaf);
		return -1;
	    }
	
	    /* Inserting new leaf into the tree */
	    if (reiserfs_tree_add(tree, coord.cache, 
		reiserfs_cache_create(leaf))) 
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't insert node %llu into the tree.", 
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
	    
	    if (reiserfs_tree_shift(&coord, &insert, needed))
		return -1;    
	    
	    if (reiserfs_node_get_free_space(insert.cache->node) < needed) {
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
		if (reiserfs_node_insert(leaf, &coord.pos, item)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert item into the node %llu.", 
			aal_block_get_nr(coord.cache->node->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	
		/* Inserting new leaf into the tree */
		if (reiserfs_tree_add(tree, coord.cache->parent, 
		    reiserfs_cache_create(leaf))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert node %llu into the tree.", 
			aal_block_get_nr(leaf->block));

		    reiserfs_node_close(leaf);
		    return -1;
		}
	    } else {
		/* Inserting item into found after shifting point */
		if (reiserfs_node_insert(insert.cache->node, &insert.pos, item)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Can't insert an %s into the node %llu.", 
			(coord.pos.unit == 0xffff ? "item" : "unit"),
			aal_block_get_nr(coord.cache->node->block));
		    return -1;
		}
	    }
	}
    } else {
	/* Inserting item into existent one leaf */
	if (reiserfs_node_insert(coord.cache->node, &coord.pos, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an %s into the node %llu.", 
		(coord.pos.unit == 0xffff ? "item" : "unit"),
		aal_block_get_nr(coord.cache->node->block));
	    return -1;
	}
    }

    return 0;
}

/* Removes item by specified key */
errno_t reiserfs_tree_remove(
    reiserfs_tree_t *tree,	/* tree item will beremoved from */
    reiserfs_key_t *key		/* key item will be found by */
) {
    return -1;
}

#endif

