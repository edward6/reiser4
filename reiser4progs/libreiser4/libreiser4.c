/*
    libreiser4.c -- version control functions and library initialization code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

/* 
    Initializing the libreiser4 core instance. It will be passed into all plugins 
    in otder to let them ability access libreiser4 methods such as insert or remove 
    an item from the tree.
*/

/* Handler for plugin finding requests from all plugins */
static inline reiserfs_plugin_t *__plugin_find(
    reiserfs_plugin_type_t type,    /* needed type of plugin*/
    reiserfs_id_t id		    /* needed plugin id */
) {
    return libreiser4_factory_find(type, id);
}

#ifndef ENABLE_COMPACT

/* Handler for item insert requests from the all plugins */
static inline errno_t __item_insert(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_item_hint_t *item	    /* item hint to be inserted into tree */
) {
    aal_assert("umka-846", tree != NULL, return -1);
    aal_assert("umka-847", item != NULL, return -1);
    
    return reiserfs_tree_insert((reiserfs_tree_t *)tree, item);
}

/* Handler for item removing requests from the all plugins */
static inline errno_t __item_remove(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_key_t *key		    /* key of the item to be removerd */
) {
    aal_assert("umka-848", tree != NULL, return -1);
    aal_assert("umka-849", key != NULL, return -1);
    
    return reiserfs_tree_remove((reiserfs_tree_t *)tree, key);
}

#endif

/* Handler for lookup reqiests from the all plugin can arrive */
static inline int __lookup(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_key_t *key,	    /* key to be found */
    reiserfs_place_t *place	    /* the same as reiserfs_coord_t;result will be stored in */
) {
    aal_assert("umka-851", key != NULL, return -1);
    aal_assert("umka-850", tree != NULL, return -1);
    aal_assert("umka-852", place != NULL, return -1);
    
    return reiserfs_tree_lookup((reiserfs_tree_t *)tree, 
	REISERFS_LEAF_LEVEL, key, (reiserfs_coord_t *)place);
}

/* Hanlder for item body requests arrive from the all plugins */
static inline errno_t __item_body(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_place_t *place,	    /* coords of the item */
    void **item,		    /* address where item body should be saved */
    uint32_t *len		    /* address where item length should be saved */
) {
    reiserfs_node_t *node;
    
    aal_assert("umka-853", tree != NULL, return -1);
    aal_assert("umka-855", place != NULL, return -1);
    aal_assert("umka-856", item != NULL, return -1);
    
    node = ((reiserfs_cache_t *)place->cache)->node;
    
    /* Getting item from the node */
    *item = libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	item_body, node->entity, place->pos.item);
    
    /* Getting item length from the node */
    if (len) {
	*len = libreiser4_plugin_call(return -1, node->plugin->node_ops,
	    item_len, node->entity, place->pos.item);
    }
    
    return 0;
}

/* Handler for requests for right neighbor */
static inline errno_t __item_right(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_place_t *place	    /* coord of node right neighbor will be obtained for */
) {
    reiserfs_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiserfs_cache_t *)place->cache; 
    
    /* Rasing from the device tree lies on both neighbors */
    if (reiserfs_cache_raise(cache) || !cache->right)
	return -1;

    /* Filling passed coord by right neighbor coords */
    place->cache = cache->right;
    place->pos.item = 0;
    place->pos.unit = 0;
    
    return 0;
}

/* Handler for requests for left neighbor */
static inline errno_t __item_left(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_place_t *place	    /* coord of node left neighbor will be obtained for */
) {
    reiserfs_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiserfs_cache_t *)place->cache; 
    
    /* Rasing from the device tree lies on both neighbors */
    if (reiserfs_cache_raise(cache) || !cache->left)
	return -1;

    /* Filling passed coord by left neighbor coords */
    place->cache = cache->left;
    place->pos.item = 0;
    place->pos.unit = 0;
    
    return 0;
}

/* Hanlder for returning item key */
static inline errno_t __item_key(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_place_t *place,	    /* coord of item key should be obtained from */
    reiserfs_key_t *key		    /* place key should be stored in */
) {
    aal_assert("umka-870", tree != NULL, return -1);
    aal_assert("umka-871", place != NULL, return -1);

    return reiserfs_node_get_key(((reiserfs_cache_t *)place->cache)->node, 
	place->pos.item, key);
}

/* Handler for plugin id requests */
static inline reiserfs_id_t __item_pid(
    const void *tree,		    /* opaque pointer to the tree */
    reiserfs_place_t *place,	    /* coord of item pid will be obtained from */
    reiserfs_plugin_type_t type	    /* requested plugin type */
) {
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

#ifndef ENABLE_COMPACT	
	/* Installing callback function for inserting items into the tree */
	.item_insert = __item_insert,

	/* Installing callback function for removing items from the tree */
	.item_remove = __item_remove,
#else
	.item_insert = NULL,
	.item_remove = NULL,
#endif
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

/* Returns libreiser4 max supported interface version */
int libreiser4_get_max_interface_version(void) {
    return LIBREISER4_MAX_INTERFACE_VERSION;
}

/* Returns libreiser4 min supported interface version */
int libreiser4_get_min_interface_version(void) {
    return LIBREISER4_MIN_INTERFACE_VERSION;
}

/* Returns libreiser4 version */
const char *libreiser4_get_version(void) {
    return VERSION;
}

static uint32_t mlimit = 0;

/* Returns current memory limit spent for tree cache */
uint32_t libreiser4_mlimit_get(void) {
    return mlimit;
}

/* Sets memory limit for tree cache */
void libreiser4_mlimit_set(uint32_t mem_limit) {
    mlimit = mem_limit;
}

/* 
    Initializes libreiser4 (plugin factory, memory limit, etc). This function 
    should be called before any actions performed on libreiser4.
*/
errno_t libreiser4_init(
    uint32_t mem_limit		/* memory limit in blocks tree cache may be size */
) {
    if (libreiser4_factory_init()) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, 
	    "Can't initialize plugin factory.");
	return -1;
    }

    mlimit = mem_limit;
    
    return 0;
}

/* Finalizes libreiser4 */
void libreiser4_done(void) {
    libreiser4_factory_done();
}

