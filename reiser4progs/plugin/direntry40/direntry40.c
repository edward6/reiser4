/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include <misc/misc.h>

#include "direntry40.h"

#define	DIRENTRY40_ID 0x2

static reiserfs_plugin_factory_t *factory = NULL;

/* 
    This will build sd key as objid has SD_MINOR.
    FIXME-UMKA: What is the sence of this function.
    It seems it doesn't used for now.
*/
static void *direntry40_build_key_by_objid(reiserfs_plugin_t *plugin, 
    reiserfs_objid_t *id) 
{
    oid_t locality;
    oid_t objectid;

    aal_assert("vpf-087", id != NULL, return NULL);
    aal_assert("umka-660", plugin != NULL, return NULL);
   
    aal_memcpy(&locality, id->locality, sizeof(locality));
    aal_memcpy(&objectid, id->objectid, sizeof(objectid));
    
    return libreiser4_plugins_call(return NULL, plugin->key, 
	create, 0, locality, objectid, 0);
}

/* 
    Builds stat-data key. It is used by direntry40_create
    function.
*/
static void direntry40_build_objid_by_params(reiserfs_objid_t *objid, 
    oid_t locality, oid_t objectid)
{
    aal_assert("vpf-089", objid != NULL, return);
    aal_memcpy(objid, &locality, sizeof(locality));
    aal_memcpy(((oid_t *)objid) + 1, &objectid, sizeof(objectid));
}

#ifndef ENABLE_COMPACT

static error_t direntry40_create(reiserfs_direntry40_t *direntry, 
    reiserfs_item_info_t *info)
{
    int i;
    uint16_t len, offset;
    reiserfs_direntry_info_t *direntry_info;
    
    aal_assert("vpf-097", direntry != NULL, return -1);
    aal_assert("vpf-098", info != NULL, return -1);
    aal_assert("vpf-099", info->info != NULL, return -1);
    
    direntry_info = info->info;
    
    de40_set_count(direntry, direntry_info->count);
    
    offset = sizeof(reiserfs_direntry40_t) + 
	direntry_info->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < direntry_info->count; i++) {	
	e40_set_offset(&direntry->entry[i], offset);
	
	/* 
	    FIXME-UMKA: This should be moved from libreiser4/key.c to 
	    direntry40 plugin or to somethinkg like it.
	*/
	build_entryid_by_info(&direntry->entry[i].entryid, &direntry_info->entry[i]);
	
	direntry40_build_objid_by_params((reiserfs_objid_t *)((char *)direntry + offset), 
	    direntry_info->entry[i].parent_id, direntry_info->entry[i].object_id);
	
	len = aal_strlen(direntry_info->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, direntry_info->entry[i].name, len);
	
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static void direntry40_estimate(reiserfs_item_info_t *info, 
    reiserfs_coord_t *coord) 
{
    int i;
    reiserfs_direntry_info_t *direntry_info;
	    
    aal_assert("vpf-095", info != NULL, return);
    aal_assert("vpf-096", info->info != NULL, return);
    aal_assert("umka-608", coord != NULL, return);
    
    direntry_info = info->info;
    info->length = direntry_info->count * sizeof(reiserfs_entry40_t);
    
    for (i = 0; i < direntry_info->count; i++) {
	info->length += aal_strlen(direntry_info->entry[i].name) + 
	    sizeof(reiserfs_objid_t) + 1;
    }

    if (coord == NULL || coord->unit_pos == -1)
	info->length += sizeof(reiserfs_direntry40_t);
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
    int res;
    void *ekey;
    oid_t locality;
    oid_t objectid;
    uint64_t offset;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-657", key1 != NULL, return -2);
    aal_assert("umka-658", key2 != NULL, return -2);
    aal_assert("umka-659", data != NULL, return -2);
    
    plugin = (reiserfs_plugin_t *)data;
    
    /* Preparing key for comparing */
    locality = libreiser4_plugins_call(return -2, plugin->key, 
	get_locality, (void *)key2);
    
    aal_memcpy(&objectid, key1, sizeof(objectid));
    aal_memcpy(&offset, ((uint64_t *)key1) + 1, sizeof(offset));

    /* FIXME-UMKA: Here should be not hardcoded key parameters */
    if (!(ekey = libreiser4_plugins_call(return -2, plugin->key, create, 
	0, locality, objectid, offset)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create key by its params (%llu:%u:%llu:%llu)", 
	    locality, 0, objectid, offset);
	return -2;
    }
    
    res = libreiser4_plugins_call(return -2, plugin->key, compare, ekey, key2);
    libreiser4_plugins_call(return -2, plugin->key, close, ekey);
    
    return res;
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
    
    if (!(plugin = factory->find_by_coords(REISERFS_KEY_PLUGIN, 0x0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find key plugin by its id %x.", 0x0);
	return -2;
    }
    
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
	    .estimate = (void (*)(void *, void *))direntry40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .minsize = (uint32_t (*)(void))direntry40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))direntry40_print,
	    .lookup = (int (*) (void *, void *, void *))direntry40_lookup,
	    .internal = (int (*)(void))direntry40_internal,
	    
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

libreiser4_plugins_register(direntry40_entry);

