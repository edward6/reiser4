/*
    cache.c -- reiserfs balanced tree cache functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

#define LEFT (1)
#define RIGHT (0)

reiserfs_cache_t *reiserfs_cache_create(reiserfs_node_t *node) {
    reiserfs_cache_t *cache;

    aal_assert("umka-797", node != NULL, return NULL);
    
    if (!(cache = aal_calloc(sizeof(*cache), 0)))
	return NULL;

    cache->node = node;
    return cache;
}

void reiserfs_cache_close(reiserfs_cache_t *cache) {
    aal_assert("umka-122", cache != NULL, return);
    
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list)
	    reiserfs_cache_close((reiserfs_cache_t *)walk->item);

	aal_list_free(cache->list);
	cache->list = NULL;
    }
    
    if (cache->left)
	cache->left->right = NULL;
    
    if (cache->right)
	cache->right->left = NULL;
    
    cache->left = NULL;
    cache->right = NULL;
    cache->parent = NULL;
    
    aal_free(cache);
}

static int callback_comp_for_find(reiserfs_cache_t *cache, 
    reiserfs_key_t *key, void *data)
{
    reiserfs_key_t ldkey;
    
    reiserfs_node_ldkey(cache->node, &ldkey);
    return reiserfs_key_compare_full(&ldkey, key) == 0;
}

reiserfs_cache_t *reiserfs_cache_find(reiserfs_cache_t *cache, 
    reiserfs_key_t *key)
{
    aal_list_t *item;
    
    if (!cache->list)
	return NULL;
    
    if (!(item = aal_list_find_custom(aal_list_first(cache->list), (void *)key, 
	    (int (*)(const void *, const void *, void *))
	    callback_comp_for_find, NULL)))
	return NULL;

    return (reiserfs_cache_t *)item->item;
}

static errno_t reiserfs_cache_nkey(reiserfs_cache_t *cache, 
    int direction, reiserfs_key_t *key) 
{
    reiserfs_pos_t pos;
    
    aal_assert("umka-770", cache != NULL, return -1);
    aal_assert("umka-771", key != NULL, return -1);
    
    if (reiserfs_cache_pos(cache, &pos))
	return -1;
    
    if (direction == LEFT) {
	if (pos.item == 0)
	    return -1;
    } else {
	if (pos.item == reiserfs_node_count(cache->parent->node) - 1) {

	    /* Here we are checking for the case called "shaft" */
    	    if (!cache->parent->parent)
		return -1;
		
	    return reiserfs_cache_nkey(cache->parent->parent, 
		direction, key);
	}
    }
    pos.item += (direction == RIGHT ? 1 : -1);
    reiserfs_node_get_key(cache->parent->node, pos.item, key);
    
    return 0;
}

errno_t reiserfs_cache_lnkey(reiserfs_cache_t *cache, 
    reiserfs_key_t *key) 
{
    return reiserfs_cache_nkey(cache, LEFT, key);
}

errno_t reiserfs_cache_rnkey(reiserfs_cache_t *cache, 
    reiserfs_key_t *key) 
{
    return reiserfs_cache_nkey(cache, RIGHT, key);
}

/* Returns position of passed cache in parent node */
errno_t reiserfs_cache_pos(reiserfs_cache_t *cache, 
    reiserfs_pos_t *pos) 
{
    reiserfs_key_t ldkey;
    
    aal_assert("umka-869", cache != NULL, return -1);
    
    if (!cache->parent)
	return -1;
    
    reiserfs_node_ldkey(cache->node, &ldkey);
    
    if (reiserfs_node_lookup(cache->parent->node, &ldkey, pos) != 1) {
	aal_throw_error(EO_OK, "Can't find left delimiting key of node %llu.\n", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }
    
    return 0;
}

/* 
    This function raises up both neighbours of the passed cache. This is used
    by shifting code in tree.c
*/
errno_t reiserfs_cache_raise(reiserfs_cache_t *cache) {
    uint32_t level;
    reiserfs_key_t key;
    reiserfs_coord_t coord;
    
    aal_assert("umka-776", cache != NULL, return -1);

    if (!cache->parent)
	return 0;
    
    /* 
	Initializing stop level for tree lookup function. Here tree lookup function is
	used as instrument for reflecting the part of b*tree into libreiser4 tree cache.
	So, connecting to the stop level for lookup we need to map part of the b*tree
	from the root (tree height) to the level of passed node, because we should make
	sure, that needed neighbour will be mapped into cache and will be accesible by
	cache->left or cache->right pointers.
    */
    level = reiserfs_node_get_level(cache->node);
    
    /* Rasing the right neighbour */
    if (!cache->left) {
	if (!reiserfs_cache_lnkey(cache, &key)) {
	    if (reiserfs_tree_lookup(cache->tree, level, &key, &coord) != 1) {
		aal_throw_error(EO_OK, "Can't find left neighbour key when raising "
		    "left neigbour.\n");
		return -1;
	    }
	}
    }

    /* Raising the right neighbour */
    if (!cache->right) {
	if (!reiserfs_cache_rnkey(cache, &key)) {
	    if (reiserfs_tree_lookup(cache->tree, level, &key, &coord) != 1) {
		aal_throw_error(EO_OK, "Can't find right neighbour key when raising "
		    "right neigbour.\n");
		return -1;
	    }
	}
    }
    
    return 0;
}

static int callback_comp_for_register(reiserfs_cache_t *cache1, 
    reiserfs_cache_t *cache2, void *data) 
{
    reiserfs_key_t ldkey1, ldkey2;
    
    reiserfs_node_ldkey(cache1->node, &ldkey1);
    reiserfs_node_ldkey(cache2->node, &ldkey2);
    
    return reiserfs_key_compare_full(&ldkey1, &ldkey2);
}

/*
    Connects children into sorted children list of specified node. Sets up both
    neighbours and parent pointer.
*/
errno_t reiserfs_cache_register(reiserfs_cache_t *cache, 
    reiserfs_cache_t *child) 
{
    reiserfs_key_t ldkey;
    reiserfs_key_t lnkey, rnkey;
    reiserfs_cache_t *left, *right;
    reiserfs_cache_limit_t *limit;
    
    aal_assert("umka-561", cache != NULL, return -1);
    aal_assert("umka-564", child != NULL, return -1);
    
    limit = &cache->tree->limit;
    
    if (limit->enabled) {
	if ((uint32_t)(limit->cur + 1) > limit->max) {
	    aal_throw_warning(EO_OK, "Cache limit has been exceeded (current: %d, "
		"allowed: %u). Flushing should be run.\n", limit->cur, limit->max);
	}
	limit->cur++;
    }
    
    cache->list = aal_list_insert_sorted(cache->list ? aal_list_first(cache->list) : NULL, 
	child, (int (*)(const void *, const void *, void *))
	callback_comp_for_register, NULL);

    left = cache->list->prev ? cache->list->prev->item : NULL;
    right = cache->list->next ? cache->list->next->item : NULL;
   
    child->parent = cache;
    child->tree = cache->tree;
    
    /* Setting up neighbours */
    if (left) {
	if (reiserfs_node_ldkey(left->node, &ldkey))
	    return -1;
	
	/* Getting left neighbour key */
	if (!reiserfs_cache_lnkey(child, &lnkey))
	    child->left = (reiserfs_key_compare_full(&lnkey, &ldkey) == 0 ? left : NULL);
    
	if (child->left)
	    child->left->right = child;
    }
   
    if (right) {
	if (reiserfs_node_ldkey(right->node, &ldkey))
	    return -1;
	
	/* Getting right neighbour key */
	if (!reiserfs_cache_rnkey(child, &rnkey))
	    child->right = (reiserfs_key_compare_full(&rnkey, &ldkey) == 0 ? right : NULL);

	if (child->right)
	    child->right->left = child;
    }
    
    return 0;
}

/*
    Remove specified childern from the node. Updates all neighbour pointers and 
    parent pointer.
*/
void reiserfs_cache_unregister(reiserfs_cache_t *cache, 
    reiserfs_cache_t *child)
{
    reiserfs_cache_limit_t *limit;
    
    aal_assert("umka-562", cache != NULL, return);
    aal_assert("umka-563", child != NULL, return);

    limit = &cache->tree->limit;

    if (limit->enabled) {
	/* Current number of blocks should be always positive or equal zero */
	aal_assert("umka-858", limit->cur > 0, return);
    }

    if (cache->list) {
	if (aal_list_length(aal_list_first(cache->list)) == 1) {
	    aal_list_remove(cache->list, child);
	    cache->list = NULL;
	} else
	    aal_list_remove(cache->list, child);
    }
    
    if (limit->enabled)
	limit->cur--;

    if (child->left)
	child->left->right = NULL;
    
    if (child->right)
	child->right->left = NULL;
    
    child->left = NULL;
    child->right = NULL;
    child->parent = NULL;
    child->tree = NULL;
}

errno_t reiserfs_cache_sync(reiserfs_cache_t *cache) {
    aal_assert("umka-124", cache != NULL, return 0);
    
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list) {
	    if (reiserfs_cache_sync((reiserfs_cache_t *)walk->item))
		return -1;
	}
    }
    
    if (reiserfs_node_sync(cache->node)) {
	aal_throw_error(EO_OK, "Can't synchronize node %llu to device. %s.\n", 
	    aal_block_get_nr(cache->node->block), 
	    aal_device_error(cache->node->block->device));
	return -1;
    }
    
    return 0;
}

