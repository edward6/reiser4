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

static reiserfs_plugin_t *__factory_find(reiserfs_plugin_type_t type, reiserfs_id_t id) {
    return libreiser4_factory_find(type, id);
}

static errno_t __tree_insert(const void *tree, reiserfs_item_hint_t *item) {
    aal_assert("umka-846", tree != NULL, return -1);
    aal_assert("umka-847", item != NULL, return -1);
    
    return reiserfs_tree_insert((reiserfs_tree_t *)tree, item);
}

static errno_t __tree_remove(const void *tree, reiserfs_key_t *key) {
    aal_assert("umka-848", tree != NULL, return -1);
    aal_assert("umka-849", key != NULL, return -1);
    
    return reiserfs_tree_remove((reiserfs_tree_t *)tree, key);
}

static int __tree_lookup(const void *tree, reiserfs_key_t *key, 
    reiserfs_place_t *place) 
{
    int lookup;
    reiserfs_coord_t coord;
    
    aal_assert("umka-850", tree != NULL, return -1);
    aal_assert("umka-851", key != NULL, return -1);
    aal_assert("umka-852", place != NULL, return -1);
    
    lookup = reiserfs_tree_lookup((reiserfs_tree_t *)tree, 
	REISERFS_LEAF_LEVEL, key, &coord);

    place->pos = coord.pos;
    place->node = coord.cache->node->entity;
    
    return lookup;
}

static errno_t __tree_data(const void *tree, reiserfs_place_t *place, 
    void **item, uint32_t *len) 
{
    reiserfs_plugin_t *node_plugin;
    
    aal_assert("umka-853", tree != NULL, return -1);
    aal_assert("umka-855", place != NULL, return -1);
    aal_assert("umka-856", item != NULL, return -1);
    
    /* FIXME-UMKA: Here should not be hardcoded node plugin id */
    if (!(node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, 0x0)))
	libreiser4_factory_failed(return -1, find, node, 0x0);
    
    *item = libreiser4_plugin_call(return -1, node_plugin->node_ops, 
	item_body, place->node, place->pos.item);
    
    *len = libreiser4_plugin_call(return -1, node_plugin->node_ops,
	item_len, place->node, place->pos.item);
    
    return 0;
}

reiserfs_core_t core = {
    .factory_find = __factory_find,
    
    .tree_insert = __tree_insert,
    .tree_remove = __tree_remove,
    .tree_lookup = __tree_lookup,
    .tree_data   = __tree_data
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

