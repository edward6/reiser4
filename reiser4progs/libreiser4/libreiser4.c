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
    return reiserfs_tree_insert((reiserfs_tree_t *)tree, item);
}

static errno_t __tree_remove(const void *tree, reiserfs_key_t *key) {
    return reiserfs_tree_remove((reiserfs_tree_t *)tree, key);
}

reiserfs_core_t core = {
    .factory_find = __factory_find,
    .tree_insert = __tree_insert,
    .tree_remove = __tree_remove
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

