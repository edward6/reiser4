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
static reiserfs_cache_t *reiserfs_tree_alloc(
    reiserfs_tree_t *tree,	    /* tree for operating on */
    uint8_t level		    /* level of new node */
) {
    blk_t blk;
    reiserfs_node_t *node;
    reiserfs_cache_t *cache;
    
    aal_assert("umka-756", tree != NULL, return NULL);
    
    /* Allocating the block */
    if (!(blk = reiserfs_alloc_alloc(tree->fs->alloc))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't allocate block for a node.");
	return NULL;
    }

    /* Creating new node */
    if (!(node = reiserfs_node_create(tree->fs->format->device, blk,
	    reiserfs_node_get_pid(tree->cache->node), level)))
	return NULL;

    if (!(cache = reiserfs_cache_create(node))) {
	reiserfs_node_close(node);
	return NULL;
    }
    
    return cache;
}

#endif

static void reiserfs_tree_dealloc(reiserfs_tree_t *tree, 
    reiserfs_cache_t *cache) 
{
    aal_assert("umka-917", cache != NULL, return);
    aal_assert("umka-918", cache->node != NULL, return);

    reiserfs_alloc_dealloc(tree->fs->alloc, 
	aal_block_get_nr(cache->node->block));
    
    reiserfs_cache_close(cache);
}

static reiserfs_cache_t *reiserfs_tree_load(reiserfs_tree_t *tree, 
    blk_t blk) 
{
    reiserfs_node_t *node;
    reiserfs_cache_t *cache;
    
    if (!(node = reiserfs_node_open(tree->fs->format->device, blk))) 
	return NULL;
	    
    if (!(cache = reiserfs_cache_create(node))) {
	reiserfs_node_close(node);
	return NULL;
    }
    
    return cache;
}

/* Setting up the tree cache limit */
static errno_t reiserfs_tree_setup(reiserfs_tree_t *tree) {
    aal_assert("umka-859", tree != NULL, return -1);

    tree->limit.cur = 0;

    if (libreiser4_mlimit_get() > 0)
	tree->limit.max = libreiser4_mlimit_get();
    else {
	/* FIXME-UMKA: Here limit should be calculated by libreiser4 */
	tree->limit.max = 1000;
    }
    
    tree->limit.enabled = 1;
    
    return 0;
}

/* Opens balanced tree (that is tree cache) on specified filesystem */
reiserfs_tree_t *reiserfs_tree_open(reiserfs_fs_t *fs) {
    reiserfs_tree_t *tree;

    aal_assert("umka-737", fs != NULL, return NULL);

    /* Allocating memory for the tree instance */
    if (!(tree = aal_calloc(sizeof(*tree), 0)))
	return NULL;
    
    tree->fs = fs;
    
    /* Opening root node */
    if (!(tree->cache = reiserfs_tree_load(tree, 
	    reiserfs_format_get_root(fs->format))))
	goto error_free_tree;
    
    tree->cache->tree = tree;
    
    /* Setting up tree cache limits */
    if (reiserfs_tree_setup(tree)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
	    "Can't initialize cache limits. Cache limit spying will be disables.");
	tree->limit.enabled = 0;
    }
    
    return tree;

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
    if (!(node = reiserfs_node_create(fs->format->device, block_nr,
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
    blk_t blk;
    int lookup;
    reiserfs_key_t ikey;
    reiserfs_cache_t *parent;

    aal_assert("umka-742", key != NULL, return -1);
    aal_assert("umka-742", coord != NULL, return -1);
  
    reiserfs_coord_init(coord, tree->cache, 0, ~0ul);
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

	if (!reiserfs_node_item_internal(coord->cache->node, &coord->pos)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Not internal item was found on the twig level. "
		"Sorry, drilling doesn't supported yet!");
	    return 0;
	}

	/* Getting the node pointer from internal item */
	if (!(blk = reiserfs_node_get_pointer(coord->cache->node, 
	    &coord->pos)))
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
	reiserfs_node_get_key(coord->cache->node, &coord->pos, &ikey);

	if (!(coord->cache = reiserfs_cache_find(parent, &ikey))) {
	    /* 
		Node was not found in the cache, we open it and register in the 
		cache.
	    */
	    if (!(coord->cache = reiserfs_tree_load(tree, blk))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't load node %llu durring lookup.", blk);
		return -1;
	    }
	    
	    /* Registering node in tree cache */
	    if (reiserfs_cache_register(parent, coord->cache)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't register node %llu in the tree.", blk);
		reiserfs_tree_dealloc(tree, coord->cache);
		return -1;
	    }
	}
    }
    
    return 0;
}

#ifndef ENABLE_COMPACT

/*static errno_t reiserfs_tree_unit_move(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src) 
{
    reiserfs_body_t *body;
	
    reiserfs_plugin_t *plugin;
    reiserfs_entry_hint_t entry;
    reiserfs_direntry_hint_t direntry;
    reiserfs_item_hint_t direntry_item;
	
    reiserfs_key_t item_key;
    reiserfs_plugin_t *key_plugin;
    	
    if (!(body = reiserfs_node_item_body(src->cache->node, &src->pos)))
        return -1;
	
    if (!(plugin = reiserfs_node_item_plugin(src->cache->node, &src->pos)))
        return -1;
    
    if ((libreiser4_plugin_call(return -1, plugin->item_ops.specific.direntry, 
	    entry, body, src->pos.unit, &entry)))
        return -1;
	
    aal_memset(&direntry_item, 0, sizeof(direntry_item));
	    
    direntry.count = 1;
    direntry_item.plugin = plugin;
    direntry_item.type = DIRENTRY_ITEM;
	
    if (!(key_plugin = libreiser4_factory_find_by_id(KEY_PLUGIN_TYPE, KEY_REISER40_ID)))
        return -1;

    if (reiserfs_node_get_key(src->cache->node, &src->pos, &item_key))
        return -1;
	
    reiserfs_key_init(&direntry_item.key, key_plugin, item_key.body);
    reiserfs_key_build_by_entry(&direntry_item.key, &entry.entryid);
	    
    if (!(direntry.entry = aal_calloc(sizeof(entry), 0)))
        return -1;
	
    direntry.entry[0] = entry;
    direntry_item.hint = &direntry;
    
    if (reiserfs_node_item_estimate(dst->cache->node, &dst->pos, &direntry_item))
	return -1;

    if (direntry_item.len > reiserfs_node_get_space(dst->cache->node))
	return -1;
    
    if (reiserfs_cache_insert(dst->cache, &dst->pos, &direntry_item))
        return -1;
    
    if (reiserfs_cache_remove(src->cache, &src->pos))
        return -1;

    return 0;
}*/

/* This function inserts internal item to the tree */
static errno_t reiserfs_tree_attach(
    reiserfs_tree_t *tree,	    /* tree we will attach node to */
    reiserfs_cache_t *cache	    /* child to attached */
) {
    int lookup, level;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;
    reiserfs_internal_hint_t internal;
    reiserfs_item_hint_t internal_item;

    aal_assert("umka-913", tree != NULL, return -1);
    aal_assert("umka-916", cache != NULL, return -1);
    aal_assert("umka-919", reiserfs_node_count(cache->node) > 0, return -1);
    
    /* Preparing internal item hint */
    aal_memset(&internal_item, 0, sizeof(internal_item));
	
    /* 
	FIXME-UMKA: Hardcoded internal item id. Here should be getting internal
	item plugin id from parent. In the case parent doesn't exist, it should
	be got from filesystem default profile.
    */
    if (!(internal_item.plugin = 
	libreiser4_factory_find_by_id(ITEM_PLUGIN_TYPE, ITEM_INTERNAL40_ID)))
    {
	libreiser4_factory_failed(return -1, find, item, ITEM_INTERNAL40_ID);
    }

    aal_memset(&internal, 0, sizeof(internal));
    
    reiserfs_node_ldkey(cache->node, &ldkey);
    internal.pointer = aal_block_get_nr(cache->node->block);
    reiserfs_key_init(&internal_item.key, ldkey.plugin, ldkey.body);

    internal_item.hint = &internal;

    level = REISERFS_LEAF_LEVEL + 1;
    if ((lookup = reiserfs_tree_lookup(tree, level, &ldkey, &coord)) == -1)
	return -1;
    
    if (lookup == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in tree.", 
	    reiserfs_key_get_locality(&ldkey), reiserfs_key_get_type(&ldkey),
	    reiserfs_key_get_objectid(&ldkey), reiserfs_key_get_offset(&ldkey));
	return -1;
    }
    
    if (reiserfs_cache_insert(coord.cache, &coord.pos, &internal_item)) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't insert internal item to the tree.");
	return -1;
    }
    
    if (reiserfs_cache_register(coord.cache, cache)) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't register node %llu in tree cache.", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }

    return 0;
}

/* This function grows and sets up tree after the growing */
static errno_t reiserfs_tree_grow(
    reiserfs_tree_t *tree,	/* tree to be growed */
    reiserfs_cache_t *cache	/* old root cached node */
) {
    /* Allocating new root node */
    if (!(tree->cache = reiserfs_tree_alloc(tree,
	reiserfs_format_get_height(tree->fs->format) + 1))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate root node.");
	return -1;
    }

    if (reiserfs_tree_attach(tree, cache)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't attach node to the tree.");
	goto error_free_cache;
    }
	    
    /* Updating the tree height and tree root values in disk format */
    reiserfs_format_set_height(tree->fs->format, 
	reiserfs_node_get_level(tree->cache->node));
			
    reiserfs_format_set_root(tree->fs->format, 
        aal_block_get_nr(tree->cache->node->block));

    return 0;

error_free_cache:
    reiserfs_tree_dealloc(tree, tree->cache);
    tree->cache = cache;
    return -1;
}

/* Pefroms left shifting of items */
errno_t reiserfs_tree_lshift(
    reiserfs_tree_t *tree,	    /* tree pointer function operates on */
    reiserfs_coord_t *old,	    /* old coord of insertion point */
    reiserfs_coord_t *new,	    /* new coord will be stored here */
    uint32_t needed,		    /* amount of space that should be freed */
    int allocate		    /* to do allocation of new blocks or not */
) {
    reiserfs_cache_t *left;
    reiserfs_cache_t *parent;
    
    reiserfs_coord_t src, dst;

    uint32_t item_len;
    uint32_t item_overhead;

    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    aal_assert("umka-929", tree != NULL, return -1);

    *new = *old;
    
    /* Checking for the root node which has not parent and has not any neighbours */
    if (needed == 0) return 0;
    
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

    if (!(left = old->cache->left))
	return 0;

    item_overhead = reiserfs_node_item_overhead(old->cache->node);
    
    /* Trying to move items into the left neighbour */
    if (reiserfs_node_count(old->cache->node) > 0) {
	reiserfs_pos_t mpos;

	reiserfs_pos_init(&mpos, 0, ~0ul);

        /* Initializing item_len by length of the first item from shifted node */
	item_len = reiserfs_node_item_len(old->cache->node, &mpos) + item_overhead;
	
	/* Moving items until insertion point reach first position in node */
	while (reiserfs_node_count(old->cache->node) > 0 && 
	    reiserfs_node_get_space(new->cache->node) < needed)
	{
	    if (allocate && (reiserfs_node_get_space(left->node) < item_len ||
		(new->cache == old->cache && new->pos.item == 0 &&
		reiserfs_node_get_space(left->node) - item_len < needed)))
	    {
		if (!(left = reiserfs_tree_alloc(tree, 
		    reiserfs_node_get_level(left->node)))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		        "Can't allocate new leaf node durring left shift.");
		    return -1;
		}
	    }
	    
	    /* Updating coord of new insertion point */
	    if (new->cache == old->cache) {
		if (new->pos.item == 0) {
		    new->cache = left;
		    new->pos.item = reiserfs_node_count(left->node);
		} else 
		    new->pos.item--;
	    } else
	        new->pos.item--;
	    
	    /* Preparing coord for moving items */
	    reiserfs_coord_init(&src, old->cache, 0, ~0ul);
	    
	    reiserfs_coord_init(&dst, left, 
		reiserfs_node_count(left->node), ~0ul);
	    
	    /* Moving item into left neighbor */
	    if (reiserfs_tree_move(tree, &dst, &src)) {
	        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		   "Left shifting failed. Can't move item.");
		return -1;
	    }
	    
	    if (reiserfs_node_count(old->cache->node) > 0) {
		item_len = reiserfs_node_item_len(old->cache->node, &mpos) + 
		    item_overhead;
	    }
	}
	
	if (left != old->cache->left) {
	    if (!old->cache->parent) {
		/* 
		    Tree growing is occured in the case insertion point hasn't
		    parent.
		*/
		if (reiserfs_tree_grow(tree, old->cache)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't grow tree durring right shift.");
		    return -1;
		}
	    }
	    
	    if (reiserfs_tree_attach(tree, left)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't attach new left neighbour block to the tree "
		    "durring left shift.");
		return -1;
	    }
	}
    }

    return 0;
}

/* Performs shift of items into right neighbor */
errno_t reiserfs_tree_rshift(
    reiserfs_tree_t *tree,	    /* tree pointer function operates on */
    reiserfs_coord_t *old,	    /* old coord of insertion point */
    reiserfs_coord_t *new,	    /* new coord will be stored here */
    uint32_t needed,		    /* amount of space that should be freed */
    int allocate		    /* to do allocation of new blocks or not */
) {
    reiserfs_cache_t *right;
    reiserfs_cache_t *parent;
    reiserfs_coord_t src, dst;

    uint32_t item_len;
    uint32_t item_overhead;

    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    aal_assert("umka-929", tree != NULL, return -1);

    *new = *old;
    
    /* Checking for the root node which has not parent and has not any neighbours */
    if (needed == 0)
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

    if (!(right = old->cache->right)) {

	if (!allocate) return 0;
	
	if (!(right = reiserfs_tree_alloc(tree, 
	    reiserfs_node_get_level(old->cache->node)))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't allocate new leaf node durring right shift.");
	    return -1;
	}
    }
    
    item_overhead = reiserfs_node_item_overhead(old->cache->node);
    
    /* Trying to move items into the right neighbour */
    if (reiserfs_node_count(old->cache->node) > 0) {
	reiserfs_pos_t mpos;
	
	reiserfs_pos_init(&mpos, 
	    reiserfs_node_count(old->cache->node) - 1, ~0ul);
	
	item_len = reiserfs_node_item_len(old->cache->node, &mpos) + 
	    item_overhead;
	    
	while (reiserfs_node_count(old->cache->node) > 0 && 
	    reiserfs_node_get_space(new->cache->node) < needed)
	{
	    uint32_t count = reiserfs_node_count(new->cache->node);
	    
	    /* Allocating new block */
	    if (allocate && (reiserfs_node_get_space(right->node) < item_len ||
		(new->cache == old->cache && new->pos.item >= count - 1 &&
		(reiserfs_node_get_space(right->node) -	item_len) < needed)))
	    {
		if (!(right = reiserfs_tree_alloc(tree, 
		    reiserfs_node_get_level(right->node)))) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't allocate new leaf node durring right shift.");
		    return -1;
		}
	    }
	    
	    if (new->cache == old->cache) {
		if (new->pos.item >= count - 1) {
		    new->cache = right;
		    new->pos.item = (new->pos.item > count - 1);
		}
	    } else
		new->pos.item++;
	    
	    reiserfs_coord_init(&src, old->cache, 
		reiserfs_node_count(old->cache->node) - 1, ~0ul);

	    reiserfs_coord_init(&dst, right, 0, ~0ul);
	
	    if (reiserfs_tree_move(tree, &dst, &src)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Right shifting of an item failed.");
		return -1;
	    }
	    
	    /* Updating item_len for next item to be moved */
	    if (reiserfs_node_count(old->cache->node) > 0) {
		mpos.item = reiserfs_node_count(old->cache->node) - 1;
		item_len = reiserfs_node_item_len(old->cache->node, &mpos) + 
		    item_overhead;
	    }
	}
	
	if (right != old->cache->right) {
	    if (!old->cache->parent) {
		/* 
		    Tree growing is occured in the case insertion point hasn't
		    parent.
		*/
		if (reiserfs_tree_grow(tree, old->cache)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Can't grow tree durring right shift.");
		    return -1;
		}
	    }

	    if (reiserfs_tree_attach(tree, right)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't attach new right neighbour block to the tree "
		    "durring right shift.");
		return -1;
	    }
	}
    }
    
    return 0;
}

/* 
    The central packing on insert function. It shifts some number of items to left
    or right neightbor in order to release "needed" space in specified by "old"
    node. As insertion point may be shifted to one of neighbors, new insertion 
    point position is stored in "new" coord.
*/
errno_t reiserfs_tree_mkspace(
    reiserfs_tree_t *tree,	    /* tree pointer function operates on */
    reiserfs_coord_t *old,	    /* old coord of insertion point */
    reiserfs_coord_t *new,	    /* new coord will be stored here */
    uint32_t needed		    /* amount of space that should be freed */
) {
    reiserfs_cache_t *parent;

    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    aal_assert("umka-929", tree != NULL, return -1);

    if (!old->cache->parent || needed == 0)
	return 0;
    
    /* Trying to shift items into left neighbour */
    if (reiserfs_tree_lshift(tree, old, new, needed, 1))
	return -1;

    /* Trying to shift items into right neighbour */
    if (reiserfs_node_get_space(new->cache->node) < needed && 
	reiserfs_node_count(old->cache->node) > 0) 
    {
	reiserfs_coord_t coord = *new;
	
	if (reiserfs_tree_rshift(tree, &coord, new, needed, 1))
	    return -1;
    }

    if (reiserfs_node_count(old->cache->node) == 0) {
	reiserfs_cache_close(old->cache);
	old->cache = NULL;
    }

    return 0;
}

/* 
    Performs shifting of units of item pointed by old coord into its right 
    neighbour.
*/
/*errno_t reiserfs_tree_ishift(
    reiserfs_coord_t *old,
    reiserfs_coord_t *new,
    uint32_t needed
) {
    uint32_t item_len;
    reiserfs_cache_t *right;	
    reiserfs_coord_t src, dst;

    if (reisersf_cache_raise(old->cache))
	return -1;
    
    if (!(right = old->cache->right))
	return 0;
    
    while (reiserfs_node_count(old->cache->node) > 0 && 
	reiserfs_node_get_space(right->node) >= item_len &&
        reiserfs_node_get_space(new->cache->node) < needed)
    {
	reiserfs_pos_t p;
	reiserfs_plugin_t *lp, *rp;

	reiserfs_pos_init(&p, reiserfs_node_count(old->cache->node) - 1, ~0ul);

	if (!(lp = reiserfs_node_item_plugin(old->cache->node, &p))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find item plugin.");
	    return -1;
	}

	if (libreiser4_plugin_call(return -1, lp->item_ops.common, compound,)) {
	    reiserfs_body_t *lbody, *rbody;
	    uint32_t lcount, rcount;
		    
	    reiserfs_entry_hint_t lentry, rentry;
		    
	    if (!(lbody = reiserfs_node_item_body(old->cache->node, &old->pos)))
		return -1;
		    
	    if (!(lcount = libreiser4_plugin_call(return -1, lp->item_ops.common, 
		count, lbody)))
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Invalid unit count has been detected.");
		return -1;
	    }
		    
	    if (libreiser4_plugin_call(return -1, lp->item_ops.specific.direntry,
		entry, lbody, lcount - 1, &lentry))
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't get first unit from right neighbour item.");
		return -1;
	    }
		    
	    reiserfs_coord_init(&src, old->cache, 
		old->pos.item, lcount - 1);
		    
    	    reiserfs_coord_init(&dst, right, 0, ~0ul);
		    
	    reiserfs_pos_init(&p, 0, ~0ul);
	    if (!(rp = reiserfs_node_item_plugin(right->node, &p))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find item plugin.");
		return -1;
	    }
			
	    if (libreiser4_plugin_call(return -1, rp->item_ops.common, 
		compound,) && lp->h.id == rp->h.id)
	    {
		reiserfs_key_t lkey, rkey;

		reiserfs_node_get_key(old->cache->node, &old->pos, &lkey);
		reiserfs_node_get_key(right->node, &dst.pos, &rkey);
			    
		if (reiserfs_key_get_locality(&lkey) == reiserfs_key_get_locality(&rkey))
		    dst.pos.unit = 0;
	    }
		   
	    if (new->cache == old->cache && new->pos.unit < lcount) {
			    
		if (reiserfs_tree_unit_move(&dst, &src)) {
		    new->pos.unit = ~0ul;
		    break;
		}
			
		if (new->pos.unit == lcount - 1)
		    reiserfs_coord_init(new, right, 0, 0);

	    } else
		reiserfs_coord_init(new, right, 0, 0);
		    
	    continue;
		    
	}
    }
    return 0;
}*/

/* Inserts new item described by item hint into the tree */
errno_t reiserfs_tree_insert(
    reiserfs_tree_t *tree,	    /* tree new item will be inserted in */
    reiserfs_item_hint_t *item,	    /* item hint to be inserted */
    reiserfs_coord_t *coord	    /* coord item or unit inserted at */
) {
    uint32_t needed;
    int lookup, level;
    reiserfs_key_t *key;

    aal_assert("umka-779", tree != NULL, return -1);
    aal_assert("umka-779", item != NULL, return -1);
    aal_assert("umka-909", coord != NULL, return -1);
    
    key = (reiserfs_key_t *)&item->key;

    /* Looking up for target node */
    level = REISERFS_LEAF_LEVEL;
    
    if ((lookup = reiserfs_tree_lookup(tree, level, key, coord)) == -1)
	return -1;

    if (lookup == 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) already exists in tree.", 
	    reiserfs_key_get_locality(key), reiserfs_key_get_type(key),
	    reiserfs_key_get_objectid(key), reiserfs_key_get_offset(key));
	return -1;
    }
    
    if ((level = reiserfs_node_get_level(coord->cache->node)) > 
	REISERFS_LEAF_LEVEL + 1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Lookup stoped on invalid level %d.", level);
	return -1;
    }
    
    /* 
	Correcting unit position in the case lookup was called for pasting unit 
	into existent item, not for inserting new item.
    */
    coord->pos.unit += (coord->pos.unit != ~0ul ? 1 : 0);
    
    /* Estimating item in order to insert it into found node */
    if (reiserfs_node_item_estimate(coord->cache->node, &coord->pos, item))
        return -1;
 
    /* Needed space is estimated space plugs item overhead */
    needed = item->len + (coord->pos.unit == ~0ul ? 
	reiserfs_node_item_overhead(coord->cache->node) : 0);
    
    /* 
	The all functions which are using reiserfs_tree_insert function, are able
	to insert just object items (the all except internals). In this case, tree
	balancing algorithm should serve that calls itself. This is the special case.
    */
    if (level > REISERFS_LEAF_LEVEL) {
	reiserfs_cache_t *cache;
	
	if (!(cache = reiserfs_tree_alloc(tree, REISERFS_LEAF_LEVEL))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't allocate new leaf node.");
	    return -1;
	}

	/* Updating coord by just allocated leaf */
	reiserfs_coord_init(coord, cache, 0, ~0ul);
	
        if (reiserfs_cache_insert(coord->cache, &coord->pos, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't insert an item into the node %llu.", 
		aal_block_get_nr(coord->cache->node->block));
	    reiserfs_tree_dealloc(tree, cache);
	    return -1;
	}
	
	if (reiserfs_tree_attach(tree, cache)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't attach node to the tree.");
	    reiserfs_tree_dealloc(tree, cache);
	    return -1;
	}

	return 0;
    }
    
    /* Inserting item at coord if there is enough free space */
    if (reiserfs_node_get_space(coord->cache->node) < needed) {
	reiserfs_coord_t insert;
		
	if (reiserfs_tree_mkspace(tree, coord, &insert, needed))
	    return -1;

	*coord = insert;
    }
    
    if (reiserfs_cache_insert(coord->cache, &coord->pos, item)) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	   "Can't insert an %s into the node %llu.", 
	    (coord->pos.unit == ~0ul ? "item" : "unit"),
	    aal_block_get_nr(coord->cache->node->block));
	return -1;
    }

    return 0;
}

/* Removes item by specified key */
errno_t reiserfs_tree_remove(
    reiserfs_tree_t *tree,	/* tree item will be removed from */
    reiserfs_key_t *key		/* key item will be found by */
) {
    int lookup, level;
    reiserfs_coord_t coord;
    
    aal_assert("umka-1018", tree != NULL, return -1);
    aal_assert("umka-1019", key != NULL, return -1);
    
    /* Looking up for target */
    level = REISERFS_LEAF_LEVEL;
    
    if ((lookup = reiserfs_tree_lookup(tree, level, key, &coord)) == -1)
	return -1;

    if (lookup == 0) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Key (%llx:%x:%llx:%llx) doesn't found in tree.", 
	    reiserfs_key_get_locality(key), reiserfs_key_get_type(key),
	    reiserfs_key_get_objectid(key), reiserfs_key_get_offset(key));
	return -1;
    }
    
    if ((level = reiserfs_node_get_level(coord.cache->node)) > 
	REISERFS_LEAF_LEVEL + 1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Lookup stoped on invalid level %d.", level);
	return -1;
    }
   
    if (reiserfs_cache_remove(coord.cache, &coord.pos))
	return -1;

    /*
	FIXME-UMKA: Here should be also checking if we need descrease tree 
	height.
    */
    
    return 0;
}

/* 
    Moves item or unit from src coord to dst. Also it keeps track of change
    of root node in the tree.
*/
errno_t reiserfs_tree_move(
    reiserfs_tree_t *tree,	    /* tree we are operating on */
    reiserfs_coord_t *dst,	    /* dst coord */
    reiserfs_coord_t *src	    /* src coord */
) {
    aal_assert("umka-1020", tree != NULL, return -1);
    aal_assert("umka-1020", dst != NULL, return -1);
    aal_assert("umka-1020", src != NULL, return -1);
    

    if (reiserfs_cache_move(dst->cache, &dst->pos, 
	    src->cache, &src->pos))
	return -1;
	    
    /* 
	FIXME-UMKA: Here should be keeping track of the situation when we need 
	to change tree root.
    */
    return 0;
}

#endif

