/*
    cache.c -- reiser4 balanced tree cache functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

/* Macro for neighbors working functions */
#define LEFT (1)
#define RIGHT (0)

/* Function for creating cache instance */
reiser4_cache_t *reiser4_cache_create(
    reiser4_node_t *node	/* node to be cached */
) {
    reiser4_cache_t *cache;

    aal_assert("umka-797", node != NULL, return NULL);
    
    if (!(cache = aal_calloc(sizeof(*cache), 0)))
	return NULL;

    cache->node = node;
    return cache;
}

/* Frees pased cache instance */
void reiser4_cache_close(
    reiser4_cache_t *cache	/* cache instance to be closed */
) {
    aal_assert("umka-122", cache != NULL, return);
    
    /* Recurcive calling of the same function in order to free all childrens too */
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list)
	    reiser4_cache_close((reiser4_cache_t *)walk->item);

	aal_list_free(aal_list_first(cache->list));
	cache->list = NULL;
    }
    
    /* Uninitializing all fields */
    if (cache->left)
	cache->left->right = NULL;
    
    if (cache->right)
	cache->right->left = NULL;
    
    cache->left = NULL;
    cache->right = NULL;
    cache->parent = NULL;
    
    reiser4_node_close(cache->node);
    aal_free(cache);
}

/* Helper for comparing during finding in the cashe */
static int callback_comp_for_find(
    reiser4_cache_t *cache,	/* cache find should be operate on */
    reiser4_key_t *key,		/* key to be find */
    void *data			/* user-specified data */
) {
    reiser4_key_t ldkey;
    
    reiser4_node_lkey(cache->node, &ldkey);
    return reiser4_key_compare(&ldkey, key) == 0;
}

/* Finds children by its left delimiting key */
reiser4_cache_t *reiser4_cache_find(
    reiser4_cache_t *cache,	/* cache to  be greped */
    reiser4_key_t *key)		/* left delimiting key */
{
    aal_list_t *item;
    
    if (!cache->list)
	return NULL;
    
    /* Using aal_list find function */
    if (!(item = aal_list_find_custom(aal_list_first(cache->list), (void *)key, 
	    (int (*)(const void *, const void *, void *))
	    callback_comp_for_find, NULL)))
	return NULL;

    return (reiser4_cache_t *)item->item;
}

/* Returns left or right neighbor key for passed cache */
static errno_t reiser4_cache_nkey(
    reiser4_cache_t *cache,	/* cahce for working with */
    int direction,		/* direction (left or right) */
    reiser4_key_t *key		/* key pointer result should be stored */
) {
    reiser4_pos_t pos;
    
    aal_assert("umka-770", cache != NULL, return -1);
    aal_assert("umka-771", key != NULL, return -1);
    
    if (reiser4_cache_pos(cache, &pos))
	return -1;
    
    /* Checking for position */
    if (direction == LEFT) {
	if (pos.item == 0)
	    return -1;
    } else {
	/* Checking and proceccing the special case called "shaft" */
	if (pos.item == reiser4_node_count(cache->parent->node) - 1) {

	    /* Here we are checking for the case called "shaft" */
    	    if (!cache->parent->parent)
		return -1;
		
	    return reiser4_cache_nkey(cache->parent->parent, 
		direction, key);
	}
    }
    
    pos.item += (direction == RIGHT ? 1 : -1);
    reiser4_node_get_key(cache->parent->node, &pos, key);
    
    return 0;
}

/* Wrapper for previous function. Returns left delimiting key for specified cache */
errno_t reiser4_cache_lnkey(
    reiser4_cache_t *cache,	/* cache for working with */
    reiser4_key_t *key		/* pointer result will be stored in */
) {
    return reiser4_cache_nkey(cache, LEFT, key);
}

/* The same as previous one */
errno_t reiser4_cache_rnkey(
    reiser4_cache_t *cache, 
    reiser4_key_t *key
) {
    return reiser4_cache_nkey(cache, RIGHT, key);
}

/* Returns position of passed cache in parent node */
errno_t reiser4_cache_pos(
    reiser4_cache_t *cache,	/* cache position will be obtained for */
    reiser4_pos_t *pos		/* pointer result will be stored in */
) {
    reiser4_key_t ldkey;
    
    aal_assert("umka-869", cache != NULL, return -1);
    
    if (!cache->parent)
	return -1;
    
    reiser4_node_lkey(cache->node, &ldkey);
    
    if (reiser4_node_lookup(cache->parent->node, &ldkey, pos) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find left delimiting key of node %llu in its parent.", 
	    aal_block_number(cache->node->block));
	return -1;
    }
    
    return 0;
}

/* 
    This function raises up both neighbours of the passed cache. This is used
    by shifting code in tree.c
*/
errno_t reiser4_cache_raise(
    reiser4_cache_t *cache	/* cache for working with */
) {
    uint32_t level;
    reiser4_key_t key;
    reiser4_coord_t coord;
    
    aal_assert("umka-776", cache != NULL, return -1);

    if (!cache->parent)
	return 0;
    
    /* 
	Initializing stop level for tree lookup function. Here tree lookup function is
	used as instrument for reflecting the part of tree into libreiser4 tree cache.
	So, connecting to the stop level for lookup we need to map part of the tree
	from the root (tree height) to the level of passed node, because we should make
	sure, that needed neighbour will be mapped into cache and will be accesible by
	cache->left or cache->right pointers.
    */
    level = cache->level;
    
    /* Rasing the right neighbour */
    if (!cache->left) {
	if (!reiser4_cache_lnkey(cache, &key)) {
	    if (reiser4_tree_lookup(cache->tree, level, &key, &coord) != 1) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find left neighbour key when raising left neigbour.");
		return -1;
	    }
	}
    }

    /* Raising the right neighbour */
    if (!cache->right) {
	if (!reiser4_cache_rnkey(cache, &key)) {
	    if (reiser4_tree_lookup(cache->tree, level, &key, &coord) != 1) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find right neighbour key when raising right neigbour.");
		return -1;
	    }
	}
    }
    
    return 0;
}

/* Helper function for registering in cache */
static int callback_comp_cache(
    const void *item1,		/* the first cache inetance for comparing */
    const void *item2,		/* the second one */
    void *data			/* user-specified data */
) {
    reiser4_key_t ldkey1, ldkey2;
    reiser4_cache_t *cache1 = (reiser4_cache_t *)item1;
    reiser4_cache_t *cache2 = (reiser4_cache_t *)item2;
    
    reiser4_node_lkey(cache1->node, &ldkey1);
    reiser4_node_lkey(cache2->node, &ldkey2);
    
    return reiser4_key_compare(&ldkey1, &ldkey2);
}

/*
    Connects children into sorted children list of specified node. Sets up both
    neighbours and parent pointer.
*/
errno_t reiser4_cache_register(
    reiser4_cache_t *cache,	/* cache child will be connected to */
    reiser4_cache_t *child	/* child cache for registering */
) {
    reiser4_key_t ldkey;
    reiser4_key_t lnkey, rnkey;
    reiser4_cache_t *left, *right;
    reiser4_cache_limit_t *limit;
    
    aal_assert("umka-561", cache != NULL, return -1);
    aal_assert("umka-564", child != NULL, return -1);
    
    limit = &cache->tree->limit;
    
    /* 
	Checking for limit exceeding. If limit has exceeded, flushing should be
	performed.
    */
    if (limit->enabled) {
	if ((uint32_t)(limit->cur + 1) > limit->max) {
	    aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
		"Cache limit has been exceeded (current: %d, allowed: %u). "
		"Flushing should be run.", limit->cur, limit->max);
	}
	limit->cur++;
    }
    
    /* Inserting passed cache into right position */
    {
	aal_list_t *list = cache->list ? aal_list_first(cache->list) : NULL;

	cache->list = aal_list_insert_sorted(list, child, 
	    callback_comp_cache, NULL);
    }
    
    left = cache->list->prev ? cache->list->prev->item : NULL;
    right = cache->list->next ? cache->list->next->item : NULL;
   
    child->parent = cache;
    child->tree = cache->tree;
    
    /* Setting up neighbours */
    if (left) {
	if (reiser4_node_lkey(left->node, &ldkey))
	    return -1;
	
	/* Getting left neighbour key */
	if (!reiser4_cache_lnkey(child, &lnkey))
	    child->left = (reiser4_key_compare(&lnkey, &ldkey) == 0 ? left : NULL);
    
	if (child->left)
	    child->left->right = child;
    }
   
    if (right) {
	if (reiser4_node_lkey(right->node, &ldkey))
	    return -1;
	
	/* Getting right neighbour key */
	if (!reiser4_cache_rnkey(child, &rnkey))
	    child->right = (reiser4_key_compare(&rnkey, &ldkey) == 0 ? right : NULL);

	if (child->right)
	    child->right->left = child;
    }
    
    return 0;
}

/*
    Remove specified childern from the node. Updates all neighbour pointers and 
    parent pointer.
*/
void reiser4_cache_unregister(
    reiser4_cache_t *cache,	/* cache child will be deleted from */
    reiser4_cache_t *child	/* pointer to child to be deleted */
) {
    reiser4_cache_limit_t *limit;
    
    aal_assert("umka-562", cache != NULL, return);
    aal_assert("umka-563", child != NULL, return);

    limit = &cache->tree->limit;

    if (limit->enabled)
	aal_assert("umka-858", limit->cur > 0, return);

    /* Deleteing passed child from children list of specified cache */
    if (cache->list) {
	uint32_t count = aal_list_length(aal_list_first(cache->list));
	
	if (child->left)
	    child->left->right = NULL;
    
	if (child->right)
	    child->right->left = NULL;
    
	child->left = NULL;
	child->right = NULL;
	child->parent = NULL;
	child->tree = NULL;
	
	aal_list_remove(cache->list, child);
	
	if (count == 1)
	    cache->list = NULL;
    
	if (limit->enabled)
	    limit->cur--;
    }
}

#ifndef ENABLE_COMPACT

/*
    Synchronizes passed cache by using resursive pass though all childrens. This
    method will be used when memory pressure occurs. There is possible to pass
    as parameter of this function the root cache pointer. In this case the whole
    tree cache will be flushed onto device, tree lies on.
*/
errno_t reiser4_cache_sync(
    reiser4_cache_t *cache	/* cache to be synchronized */
) {
    aal_assert("umka-124", cache != NULL, return 0);
    
    /* 
	Walking through the list of childrens and calling reiser4_cache_sync
	function for each element.
    */
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list) {
	    if (reiser4_cache_sync((reiser4_cache_t *)walk->item))
		return -1;
	}
    }
    
    /* Synchronizing cache itself */
    if (reiser4_node_sync(cache->node)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize node %llu to device. %s.", 
	    aal_block_number(cache->node->block), 
	    aal_device_error(cache->node->block->device));
	return -1;
    }
    
    return 0;
}

errno_t reiser4_cache_set_key(reiser4_cache_t *cache, 
    reiser4_pos_t *pos, reiser4_key_t *key)
{
    aal_assert("umka-999", cache != NULL, return -1);
    aal_assert("umka-1000", pos != NULL, return -1);
    aal_assert("umka-1001", key != NULL, return -1);
    
    aal_assert("umka-1002", 
	reiser4_node_count(cache->node) > 0, return -1);
    
    if (reiser4_node_set_key(cache->node, pos, key))
        return -1;
    
    if (cache->parent && pos->item == 0 && 
	(pos->unit == ~0ul || pos->unit == 0)) 
    {
	reiser4_pos_t p;

	if (reiser4_cache_pos(cache, &p))
	    return -1;

	return reiser4_cache_set_key(cache->parent, &p, key);
    }
    
    return 0;
}

/* 
    Inserts item or unit into cached node. Keeps track of changes of the left
    delimiting key.
*/
errno_t reiser4_cache_insert(
    reiser4_cache_t *cache,	    /* cache item will be inserted in */
    reiser4_pos_t *pos,	    	    /* pos item will be inserted at */
    reiser4_item_hint_t *hint	    /* item hint to be inserted */
) {
    aal_assert("umka-990", cache != NULL, return -1);
    aal_assert("umka-991", pos != NULL, return -1);
    aal_assert("umka-992", hint != NULL, return -1);

    /* Check if we need to update ldkey in parent cache */
    if (reiser4_node_count(cache->node) > 0 && cache->parent && 
	pos->item == 0 && (pos->unit == 0 || pos->unit == ~0ul)) 
    {
	reiser4_pos_t p;
	
	if (reiser4_cache_pos(cache, &p))
	    return -1;
	
	if (reiser4_cache_set_key(cache->parent, &p, &hint->key))
	    return -1;
    }

    /* Inserting item */
    if (reiser4_node_insert(cache->node, pos, hint))
	return -1;

    /* Updating ldkey in parent cache */
    if (pos->item == 0 && (pos->unit == 0 || pos->unit == ~0ul)) {
	if (reiser4_node_set_key(cache->node, pos, &hint->key))
	    return -1;
    }
    
    return 0;
}

/* 
    Deletes item or unit from cached node. Keeps track of changes of the left
    delimiting key.
*/
errno_t reiser4_cache_remove(
    reiser4_cache_t *cache,	    /* cache item will be inserted in */
    reiser4_pos_t *pos		    /* pos item will be inserted at */
) {
    int update;
    reiser4_pos_t p;
    
    aal_assert("umka-993", cache != NULL, return -1);
    aal_assert("umka-994", pos != NULL, return -1);

    /* Getting position in parent cached node */
    update = (cache->parent && pos->item == 0 && 
	(pos->unit == 0 || pos->unit == ~0ul));
    
    if ((update = (cache->parent && pos->item == 0 && 
	(pos->unit == 0 || pos->unit == ~0ul)))) 
    {
	if (reiser4_cache_pos(cache, &p))
	    return -1;
    }
    
    /* Removing item or unit */
    if (reiser4_node_remove(cache->node, pos))
	return -1;
    
    /* 
	Updating list of childrens of modified node in the case we modifying an 
	internal node.
    */
    if (cache->level > REISER4_LEAF_LEVEL) {
        reiser4_key_t key;
        reiser4_cache_t *child;

        reiser4_node_get_key(cache->node, pos, &key);
	
        if ((child = reiser4_cache_find(cache, &key))) {
	    reiser4_cache_unregister(cache, child);
	    reiser4_cache_close(child);
	}
    }
    
    /* Updating left deleimiting key in all parent nodes */
    if (update) {
	if (reiser4_node_count(cache->node) > 0) {
	    reiser4_key_t ldkey;
	    
	    reiser4_node_lkey(cache->node, &ldkey);
	    
	    if (reiser4_cache_set_key(cache->parent, &p, &ldkey))
		return -1;
	    
	} else {

	    /* 
		Removing cached node from the tree in the case it has not items 
		anymore.
	    */
	    if (reiser4_cache_remove(cache->parent, &p))
		return -1;

	    reiser4_alloc_release(cache->tree->fs->alloc,
		aal_block_number(cache->node->block));
	    
	    reiser4_cache_unregister(cache->parent, cache);
	    reiser4_cache_close(cache);
	}
    }

    return 0;
}

/* Moves item or unit from src cached node to dst one */
errno_t reiser4_cache_move(
    reiser4_cache_t *dst_cache,	/* destination cached node */
    reiser4_pos_t *dst_pos,		/* destination pos */
    reiser4_cache_t *src_cache,	/* source cached node */
    reiser4_pos_t *src_pos		/* source pos */
) {
    reiser4_pos_t sp;
    reiser4_pos_t dp;
    
    aal_assert("umka-995", dst_cache != NULL, return -1);
    aal_assert("umka-996", dst_pos != NULL, return -1);
    aal_assert("umka-997", src_cache != NULL, return -1);
    aal_assert("umka-998", src_pos != NULL, return -1);
    
    /* Saving pos of ldkey in parent node for src node */
    if (src_cache->parent && src_pos->item == 0 && 
	(src_pos->unit == 0 || src_pos->unit == ~0ul))
    {
	if (reiser4_cache_pos(src_cache, &sp))
	    return -1;
    }
    
    /* Saving pos of ldkey in parent node for dst node */
    if (dst_cache->parent && dst_pos->item == 0 && 
	(dst_pos->unit == ~0ul || dst_pos->unit == 0) &&
	reiser4_node_count(dst_cache->node) > 0)
    {
	if (reiser4_cache_pos(dst_cache, &dp))
	    return -1;
    }
    
    /* Updating cache pointers in the case of moving items on leaves level */
    if (src_cache->level > REISER4_LEAF_LEVEL) {
        reiser4_key_t key;
        reiser4_cache_t *child;

        reiser4_node_get_key(src_cache->node, src_pos, &key);
	
        if ((child = reiser4_cache_find(src_cache, &key))) {
	    reiser4_cache_unregister(src_cache, child);
	    reiser4_cache_register(dst_cache, child);
	}
    }
    
    /* Moving items */
    if (reiser4_node_move(dst_cache->node, dst_pos, 
	    src_cache->node, src_pos))
	return -1;
    
    /* Updating ldkey in parent node for dst node */
    if (dst_cache->parent && dst_pos->item == 0 && 
	(dst_pos->unit == ~0ul || dst_pos->unit == 0))
    {
	reiser4_key_t ldkey;
	
	reiser4_node_lkey(dst_cache->node, &ldkey);
	
	if (reiser4_node_count(dst_cache->node) > 1) {
		
	    if (reiser4_cache_set_key(dst_cache->parent, &dp, &ldkey))
		return -1;
	    
	} else {
	    if (reiser4_node_lookup(dst_cache->parent->node, &ldkey, &dp) == -1)
	        return -1;

	    dp.item--;
	    
	    if (reiser4_cache_set_key(dst_cache->parent, &dp, &ldkey))
	        return -1;
	}
    }

    /* Updating ldkey in parent node for src node */
    if (reiser4_node_count(src_cache->node) > 0) {
	if (src_cache->parent && src_pos->item == 0 && 
	    (src_pos->unit == ~0ul || src_pos->unit == 0))
	{
	    reiser4_key_t ldkey;
	
	    reiser4_node_lkey(src_cache->node, &ldkey);
	
	    if (reiser4_cache_set_key(src_cache->parent, &sp, &ldkey))
		return -1;
	}
    } else {
	
	if (reiser4_cache_remove(src_cache->parent, &sp))
	    return -1;

	reiser4_cache_unregister(src_cache->parent, src_cache);
    }
    
    return 0;
}

#endif

