/*
    libreiser4.c -- version control functions and library initialization code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

/* 
    Initializing the libreiser4 core instance. It will be passed into all 
    plugins in otder to let them ability access libreiser4 methods such as
    insert or remove an item from the tree.
*/

static inline reiserfs_plugin_t *__plugin_find(reiserfs_plugin_type_t type, reiserfs_id_t id) {
    return libreiser4_factory_find(type, id);
}

static inline errno_t __item_insert(const void *tree, reiserfs_item_hint_t *item) {
    aal_assert("umka-846", tree != NULL, return -1);
    aal_assert("umka-847", item != NULL, return -1);
    
    return reiserfs_tree_insert((reiserfs_tree_t *)tree, item);
}

static inline errno_t __item_remove(const void *tree, reiserfs_key_t *key) {
    aal_assert("umka-848", tree != NULL, return -1);
    aal_assert("umka-849", key != NULL, return -1);
    
    return reiserfs_tree_remove((reiserfs_tree_t *)tree, key);
}

static inline int __lookup(const void *tree, reiserfs_key_t *key, 
    reiserfs_place_t *place) 
{
    aal_assert("umka-851", key != NULL, return -1);
    aal_assert("umka-850", tree != NULL, return -1);
    aal_assert("umka-852", place != NULL, return -1);
    
    return reiserfs_tree_lookup((reiserfs_tree_t *)tree, 
	REISERFS_LEAF_LEVEL, key, (reiserfs_coord_t *)place);
}

static inline errno_t __item_body(const void *tree, reiserfs_place_t *place, 
    void **item, uint32_t *len) 
{
    reiserfs_node_t *node;
    
    aal_assert("umka-853", tree != NULL, return -1);
    aal_assert("umka-855", place != NULL, return -1);
    aal_assert("umka-856", item != NULL, return -1);
    
    node = ((reiserfs_cache_t *)place->cache)->node;
    
    *item = libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	item_body, node->entity, place->pos.item);
    
    if (len) {
	*len = libreiser4_plugin_call(return -1, node->node_plugin->node_ops,
	    item_len, node->entity, place->pos.item);
    }
    
    return 0;
}

static inline errno_t __item_right(const void *tree, 
    reiserfs_place_t *place) 
{
    reiserfs_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiserfs_cache_t *)place->cache; 
    
    if (reiserfs_cache_raise(cache) || !cache->right)
	return -1;

    place->cache = cache->right;
    place->pos.item = 0;
    place->pos.unit = 0;
    
    return 0;
}

static inline errno_t __item_left(const void *tree, 
    reiserfs_place_t *place) 
{
    reiserfs_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiserfs_cache_t *)place->cache; 
    
    if (reiserfs_cache_raise(cache) || !cache->left)
	return -1;

    place->cache = cache->left;
    place->pos.item = 0;
    place->pos.unit = 0;
    
    return 0;
}

static inline errno_t __item_key(const void *tree, 
    reiserfs_place_t *place, reiserfs_key_t *key) 
{
    aal_assert("umka-870", tree != NULL, return -1);
    aal_assert("umka-871", place != NULL, return -1);

    return reiserfs_node_get_key(((reiserfs_cache_t *)place->cache)->node, 
	place->pos.item, key);
}

static inline reiserfs_id_t __item_pid(const void *tree, 
    reiserfs_place_t *place, reiserfs_plugin_type_t type)
{
    aal_assert("umka-872", tree != NULL, return -1);
    aal_assert("umka-873", place != NULL, return -1);
    
    switch (type) {
	case REISERFS_ITEM_PLUGIN:
	    return reiserfs_node_item_get_pid(((reiserfs_cache_t *)place->cache)->node, 
		place->pos.item);
	case REISERFS_NODE_PLUGIN:
	    return reiserfs_node_get_pid(((reiserfs_cache_t *)place->cache)->node);
	default: {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Unknown plugin type %x.", type);
	    return REISERFS_INVAL_PLUGIN;
	}
    }
}

reiserfs_core_t core = {
    .factory_ops = {
	/* Installing callback for make search for a plugin by its attributes */
	.plugin_find = __plugin_find
    },
    
    .tree_ops = {
	/* This one for lookuping the tree */
	.lookup = __lookup,

	/* Installing callback function for inserting items into the tree */
	.item_insert = __item_insert,

	/* Installing callback function for removing items from the tree */
	.item_remove = __item_remove,

	/* 
	    And finally this one for getting body of some item and its size by passed 
	    coord.
	*/
	.item_body = __item_body,

	/* Returns key by coords */
	.item_key = __item_key,

	/* Returns right neighbour of passed coord */
	.item_right = __item_right,
    
	/* Returns left neighbour of passed coord */
	.item_left = __item_left,

	/* Returns tree pid by coord */
	.item_pid = __item_pid
    }
};

int libreiser4_get_max_interface_version(void) {
    return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiser4_get_min_interface_version(void) {
    return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiser4_get_version(void) {
    return VERSION;
}

errno_t libreiser4_init(void) {
    if (libreiser4_factory_init()) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, 
	    "Can't initialize plugin factory.");
	return -1;
    }
    return 0;
}

void libreiser4_done(void) {
    libreiser4_factory_done();
}

