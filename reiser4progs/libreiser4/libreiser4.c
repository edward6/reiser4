/*
    libreiser4.c -- version control functions and library initialization code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>
#include <printf.h>

/* 
    Initializing the libreiser4 core instance. It will be passed into all plugins 
    in otder to let them ability access libreiser4 methods such as insert or remove 
    an item from the tree.
*/

/* Handler for plugin finding requests from all plugins */
static inline reiser4_plugin_t *__plugin_ifind(
    rid_t type,			    /* needed type of plugin*/
    rid_t id			    /* needed plugin id */
) {
    return libreiser4_factory_ifind(type, id);
}

/* Handler for plugin finding requests from all plugins */
static inline reiser4_plugin_t *__plugin_nfind(
    rid_t type,			    /* needed type of plugin*/
    const char *name		    /* needed plugin name (label) */
) {
    return libreiser4_factory_nfind(type, name);
}

#ifndef ENABLE_COMPACT

/* Handler for item insert requests from the all plugins */
static inline errno_t __item_insert(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_item_hint_t *item	    /* item hint to be inserted into tree */
) {
    reiser4_coord_t coord;

    aal_assert("umka-846", tree != NULL, return -1);
    aal_assert("umka-847", item != NULL, return -1);
    
    return reiser4_tree_insert((reiser4_tree_t *)tree, item, &coord);
}

/* Handler for item removing requests from the all plugins */
static inline errno_t __item_remove(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_key_t *key		    /* key of the item to be removerd */
) {
    aal_assert("umka-848", tree != NULL, return -1);
    aal_assert("umka-849", key != NULL, return -1);
    
    return reiser4_tree_remove((reiser4_tree_t *)tree, key);
}

#endif

/* Handler for lookup reqiests from the all plugin can arrive */
static inline int __lookup(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_key_t *key,	    /* key to be found */
    reiser4_place_t *place	    /* the same as reiser4_coord_t;result will be stored in */
) {
    aal_assert("umka-851", key != NULL, return -1);
    aal_assert("umka-850", tree != NULL, return -1);
    aal_assert("umka-852", place != NULL, return -1);
    
    return reiser4_tree_lookup((reiser4_tree_t *)tree, 
	REISER4_LEAF_LEVEL, key, (reiser4_coord_t *)place);
}

/* Hanlder for item body requests arrive from the all plugins */
static inline errno_t __item_body(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_place_t *place,	    /* coords of the item */
    void **item,		    /* address where item body should be saved */
    uint32_t *len		    /* address where item length should be saved */
) {
    reiser4_node_t *node;
    
    aal_assert("umka-853", tree != NULL, return -1);
    aal_assert("umka-855", place != NULL, return -1);
    aal_assert("umka-856", item != NULL, return -1);
    
    node = ((reiser4_cache_t *)place->cache)->node;
    
    /* Getting item from the node */
    *item = plugin_call(return -1, node->entity->plugin->node_ops, 
	item_body, node->entity, &place->pos);
    
    /* Getting item length from the node */
    if (len) {
	*len = plugin_call(return -1, node->entity->plugin->node_ops,
	    item_len, node->entity, &place->pos);
    }
    
    return 0;
}

/* Handler for requests for right neighbor */
static inline errno_t __item_right(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_place_t *place	    /* coord of node right neighbor will be obtained for */
) {
    reiser4_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiser4_cache_t *)place->cache; 
    
    /* Rasing from the device tree lies on both neighbors */
    if (reiser4_cache_raise(cache) || !cache->right)
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
    reiser4_place_t *place	    /* coord of node left neighbor will be obtained for */
) {
    reiser4_cache_t *cache;
    
    aal_assert("umka-867", tree != NULL, return -1);
    aal_assert("umka-868", place != NULL, return -1);
    
    cache = (reiser4_cache_t *)place->cache; 
    
    /* Rasing from the device tree lies on both neighbors */
    if (reiser4_cache_raise(cache) || !cache->left)
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
    reiser4_place_t *place,	    /* coord of item key should be obtained from */
    reiser4_key_t *key		    /* place key should be stored in */
) {
    aal_assert("umka-870", tree != NULL, return -1);
    aal_assert("umka-871", place != NULL, return -1);

    return reiser4_node_get_key(((reiser4_cache_t *)place->cache)->node, 
	&place->pos, key);
}

/* Handler for plugin id requests */
static inline rid_t __item_pid(
    const void *tree,		    /* opaque pointer to the tree */
    reiser4_place_t *place,	    /* coord of item pid will be obtained from */
    reiser4_plugin_type_t type	    /* requested plugin type */
) {
    aal_assert("umka-872", tree != NULL, return -1);
    aal_assert("umka-873", place != NULL, return -1);
    
    switch (type) {
	case ITEM_PLUGIN_TYPE: {
	    reiser4_node_t *node;
	    
	    node = ((reiser4_cache_t *)place->cache)->node;
	    
	    return plugin_call(return -1, node->entity->plugin->node_ops, 
		item_pid, node->entity, &place->pos);
	}
	case NODE_PLUGIN_TYPE:
	    return reiser4_node_pid(((reiser4_cache_t *)place->cache)->node);
	    
	default:
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Unknown plugin type 0x%x.", type);
	    return INVALID_PLUGIN_ID;
    }
}

#ifndef ENABLE_COMPACT

/* Support for the %k occurences in the formated messages */
#define PA_REISER4_KEY  (PA_LAST)

static int _arginfo_k (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
        argtypes[0] = PA_REISER4_KEY | PA_FLAG_PTR;
    return 1;
}

static int __print_key(FILE * stream, const struct printf_info *info, 
    const void *const *args) 
{
    int len;
    char buffer[100];
    reiser4_key_t *key;

    aal_memset(buffer, 0, sizeof(buffer));
    
    key = *((reiser4_key_t **)(args[0]));
    reiser4_key_print(key, buffer, sizeof(buffer), 0);

    fprintf(stream, "%s", buffer);
    
    return aal_strlen(buffer);
}

#endif

reiser4_core_t core = {
    .factory_ops = {
	/* Installing callback for making search for a plugin by its type and id */
	.plugin_ifind = __plugin_ifind,
	
	/* Installing callback for making search for a plugin by its type and name */
	.plugin_nfind = __plugin_nfind,
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
int libreiser4_max_interface_version(void) {
    return LIBREISER4_MAX_INTERFACE_VERSION;
}

/* Returns libreiser4 min supported interface version */
int libreiser4_min_interface_version(void) {
    return LIBREISER4_MIN_INTERFACE_VERSION;
}

/* Returns libreiser4 version */
const char *libreiser4_version(void) {
    return VERSION;
}

/* 
    Initializes libreiser4 (plugin factory, memory limit, etc). This function 
    should be called before any actions performed on libreiser4.
*/
errno_t libreiser4_init(void) {
    if (libreiser4_factory_init()) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, 
	    "Can't initialize plugin factory.");
	return -1;
    }
    
#ifndef ENABLE_COMPACT
    register_printf_function ('k', __print_key, _arginfo_k);
#endif
    
    return 0;
}

/* Finalizes libreiser4 */
void libreiser4_done(void) {
    libreiser4_factory_done();
}

