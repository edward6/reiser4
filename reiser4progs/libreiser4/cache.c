/*
    cache.c -- reiserfs balanced tree cache functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

/* Macro for neighbors working functions */
#define LEFT (1)
#define RIGHT (0)

/* Function for creating cache instance */
reiserfs_cache_t *reiserfs_cache_create(
    reiserfs_node_t *node	/* node to be cached */
) {
    reiserfs_cache_t *cache;

    aal_assert("umka-797", node != NULL, return NULL);
    
    if (!(cache = aal_calloc(sizeof(*cache), 0)))
	return NULL;

    cache->node = node;
    return cache;
}

/* Frees pased cache instance */
void reiserfs_cache_close(
    reiserfs_cache_t *cache	/* cache instance to be closed */
) {
    aal_assert("umka-122", cache != NULL, return);
    
    /* Recurcive calling of the same function in order to free all childrens too */
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list)
	    reiserfs_cache_close((reiserfs_cache_t *)walk->item);

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
    
    reiserfs_node_close(cache->node);
    aal_free(cache);
}

/* Helper for comparing during finding in the cashe */
static int callback_comp_for_find(
    reiserfs_cache_t *cache,	/* cache find should be operate on */
    reiserfs_key_t *key,	/* key to be find */
    void *data			/* user-specified data */
) {
    reiserfs_key_t ldkey;
    
    reiserfs_node_ldkey(cache->node, &ldkey);
    return reiserfs_key_compare(&ldkey, key) == 0;
}

/* Finds children by its left delimiting key */
reiserfs_cache_t *reiserfs_cache_find(
    reiserfs_cache_t *cache,	/* cache to  be greped */
    reiserfs_key_t *key)	/* left delimiting key */
{
    aal_list_t *item;
    
    if (!cache->list)
	return NULL;
    
    /* Using aal_list find function */
    if (!(item = aal_list_find_custom(aal_list_first(cache->list), (void *)key, 
	    (int (*)(const void *, const void *, void *))
	    callback_comp_for_find, NULL)))
	return NULL;

    return (reiserfs_cache_t *)item->item;
}

/* Returns left or right neighbor key for passed cache */
static errno_t reiserfs_cache_nkey(
    reiserfs_cache_t *cache,	/* cahce for working with */
    int direction,		/* direction (left or right) */
    reiserfs_key_t *key		/* key pointer result should be stored */
) {
    reiserfs_pos_t pos;
    
    aal_assert("umka-770", cache != NULL, return -1);
    aal_assert("umka-771", key != NULL, return -1);
    
    if (reiserfs_cache_pos(cache, &pos))
	return -1;
    
    /* Checking for position */
    if (direction == LEFT) {
	if (pos.item == 0)
	    return -1;
    } else {
	/* Checking and proceccing the special case called "shaft" */
	if (pos.item == reiserfs_node_count(cache->parent->node) - 1) {

	    /* Here we are checking for the case called "shaft" */
    	    if (!cache->parent->parent)
		return -1;
		
	    return reiserfs_cache_nkey(cache->parent->parent, 
		direction, key);
	}
    }
    
    pos.item += (direction == RIGHT ? 1 : -1);
    reiserfs_node_get_key(cache->parent->node, &pos, key);
    
    return 0;
}

/* Wrapper for previous function. Returns left delimiting key for specified cache */
errno_t reiserfs_cache_lnkey(
    reiserfs_cache_t *cache,	/* cache for working with */
    reiserfs_key_t *key		/* pointer result will be stored in */
) {
    return reiserfs_cache_nkey(cache, LEFT, key);
}

/* The same as previous one */
errno_t reiserfs_cache_rnkey(
    reiserfs_cache_t *cache, 
    reiserfs_key_t *key
) {
    return reiserfs_cache_nkey(cache, RIGHT, key);
}

/* Returns position of passed cache in parent node */
errno_t reiserfs_cache_pos(
    reiserfs_cache_t *cache,	/* cache position will be obtained for */
    reiserfs_pos_t *pos		/* pointer result will be stored in */
) {
    reiserfs_key_t ldkey;
    
    aal_assert("umka-869", cache != NULL, return -1);
    
    if (!cache->parent)
	return -1;
    
    reiserfs_node_ldkey(cache->node, &ldkey);
    
    if (reiserfs_node_lookup(cache->parent->node, &ldkey, pos) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find left delimiting key of node %llu in its parent.", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }
    
    return 0;
}

/* 
    This function raises up both neighbours of the passed cache. This is used
    by shifting code in tree.c
*/
errno_t reiserfs_cache_raise(
    reiserfs_cache_t *cache	/* cache for working with */
) {
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
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find left neighbour key when raising left neigbour.");
		return -1;
	    }
	}
    }

    /* Raising the right neighbour */
    if (!cache->right) {
	if (!reiserfs_cache_rnkey(cache, &key)) {
	    if (reiserfs_tree_lookup(cache->tree, level, &key, &coord) != 1) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find right neighbour key when raising right neigbour.");
		return -1;
	    }
	}
    }
    
    return 0;
}

/* Helper function for registering in cache */
static int callback_comp_for_register(
    reiserfs_cache_t *cache1,	/* the first cache inetance for comparing */
    reiserfs_cache_t *cache2,	/* the second one */
    void *data			/* user-specified data */
) {
    reiserfs_key_t ldkey1, ldkey2;
    
    reiserfs_node_ldkey(cache1->node, &ldkey1);
    reiserfs_node_ldkey(cache2->node, &ldkey2);
    
    return reiserfs_key_compare(&ldkey1, &ldkey2);
}

/*
    Connects children into sorted children list of specified node. Sets up both
    neighbours and parent pointer.
*/
errno_t reiserfs_cache_register(
    reiserfs_cache_t *cache,	/* cache child will be connected to */
    reiserfs_cache_t *child	/* child cache for registering */
) {
    reiserfs_key_t ldkey;
    reiserfs_key_t lnkey, rnkey;
    reiserfs_cache_t *left, *right;
    reiserfs_cache_limit_t *limit;
    
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
	    child->left = (reiserfs_key_compare(&lnkey, &ldkey) == 0 ? left : NULL);
    
	if (child->left)
	    child->left->right = child;
    }
   
    if (right) {
	if (reiserfs_node_ldkey(right->node, &ldkey))
	    return -1;
	
	/* Getting right neighbour key */
	if (!reiserfs_cache_rnkey(child, &rnkey))
	    child->right = (reiserfs_key_compare(&rnkey, &ldkey) == 0 ? right : NULL);

	if (child->right)
	    child->right->left = child;
    }
    
    return 0;
}

/*
    Remove specified childern from the node. Updates all neighbour pointers and 
    parent pointer.
*/
void reiserfs_cache_unregister(
    reiserfs_cache_t *cache,	/* cache child will be deleted from */
    reiserfs_cache_t *child	/* pointer to child to be deleted */
) {
    reiserfs_cache_limit_t *limit;
    
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
errno_t reiserfs_cache_sync(
    reiserfs_cache_t *cache	/* cache to be synchronized */
) {
    aal_assert("umka-124", cache != NULL, return 0);
    
    /* 
	Walking through the list of childrens and calling reiserfs_cache_sync
	function for each element.
    */
    if (cache->list) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, cache->list) {
	    if (reiserfs_cache_sync((reiserfs_cache_t *)walk->item))
		return -1;
	}
    }
    
    /* Synchronizing cache itself */
    if (reiserfs_node_sync(cache->node)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize node %llu to device. %s.", 
	    aal_block_get_nr(cache->node->block), 
	    aal_device_error(cache->node->block->device));
	return -1;
    }
    
    return 0;
}

errno_t reiserfs_cache_set_key(reiserfs_cache_t *cache, 
    reiserfs_pos_t *pos, reiserfs_key_t *key)
{
    aal_assert("umka-999", cache != NULL, return -1);
    aal_assert("umka-1000", pos != NULL, return -1);
    aal_assert("umka-1001", key != NULL, return -1);
    
    aal_assert("umka-1002", 
	reiserfs_node_count(cache->node) > 0, return -1);
    
    if (reiserfs_node_set_key(cache->node, pos, key))
        return -1;
    
    if (cache->parent && pos->item == 0 && 
	pos->unit == ~0ul) 
    {
	reiserfs_pos_t p;

	if (reiserfs_cache_pos(cache, &p))
	    return -1;

	return reiserfs_cache_set_key(cache->parent, &p, key);
    }
    
    return 0;
}

/* 
    Inserts item or unit into cached node. Keeps track of changes of the left
    delimiting key.
*/
errno_t reiserfs_cache_insert(
    reiserfs_cache_t *cache,	    /* cache item will be inserted in */
    reiserfs_pos_t *pos,	    /* pos item will be inserted at */
    reiserfs_item_hint_t *item	    /* item hint to be inserted */
) {
    aal_assert("umka-990", cache != NULL, return -1);
    aal_assert("umka-991", pos != NULL, return -1);
    aal_assert("umka-992", item != NULL, return -1);

    /* Check if we need to update ldkey in parent cache */
    if (reiserfs_node_count(cache->node) > 0 && cache->parent && 
	pos->item == 0 && (pos->unit == 0 || pos->unit == ~0ul)) 
    {
	reiserfs_pos_t p;
	
	if (reiserfs_cache_pos(cache, &p))
	    return -1;
	
	if (reiserfs_cache_set_key(cache->parent, &p, &item->key))
	    return -1;
    }
    
    if (reiserfs_node_insert(cache->node, pos, item))
	return -1;

    if (pos->item == 0 && pos->unit == 0) {
	if (reiserfs_node_set_key(cache->node, pos, &item->key))
	    return -1;
    }
    
    return 0;
}

/* 
    Deletes item or unit from cached node. Keeps track of changes of the left
    delimiting key.
*/
errno_t reiserfs_cache_remove(
    reiserfs_cache_t *cache,	    /* cache item will be inserted in */
    reiserfs_pos_t *pos		    /* pos item will be inserted at */
) {
    int do_update;
    reiserfs_pos_t p;
    
    aal_assert("umka-993", cache != NULL, return -1);
    aal_assert("umka-994", pos != NULL, return -1);

    do_update = (reiserfs_node_count(cache->node) > 0 && 
	cache->parent && pos->item == 0 && (pos->unit == 0 || 
	pos->unit == ~0ul));
    
    if (do_update){
	if (reiserfs_cache_pos(cache, &p))
	    return -1;
    }
    
    if (reiserfs_node_remove(cache->node, pos))
	return -1;
    
    if (reiserfs_node_get_level(cache->node) > REISERFS_LEAF_LEVEL) {
        reiserfs_key_t key;
        reiserfs_cache_t *child;

        reiserfs_node_get_key(cache->node, pos, &key);
	
        if ((child = reiserfs_cache_find(cache, &key))) {
	    reiserfs_cache_unregister(cache, child);
	    reiserfs_cache_close(child);
	}
    }
    
    if (do_update) {
	if (reiserfs_node_count(cache->node) > 0) {
	    reiserfs_key_t ldkey;
	    
	    reiserfs_node_ldkey(cache->node, &ldkey);
	    
	    if (reiserfs_cache_set_key(cache->parent, &p, &ldkey))
		return -1;
	} else {
	    if (reiserfs_cache_remove(cache->parent, &p))
		return -1;

	    reiserfs_cache_unregister(cache->parent, cache);
	    reiserfs_cache_close(cache);
	}
    }

    return 0;
}

/* Moves item or unit from src cached node to dst one */
errno_t reiserfs_cache_move(
    reiserfs_cache_t *dst_cache,	/* destination cached node */
    reiserfs_pos_t *dst_pos,		/* destination pos */
    reiserfs_cache_t *src_cache,	/* source cached node */
    reiserfs_pos_t *src_pos		/* source pos */
) {
    reiserfs_pos_t sp;
    reiserfs_pos_t dp;
    
    aal_assert("umka-995", dst_cache != NULL, return -1);
    aal_assert("umka-996", dst_pos != NULL, return -1);
    aal_assert("umka-997", src_cache != NULL, return -1);
    aal_assert("umka-998", src_pos != NULL, return -1);
    
    /* Saving pos of ldkey in parent node for src node */
    if (src_pos->item == 0 && src_pos->unit == ~0ul &&
	src_cache->parent)
    {
	if (reiserfs_cache_pos(src_cache, &sp))
	    return -1;
    }
    
    /* Saving pos of ldkey in parent node for dst node */
    if (dst_pos->item == 0 && dst_pos->unit == ~0ul &&
	dst_cache->parent)
    {
	if (reiserfs_cache_pos(dst_cache, &dp))
	    return -1;
    }
    
    /* Updating cache pointers in the case of moving items on leaves level */
    if (reiserfs_node_get_level(src_cache->node) > 
	REISERFS_LEAF_LEVEL && src_pos->unit == ~0ul) 
    {
        reiserfs_key_t key;
        reiserfs_cache_t *child;

        reiserfs_node_get_key(src_cache->node, src_pos, &key);
	
        if ((child = reiserfs_cache_find(src_cache, &key))) {
	    reiserfs_cache_unregister(src_cache, child);
	    reiserfs_cache_register(dst_cache, child);
	}
    }
    
    /* Moving items */
    if (reiserfs_node_move(dst_cache->node, dst_pos, 
	    src_cache->node, src_pos))
	return -1;
    
    /* Updating ldkey in parent node for dst node */
    if (dst_pos->item == 0 && dst_pos->unit == ~0ul &&
	dst_cache->parent)
    {
	reiserfs_key_t ldkey;
	
	reiserfs_node_ldkey(dst_cache->node, &ldkey);
	
	if (reiserfs_cache_set_key(dst_cache->parent, &dp, &ldkey))
	    return -1;
    }

    /* Updating ldkey in parent node for src node */
    if (reiserfs_node_count(src_cache->node) > 0) {
	if (src_pos->item == 0 && src_pos->unit == ~0ul &&
	    src_cache->parent)
	{
	    reiserfs_key_t ldkey;
	
	    reiserfs_node_ldkey(src_cache->node, &ldkey);
	
	    if (reiserfs_cache_set_key(src_cache->parent, &sp, &ldkey))
		return -1;
	}
    } else {
	
	if (reiserfs_cache_remove(src_cache->parent, &sp))
	    return -1;

	reiserfs_cache_unregister(src_cache->parent, src_cache);
    }
    
    return 0;
}

#endif

