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

static inline reiserfs_plugin_t *__factory_find(reiserfs_plugin_type_t type, reiserfs_id_t id) {
    return libreiser4_factory_find(type, id);
}

static inline errno_t __tree_insert(const void *tree, reiserfs_item_hint_t *item) {
    aal_assert("umka-846", tree != NULL, return -1);
    aal_assert("umka-847", item != NULL, return -1);
    
    return reiserfs_tree_insert((reiserfs_tree_t *)tree, item);
}

static inline errno_t __tree_remove(const void *tree, reiserfs_key_t *key) {
    aal_assert("umka-848", tree != NULL, return -1);
    aal_assert("umka-849", key != NULL, return -1);
    
    return reiserfs_tree_remove((reiserfs_tree_t *)tree, key);
}

static inline int __tree_lookup(const void *tree, reiserfs_key_t *key, 
    reiserfs_place_t *place) 
{
    aal_assert("umka-851", key != NULL, return -1);
    aal_assert("umka-850", tree != NULL, return -1);
    aal_assert("umka-852", place != NULL, return -1);
    
    return reiserfs_tree_lookup((reiserfs_tree_t *)tree, 
	REISERFS_LEAF_LEVEL, key, (reiserfs_coord_t *)place);
}

static inline errno_t __tree_data(const void *tree, reiserfs_place_t *place, 
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

static inline errno_t __tree_right(const void *tree, 
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

static inline errno_t __tree_left(const void *tree, 
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

static inline errno_t __tree_key(const void *tree, 
    reiserfs_place_t *place, reiserfs_key_t *key) 
{
    aal_assert("umka-870", tree != NULL, return -1);
    aal_assert("umka-871", place != NULL, return -1);

    return reiserfs_node_get_key(((reiserfs_cache_t *)place->cache)->node, 
	place->pos.item, key);
}

static inline reiserfs_id_t __tree_pid(const void *tree, 
    reiserfs_place_t *place)
{
    aal_assert("umka-872", tree != NULL, return -1);
    aal_assert("umka-873", place != NULL, return -1);

    return reiserfs_node_item_get_pid(((reiserfs_cache_t *)place->cache)->node, 
	place->pos.item);
}

reiserfs_core_t core = {
    /* Installing callback for make search for a plugin by its attributes */
    .factory_find = __factory_find,
    
    /* Installing callback function for inserting items into the tree */
    .tree_insert = __tree_insert,

    /* Installing callback function for removing items from the tree */
    .tree_remove = __tree_remove,

    /* This one for lookuping the tree */
    .tree_lookup = __tree_lookup,

    /* 
	And finally this one for getting body of some item and its size by passed 
	coord.
    */
    .tree_data = __tree_data,

    /* Returns key by coords */
    .tree_key = __tree_key,

    /* Returns right neighbour of passed coord */
    .tree_right = __tree_right,
    
    /* Returns left neighbour of passed coord */
    .tree_left = __tree_left,

    /* Returns tree pid by coord */
    .tree_pid = __tree_pid
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

