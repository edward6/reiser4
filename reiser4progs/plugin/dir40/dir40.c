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

#define DIR40_ID 0x0

static reiserfs_plugin_factory_t *factory = NULL;

#ifndef ENABLE_COMPACT

static error_t dir40_item_insert(reiserfs_item_info_t *item_info, aal_block_t *block, 
    reiserfs_unit_coord_t *coord, reiserfs_plugin_t *node_plugin, reiserfs_key_t *key) 
{
    uint32_t overhead, free_space;

    aal_assert("umka-721", key != NULL, return -1);
    aal_assert("umka-722", key->plugin != NULL, return -1);
    
    if (libreiser4_plugin_call(return -1, item_info->plugin->item.common, 
	estimate, item_info, coord))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate stat data item.");
	return -1;
    }

    overhead = libreiser4_plugin_call(return -1, node_plugin->node, 
	item_overhead, block);
    
    free_space = libreiser4_plugin_call(return -1, node_plugin->node, 
	get_free_space, block);
    
    if (item_info->length + overhead > free_space) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is not enought free space (%u) for inserting stat data item "
	    "into block %llu.", free_space, aal_block_get_nr(block));
	return -1;
    }
    
    return libreiser4_plugin_call(return -1, node_plugin->node, item_insert, 
	block, coord, key, item_info);
}

static reiserfs_dir40_t *dir40_create(aal_block_t *block, uint32_t pos, 
    uint16_t key_plugin_id, uint16_t stat_plugin_id, uint16_t direntry_plugin_id, 
    uint16_t oid_plugin_id, uint16_t node_plugin_id) 
{
    reiserfs_key_t key;
    reiserfs_unit_coord_t coord;
    
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *oid_plugin;
    reiserfs_plugin_t *node_plugin;
    
    reiserfs_entry_info_t entry[2];
    reiserfs_item_info_t item_info;
    reiserfs_stat_info_t stat_info;
    reiserfs_direntry_info_t direntry_info;

    oid_t root_parent_objectid;
    oid_t root_parent_locality;
    oid_t root_objectid;

    reiserfs_dir40_t *dir;

    aal_assert("umka-672", block != NULL, return NULL);

    /* Initialize plugins */
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
    dir->pos = pos;
    
    /* Initialize stat data */
    stat_info.mode = S_IFDIR | 0755;
    stat_info.extmask = 0;
    stat_info.nlink = 2;
    stat_info.size = 0;
    
    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &stat_info;
    
    coord.item_pos = pos;
    coord.unit_pos = -1;
    
    key.plugin = key_plugin;
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, clean, key.body);
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, build_file_key, 
	key.body, KEY40_STATDATA_MINOR, root_parent_objectid, root_objectid, 0);
    
    if (!(item_info.plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN,
	stat_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, stat_plugin_id,
	    goto error_free_dir);
    }
    
    if (dir40_item_insert(&item_info, block, &coord, node_plugin, &key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert stat data item into node %llu", 
	    aal_block_get_nr(block));
	goto error_free_dir;
    }

    /*
	FIXME-UMKA: Declaration of this type also should be
	moved to plugin.h
    */
    entry[0].locality = root_parent_objectid;
    entry[0].objectid = root_objectid;
    entry[0].name = ".";
    entry[1].locality = root_parent_locality;
    entry[1].objectid = root_parent_objectid;
    entry[1].name = "..";
    
    /* Initialize direntry item */   
    direntry_info.count = 2;
    direntry_info.entry = entry;
    direntry_info.parent_id = root_parent_objectid;
    direntry_info.object_id = root_objectid;
    direntry_info.key_plugin = key_plugin;
    direntry_info.hash_plugin = NULL;
    
    aal_memset(&item_info, 0, sizeof(item_info));
    item_info.info = &direntry_info;

    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, clean, key.body);
    libreiser4_plugin_call(goto error_free_dir, key_plugin->key, build_dir_key, 
	key.body, direntry_info.hash_plugin, direntry_info.parent_id, 
	direntry_info.object_id, ".");
     
    coord.item_pos = pos + 1;
    coord.unit_pos = -1;

    if (!(item_info.plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN, 
	direntry_plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, direntry_plugin_id,
	    goto error_free_dir);
    }

    if (dir40_item_insert(&item_info, block, &coord, node_plugin, &key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert direntry item into node %llu", aal_block_get_nr(block));
	goto error_free_dir;
    }
    
    return dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}

#endif

static reiserfs_opaque_t *dir40_open(aal_block_t *block, uint32_t pos) {
    reiserfs_dir40_t *dir;
    
    aal_assert("umka-674", block != NULL, return NULL);

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->block = block;
    dir->pos = pos;

    /* 
	Here will be also initializing of the stat data of 
	this directory.
    */
    
    return dir;
}

static void dir40_close(reiserfs_dir40_t *dir) {
    aal_assert("umka-673", dir != NULL, return);
    aal_free(dir);
}

static reiserfs_plugin_t dir40_plugin = {
    .dir = {
	.h = {
	    .handle = NULL,
	    .id = DIR40_ID,
	    .type = REISERFS_DIR_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
#ifndef ENABLE_COMPACT
	.create = (reiserfs_opaque_t *(*)(aal_block_t *, uint32_t, uint16_t, uint16_t, 
	    uint16_t, uint16_t, uint16_t))dir40_create,
#else
	.create = NULL,
#endif
	.open = (reiserfs_opaque_t *(*)(aal_block_t *, uint32_t))dir40_open,
	.close = (void (*)(reiserfs_opaque_t *))dir40_close
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &dir40_plugin;
}

libreiser4_factory_register(dir40_entry);

