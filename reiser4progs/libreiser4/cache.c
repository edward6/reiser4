/*
    cache.c -- reiserfs balanced tree cache functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

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
    return reiserfs_key_compare(&ldkey, key) == 0;
}

reiserfs_cache_t *reiserfs_cache_find(reiserfs_cache_t *cache, 
    reiserfs_key_t *key)
{
    aal_list_t *item;
    
    if (!cache->list)
	return NULL;
    
    if (!(item = aal_list_find_custom(cache->list, (void *)key, 
	    (int (*)(const void *, const void *, void *))
	    callback_comp_for_find, NULL)))
	return NULL;

    return (reiserfs_cache_t *)item->item;
}

static int callback_comp_for_register(reiserfs_cache_t *cache1, 
    reiserfs_cache_t *cache2, void *data) 
{
    reiserfs_key_t ldkey1, ldkey2;
    
    reiserfs_node_ldkey(cache1->node, &ldkey1);
    reiserfs_node_ldkey(cache2->node, &ldkey2);
    
    return reiserfs_key_compare(&ldkey1, &ldkey2) == 0;
}

static errno_t reiserfs_cache_nkey(reiserfs_cache_t *cache, 
    int direction, reiserfs_key_t *key) 
{
    int res;
    reiserfs_key_t ldkey;
    reiserfs_coord_t coord;
    
    aal_assert("umka-770", cache != NULL, return -1);
    aal_assert("umka-771", key != NULL, return -1);
    
    if (!cache->parent)
	return -1;
    
    if (reiserfs_node_ldkey(cache->node, &ldkey)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get left delimiting key of node %llu.", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }
    
    if (!(res = reiserfs_node_lookup(cache->parent->node, &ldkey, &coord.pos)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Lookup of registering node %llu failed.", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }
    
    if (res == 0) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find registering node %llu in parent.", 
	    aal_block_get_nr(cache->node->block));
	return -1;
    }
    
    if (direction == LEFT) {
	if (coord.pos.item == 0)
	    return -1;

	coord.pos.item--;
    } else {
	if (coord.pos.item == reiserfs_node_count(cache->parent->node) - 1)
	    return -1;
	
	coord.pos.item++;
    }
    
    reiserfs_key_init(key, reiserfs_node_item_key(cache->parent->node, 
	coord.pos.item), cache->node->key_plugin);
    
    return 0;
}

errno_t reiserfs_cache_lnkey(reiserfs_cache_t *cache, reiserfs_key_t *key) {
    return reiserfs_cache_nkey(cache, LEFT, key);
}

errno_t reiserfs_cache_rnkey(reiserfs_cache_t *cache, reiserfs_key_t *key) {
    return reiserfs_cache_nkey(cache, RIGHT, key);
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
    
    aal_assert("umka-561", cache != NULL, return -1);
    aal_assert("umka-564", child != NULL, return -1);
    
    cache->list = aal_list_insert_sorted(cache->list, 
	child, (int (*)(const void *, const void *, void *))
	callback_comp_for_register, NULL);

    left = cache->list->prev ? cache->list->prev->item : NULL;
    right = cache->list->next ? cache->list->next->item : NULL;
   
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
	    child->right = (reiserfs_key_compare(&rnkey, &ldkey) ? right : NULL);

	if (child->right)
	    child->right->left = child;
    }
    
    child->parent = cache;
    
    return 0;
}

/*
    Remove specified childern from the node. Updates all neighbour pointers and 
    parent pointer.
*/
void reiserfs_cache_unregister(reiserfs_cache_t *cache, 
    reiserfs_cache_t *child)
{
    aal_assert("umka-562", cache != NULL, return);
    aal_assert("umka-563", child != NULL, return);

    if (cache->list) {
	if (aal_list_length(aal_list_first(cache->list)) == 1) {
	    aal_list_remove(cache->list, child);
	    cache->list = NULL;
	} else
	    aal_list_remove(cache->list, child);
    }

    if (child->left)
	child->left->right = NULL;
    
    if (child->right)
	child->right->left = NULL;
    
    child->left = NULL;
    child->right = NULL;
    child->parent = NULL;
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
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize node %llu to device. %s.", 
	    aal_block_get_nr(cache->node->block), 
	    aal_device_error(cache->node->block->device));
	return -1;
    }
    
    return 0;
}

