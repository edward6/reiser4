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

static reiserfs_plugin_factory_t *factory = NULL;

#ifndef ENABLE_COMPACT

/*
    FIXME-UMKA: Is it possible will be exist objects without 
    stat data? If so, we need to throw out stat_plugin_id
    from accepted parameters.
*/
static reiserfs_object_hint_t *dir40_build(reiserfs_key_t *parent, 
    reiserfs_key_t *object, uint16_t stat_plugin_id, uint16_t direntry_plugin_id) 
{
    reiserfs_object_hint_t *hint;
    reiserfs_item_hint_t *item_hint;

    reiserfs_entry_hint_t **entry;
    reiserfs_stat_hint_t *stat_hint;
    reiserfs_direntry_hint_t *direntry_hint;

    reiserfs_plugin_t *key_plugin;
    
    oid_t parent_objectid;
    oid_t parent_locality;
    oid_t objectid;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);

    key_plugin = object->plugin;
    
    parent_objectid = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_objectid, parent->body);
    
    parent_locality = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_locality, parent->body);
    
    objectid = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_objectid, object->body);
    
    if (!(hint = aal_calloc(sizeof(*hint), 0)))
	return NULL;

    hint->count = 2;
    
    if (!(hint->item = aal_calloc(2 * sizeof(reiserfs_item_hint_t *), 0)))
	goto error_free_hint;
    
    /* Initializing stat data hint */
    if (!(hint->item[0] = aal_calloc(sizeof(reiserfs_item_hint_t), 0)))
	goto error_free_item;
    
    hint->item[0]->type = REISERFS_STAT_ITEM; 
    
    if (!(hint->item[0]->plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN,
	stat_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, stat_plugin_id,
	    goto error_free_item0);
    }

    if (!(hint->item[0]->hint = aal_calloc(sizeof(reiserfs_stat_hint_t), 0)))
	goto error_free_item0;
   
    hint->item[0]->key.plugin = key_plugin; 
    aal_memcpy(&hint->item[0]->key.body, object->body, sizeof(object->body));
    stat_hint = hint->item[0]->hint;

    stat_hint->mode = S_IFDIR | 0755;
    stat_hint->extmask = 0;
    stat_hint->nlink = 2;
    stat_hint->size = 0;
    
    /* Initializing direntry hint */
    if (!(hint->item[1] = aal_calloc(sizeof(reiserfs_item_hint_t), 0)))
	goto error_free_hint0;
    
    hint->item[1]->type = REISERFS_DIRENTRY_ITEM; 
    
    if (!(hint->item[1]->plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN,
	direntry_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, direntry_plugin_id,
	    goto error_free_item1);
    }
    
    if (!(hint->item[1]->hint = aal_calloc(sizeof(reiserfs_direntry_hint_t), 0)))
	goto error_free_item1;

    hint->item[1]->key.plugin = key_plugin; 
    aal_memcpy(&hint->item[1]->key.body, parent, sizeof(parent->body));
    direntry_hint = hint->item[1]->hint;

    direntry_hint->count = 2;
    direntry_hint->key_plugin = key_plugin;
    direntry_hint->hash_plugin = NULL;
   
    libreiser4_plugin_call(goto error_free_hint1, key_plugin->key, build_dir_key, 
	&hint->item[1]->key.body, direntry_hint->hash_plugin, parent_objectid, objectid, ".");
    
    if (!(direntry_hint->entry = aal_calloc(2 * sizeof(reiserfs_entry_hint_t *), 0)))
	goto error_free_hint1;
    
    entry = direntry_hint->entry;

    if (!(entry[0] = aal_calloc(sizeof(reiserfs_entry_hint_t), 0)))
	goto error_free_entry;
    
    entry[0]->locality = parent_objectid;
    entry[0]->objectid = objectid;
    entry[0]->name = ".";
    
    if (!(entry[1] = aal_calloc(sizeof(reiserfs_entry_hint_t), 0)))
	goto error_free_entry0;
    
    entry[1]->locality = parent_locality;
    entry[1]->objectid = parent_objectid;
    entry[1]->name = "..";
    
    return hint;

error_free_entry1:
    aal_free(entry[1]);
error_free_entry0:
    aal_free(entry[0]);
error_free_entry:
    aal_free(direntry_hint->entry);
error_free_hint1:
    aal_free(hint->item[1]->hint);
error_free_item1:
    aal_free(hint->item[1]);
error_free_hint0:
    aal_free(hint->item[0]->hint);
error_free_item0:
    aal_free(hint->item[0]);
error_free_item:
    aal_free(hint->item);
error_free_hint:
    aal_free(hint);
error:
    return NULL;
}

static void dir40_destroy(reiserfs_object_hint_t *hint) {
    int i;
    
    aal_assert("umka-750", hint != NULL, return);

    for (i = 0; i < hint->count; i++) {
	reiserfs_item_hint_t *item_hint = hint->item[i];
	
	if (item_hint->type == REISERFS_DIRENTRY_ITEM) {
	    int j;
	    
	    reiserfs_direntry_hint_t *de_hint = 
		(reiserfs_direntry_hint_t *)item_hint->hint;
	
	    for (j = 0; j < de_hint->count; j++)
		aal_free(de_hint->entry[j]);
	    
	    aal_free(de_hint->entry);
	    aal_free(de_hint);
	}
	aal_free(item_hint);
    }
    aal_free(hint->item);
    aal_free(hint);
}

/*static error_t dir40_item_insert(reiserfs_item_hint_t *hint, aal_block_t *block, 
    reiserfs_unit_coord_t *coord, reiserfs_plugin_t *node_plugin, reiserfs_key_t *key) 
{
    uint32_t overhead, free_space;

    aal_assert("umka-721", key != NULL, return -1);
    aal_assert("umka-722", key->plugin != NULL, return -1);
    
    if (libreiser4_plugin_call(return -1, hint->plugin->item.common, 
	estimate, hint, coord))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate stat data item.");
	return -1;
    }

    overhead = libreiser4_plugin_call(return -1, node_plugin->node, 
	item_overhead, block);
    
    free_space = libreiser4_plugin_call(return -1, node_plugin->node, 
	get_free_space, block);
    
    if (hint->length + overhead > free_space) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is not enought free space (%u) for inserting stat data item "
	    "into block %llu.", free_space, aal_block_get_nr(block));
	return -1;
    }
    
    return libreiser4_plugin_call(return -1, node_plugin->node, item_insert, 
	block, coord, key, hint);
}*/

/*static reiserfs_dir40_t *dir40_create(aal_block_t *block, uint32_t pos, 
    uint16_t key_plugin_id, uint16_t stat_plugin_id, uint16_t direntry_plugin_id, 
    uint16_t oid_plugin_id, uint16_t node_plugin_id) 
{
    reiserfs_key_t key;
    reiserfs_unit_coord_t coord;
    
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *oid_plugin;
    reiserfs_plugin_t *node_plugin;
    
    reiserfs_entry_hint_t entry[2];
    reiserfs_item_hint_t item_hint;
    reiserfs_stat_hint_t stat_hint;
    reiserfs_direntry_hint_t direntry_hint;

    oid_t root_parent_objectid;
    oid_t root_parent_locality;
    oid_t root_objectid;

    reiserfs_dir40_t *dir;

    aal_assert("umka-672", block != NULL, return NULL);

    if (!(key_plugin = factory->find_by_coord(REISERFS_KEY_PLUGIN, 
	key_plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, key_plugin_id,
	    return NULL);
    }
    
    if (!(node_plugin = factory->find_by_coord(REISERFS_NODE_PLUGIN,
	node_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_NODE_PLUGIN, node_plugin_id,
	    return NULL);
    }
    
    if (!(oid_plugin = factory->find_by_coord(REISERFS_OID_PLUGIN, 
	oid_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_OID_PLUGIN, oid_plugin_id,
	    return NULL);
    }
    
    root_parent_objectid = libreiser4_plugin_call(return NULL, 
	oid_plugin->oid, root_parent_objectid,);
    
    root_parent_locality = libreiser4_plugin_call(return NULL, 
	oid_plugin->oid, root_parent_locality,);
    
    root_objectid = libreiser4_plugin_call(return NULL, 
	oid_plugin->oid, root_objectid,);
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;

    dir->block = block;
    dir->pos = pos;*/
    
    /* Initialize stat data */
/*    stat_hint.mode = S_IFDIR | 0755;
    stat_hint.extmask = 0;
    stat_hint.nlink = 2;
    stat_hint.size = 0;
    
    aal_memset(&item_hint, 0, sizeof(item_hint));
    item_hint.info = &stat_hint;
    
    coord.item_pos = pos;
    coord.unit_pos = -1;
    
    key.plugin = key_plugin;
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, clean, key.body);
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, build_file_key, 
	key.body, KEY40_STATDATA_MINOR, root_parent_objectid, root_objectid, 0);
    
    if (!(item_hint.plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN,
	stat_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, stat_plugin_id,
	    goto error_free_dir);
    }
    
    if (dir40_item_insert(&item_hint, block, &coord, node_plugin, &key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert stat data item into node %llu", 
	    aal_block_get_nr(block));
	goto error_free_dir;
    }

    entry[0].locality = root_parent_objectid;
    entry[0].objectid = root_objectid;
    entry[0].name = ".";
    entry[1].locality = root_parent_locality;
    entry[1].objectid = root_parent_objectid;
    entry[1].name = "..";*/
    
    /* Initialize direntry item */   
/*    direntry_hint.count = 2;
    direntry_hint.entry = entry;
    direntry_hint.key_plugin = key_plugin;
    direntry_hint.hash_plugin = NULL;
    
    aal_memset(&item_hint, 0, sizeof(item_hint));
    item_hint.info = &direntry_hint;

    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, clean, key.body);
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, build_dir_key, 
	key.body, direntry_hint.hash_plugin, root_parent_objectid, root_objectid, ".");
     
    coord.item_pos = pos + 1;
    coord.unit_pos = -1;

    if (!(item_hint.plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN, 
	direntry_plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, direntry_plugin_id,
	    goto error_free_dir);
    }

    if (dir40_item_insert(&item_hint, block, &coord, node_plugin, &key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert direntry item into node %llu", aal_block_get_nr(block));
	goto error_free_dir;
    }
    
    return dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}*/

#endif

/*static reiserfs_opaque_t *dir40_open(aal_block_t *block, uint32_t pos) {
    reiserfs_dir40_t *dir;
    
    aal_assert("umka-674", block != NULL, return NULL);

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->block = block;
    dir->pos = pos;

    
    return dir;
}

static void dir40_close(reiserfs_dir40_t *dir) {
    aal_assert("umka-673", dir != NULL, return);
    aal_free(dir);
}*/

static reiserfs_plugin_t dir40_plugin = {
    .dir = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_DIR_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
#ifndef ENABLE_COMPACT
	.build = (void *(*)(void *, void *, uint16_t, uint16_t))
	    dir40_build,

	.destroy = (void (*)(void *))dir40_destroy
#else
	.build = NULL,
	.destroy = NULL
#endif
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &dir40_plugin;
}

libreiser4_factory_register(dir40_entry);

