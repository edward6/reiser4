/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include <reiser4/key.h>
#include <misc/misc.h>

#include "direntry40.h"

#define	DIRENTRY40_ID 0x2

static reiserfs_plugin_factory_t *factory = NULL;

#ifndef ENABLE_COMPACT

static error_t direntry40_create(reiserfs_direntry40_t *direntry, 
    reiserfs_item_info_t *info)
{
    int i;
    uint16_t len, offset;
    reiserfs_plugin_t *key_plugin;
    reiserfs_direntry_info_t *direntry_info;
    
    aal_assert("vpf-097", direntry != NULL, return -1);
    aal_assert("vpf-098", info != NULL, return -1);
    aal_assert("vpf-099", info->info != NULL, return -1);
    
    direntry_info = info->info;
    key_plugin = direntry_info->key_plugin;
    
    de40_set_count(direntry, direntry_info->count);
    offset = sizeof(reiserfs_direntry40_t) + 
	direntry_info->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < direntry_info->count; i++) {	
	e40_set_offset(&direntry->entry[i], offset);

	libreiser4_plugin_call(return -1, key_plugin->key, build_dir_short_key, 
	    &direntry->entry[i].entryid, direntry_info->entry[i].name, 
	    direntry_info->hash_plugin, sizeof(reiserfs_entryid_t));

	libreiser4_plugin_call(return -1, key_plugin->key, build_file_short_key, 
	    (reiserfs_objid_t *)((char *)direntry + offset), KEY40_STATDATA_MINOR, 
	    direntry_info->entry[i].locality, direntry_info->entry[i].objectid, 
	    sizeof(reiserfs_objid_t));
	
	len = aal_strlen(direntry_info->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, direntry_info->entry[i].name, len);
	
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static error_t direntry40_estimate(reiserfs_item_info_t *info, 
    reiserfs_coord_t *coord) 
{
    int i;
    reiserfs_direntry_info_t *direntry_info;
	    
    aal_assert("vpf-095", info != NULL, return -1);
    aal_assert("vpf-096", info->info != NULL, return -1);
    aal_assert("umka-608", coord != NULL, return -1);
    
    direntry_info = info->info;
    info->length = direntry_info->count * sizeof(reiserfs_entry40_t);
    
    for (i = 0; i < direntry_info->count; i++) {
	info->length += aal_strlen(direntry_info->entry[i].name) + 
	    sizeof(reiserfs_objid_t) + 1;
    }

    if (coord == NULL || coord->unit_pos == -1)
	info->length += sizeof(reiserfs_direntry40_t);
    
    return 0;
}

#endif

static void direntry40_print(reiserfs_direntry40_t *direntry, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-548", direntry != NULL, return);
    aal_assert("umka-549", buff != NULL, return);
}

static uint32_t direntry40_minsize(void) {
    return sizeof(reiserfs_direntry40_t);
}

/* 
    Helper function that is used by lookup method 
    for getting n-th element of direntry.
*/
static void *callback_elem_for_lookup(void *direntry, uint32_t pos, 
    void *data) 
{
    return &((reiserfs_direntry40_t *)direntry)->entry[pos].entryid;
}

/* 
    Helper function that is used by lookup method
    for comparing given key with passed dirid.
*/
static int callback_cmp_for_lookup(const void *key1, const void *key2, 
    void *data) 
{
    oid_t locality;
    oid_t objectid;
    uint64_t offset;
    reiserfs_key_t key;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-657", key1 != NULL, return -2);
    aal_assert("umka-658", key2 != NULL, return -2);
    aal_assert("umka-659", data != NULL, return -2);
    
    plugin = (reiserfs_plugin_t *)data;
    
    locality = libreiser4_plugin_call(return -2, plugin->key, 
	get_locality, (void *)key2);
   
    objectid = entryid_get_objectid(((reiserfs_entryid_t *)key1));
    offset = entryid_get_offset(((reiserfs_entryid_t *)key1));
    
    /* FIXME-UMKA: Here should be not hardcoded key parameters */
    libreiser4_plugin_call(return -2, plugin->key, clean, &key);
    libreiser4_plugin_call(return -2, plugin->key, build_file_key, 
	&key, 0, locality, objectid, offset);
    
    return libreiser4_plugin_call(return -2, plugin->key, compare, &key, key2);
}

static int direntry40_lookup(reiserfs_direntry40_t *direntry, 
    void *key, reiserfs_coord_t *coord)
{
    int found;
    uint64_t pos;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-609", direntry != NULL, return -2);
    aal_assert("umka-610", key != NULL, return -2);
    aal_assert("umka-629", coord != NULL, return -2);
    
    /*
	FIXME-UMKA: Here should be using not hardcoded
	key id.
    */
    if (!(plugin = factory->find_by_coord(REISERFS_KEY_PLUGIN, 0x0)))
	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, 0x0, return -2);
    
    if ((found = reiserfs_misc_bin_search((void *)direntry, 
	    direntry->count, key, callback_elem_for_lookup, 
	    callback_cmp_for_lookup, plugin, &pos)) == -1)
	return -1;

    coord->unit_pos = (uint32_t)pos;

    return found;
}

static int direntry40_internal(void) {
    return 0;
}

static error_t direntry40_max_key_inside(void *key, reiserfs_plugin_t *plugin) {
    aal_assert("vpf-120", plugin != NULL, return -1);
    aal_assert("vpf-121", plugin->key.set_objectid != NULL, return -1);
    aal_assert("vpf-122", plugin->key.get_objectid != NULL, return -1);
    aal_assert("vpf-123", plugin->key.maximal != NULL, return -1);
    
    plugin->key.set_objectid(key, plugin->key.get_objectid(plugin->key.maximal()));
    plugin->key.set_offset(key, plugin->key.get_offset(plugin->key.maximal()));
    return 0;
}

static reiserfs_plugin_t direntry40_plugin = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = DIRENTRY40_ID,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "direntry40",
	    .desc = "Directory plugin for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .type = DIRENTRY_ITEM,

#ifndef ENABLE_COMPACT	    
	    .create = (error_t (*)(void *, void *))direntry40_create,
	    .estimate = (error_t (*)(void *, void *))direntry40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .minsize = (uint32_t (*)(void))direntry40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))direntry40_print,
	    .lookup = (int (*) (void *, void *, void *))direntry40_lookup,
	    .internal = (int (*)(void))direntry40_internal,
	    .max_key_inside = (error_t (*)(void *, void*))direntry40_max_key_inside,
	    
	    .confirm = NULL,
	    .check = NULL,

	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL
	},
	.specific = {
	    .dir = { }
	}
    }
};

static reiserfs_plugin_t *direntry40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &direntry40_plugin;
}

libreiser4_factory_register(direntry40_entry);
