/*
    dir40.c -- reiser4 default directory object plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <sys/stat.h>
#endif

#include <reiser4/reiser4.h>
#include "dir40.h"

static reiserfs_core_t *core = NULL;

static oid_t dir40_objectid(reiserfs_dir40_t *dir) {
    aal_assert("umka-839", dir != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, dir->key.plugin->key_ops, 
	get_objectid, dir->key.body);
}

static errno_t dir40_rewind(reiserfs_dir40_t *dir) {
    
    if (core->tree_lookup(dir->tree, &dir->key, &dir->place) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find stat data of directory with oid %llx.", 
	    dir40_objectid(dir));
	return -1;
    }
    
    return 0;
}

/* This function grabbs the stat data of directory */
static errno_t dir40_realize(reiserfs_dir40_t *dir) {
    reiserfs_plugin_t *node_plugin;
    
    aal_assert("umka-840", dir->place.node != NULL, return -1);

    /* 
	FIXME-UMKA: Here should be not hardcoded node plugin id. Actually we can get it
	from node header, but this will not be work on reiser3 nodes.
    */
    if (!(node_plugin = core->factory_find(REISERFS_NODE_PLUGIN, 0x0)))
	libreiser4_factory_failed(return -1, find, node, 0x0);
    
    return ((dir->statdata = libreiser4_plugin_call(return -1, node_plugin->node_ops, 
	item_body, dir->place.node, dir->place.pos.item)) == NULL);
}

static reiserfs_dir40_t *dir40_open(const void *tree, 
    reiserfs_key_t *key) 
{
    reiserfs_dir40_t *dir;

    aal_assert("umka-836", tree != NULL, return NULL);
    aal_assert("umka-837", key != NULL, return NULL);
    aal_assert("umka-838", key->plugin != NULL, return NULL);
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    
    dir->key.plugin = key->plugin;
    aal_memcpy(dir->key.body, key->body, libreiser4_plugin_call(goto error_free_dir, 
	key->plugin->key_ops, size,));
    
    /* Positioning onto first directory unit */
    if (dir40_rewind(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't rewind directory with oid %llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    if (dir40_realize(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't grabb stat data of  directory with oid %llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    return dir;

error_free_dir:
    aal_free(dir);
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiserfs_dir40_t *dir40_create(const void *tree, 
    reiserfs_key_t *parent, reiserfs_key_t *object) 
{
    reiserfs_dir40_t *dir;
    reiserfs_item_hint_t item;
    reiserfs_stat_hint_t stat;
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *hash_plugin;
    reiserfs_direntry_hint_t direntry;
   
    reiserfs_id_t stat_pid;
    reiserfs_id_t direntry_pid;
    
    oid_t parent_objectid;
    oid_t parent_locality;
    oid_t objectid;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);
    aal_assert("umka-835", tree != NULL, return NULL);

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    
    key_plugin = object->plugin;

    /* FIXME-UMKA: Here should be not hardcoded hash plugin id */
    if (!(hash_plugin = core->factory_find(REISERFS_HASH_PLUGIN, REISERFS_R5_HASH)))
	libreiser4_factory_failed(goto error_free_dir, find, hash, REISERFS_R5_HASH);
    
    parent_objectid = libreiser4_plugin_call(return NULL, 
	key_plugin->key_ops, get_objectid, parent->body);
    
    parent_locality = libreiser4_plugin_call(return NULL, 
	key_plugin->key_ops, get_locality, parent->body);
    
    objectid = libreiser4_plugin_call(return NULL, 
	key_plugin->key_ops, get_objectid, object->body);
    
    aal_memset(&item, 0, sizeof(item));
    
    /* Initializing stat data hint */
    item.type = REISERFS_STATDATA_ITEM; 
   
    /* FIXME-UMKA: Here should not hardcoded stat data plugin id */
    stat_pid = REISERFS_STATDATA_ITEM;
    if (!(item.plugin = core->factory_find(REISERFS_ITEM_PLUGIN, stat_pid))) 
	libreiser4_factory_failed(goto error_free_dir, find, item, stat_pid);

    item.key.plugin = key_plugin;
    
    aal_memcpy(item.key.body, object->body, libreiser4_plugin_call(goto error_free_dir, 
	key_plugin->key_ops, size,));
    
    stat.mode = S_IFDIR | 0755;
    stat.extmask = 0;
    stat.nlink = 2;
    stat.size = 0;

    item.hint = &stat;

    /* Calling balancing code in order to insert statdata item into the tree */
    if (core->tree_insert(tree, &item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert stat data item of object %llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    aal_memset(&item, 0, sizeof(item));

    /* Initializing direntry hint */
    item.type = REISERFS_CDE_ITEM; 
    
    /* FIXME-UMKA: Here should be not hardcoded direntry plugin id */
    direntry_pid = REISERFS_CDE_ITEM;
    if (!(item.plugin = core->factory_find(REISERFS_ITEM_PLUGIN, direntry_pid))) 
	libreiser4_factory_failed(goto error_free_dir, find, item, direntry_pid);
    
    item.key.plugin = key_plugin; 
    aal_memcpy(item.key.body, parent, libreiser4_plugin_call(goto error_free_dir, 
	key_plugin->key_ops, size,));
    
    direntry.count = 2;
    direntry.key_plugin = key_plugin;
    direntry.hash_plugin = hash_plugin;
   
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key_ops, 
	build_entry_full, item.key.body, direntry.hash_plugin, 
	parent_objectid, objectid, ".");
    
    if (!(direntry.entry = aal_calloc(direntry.count * 
	    sizeof(reiserfs_entry_hint_t), 0)))
	goto error_free_dir;
    
    direntry.entry[0].locality = parent_objectid;
    direntry.entry[0].objectid = objectid;
    direntry.entry[0].name = ".";
    
    direntry.entry[1].locality = parent_locality;
    direntry.entry[1].objectid = parent_objectid;
    direntry.entry[1].name = "..";
    
    item.hint = &direntry;
    
    /* Inserting the direntry item into the tree */
    if (core->tree_insert(tree, &item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert direntry item of object %llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    aal_free(direntry.entry);
    
    dir->key.plugin = key_plugin;
    aal_memcpy(dir->key.body, object->body, libreiser4_plugin_call(goto error_free_dir, 
	key_plugin->key_ops, size,));
    
    /* Positioning onto first directory unit */
    if (dir40_rewind(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't rewind directory with oid %llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    if (dir40_realize(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't grabb stat data of  directory with oid %llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }

    return dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}

static void dir40_close(reiserfs_dir40_t *dir) {
    aal_assert("umka-750", dir != NULL, return);
    aal_free(dir);
}

#endif

static reiserfs_plugin_t dir40_plugin = {
    .dir_ops = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_DIR_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(const void *, reiserfs_key_t *, 
	    reiserfs_key_t *)) dir40_create,
	
	.check = NULL,
#else
	.create = NULL,
	.check = NULL,
#endif
	.open = (reiserfs_entity_t *(*)(const void *, reiserfs_key_t *))
	    dir40_open,

	.close = (void (*)(reiserfs_entity_t *))dir40_close,
	.confirm = NULL,
	.rewind = (errno_t (*)(reiserfs_entity_t *))dir40_rewind
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_core_t *c) {
    core = c;
    return &dir40_plugin;
}

libreiser4_factory_register(dir40_entry);

