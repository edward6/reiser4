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

#ifndef ENABLE_COMPACT

static errno_t direntry40_create(reiserfs_direntry40_t *direntry, 
    reiserfs_item_hint_t *hint)
{
    int i;
    uint16_t len, offset;
    reiserfs_plugin_t *key_plugin;
    reiserfs_direntry_hint_t *direntry_hint;
    
    aal_assert("vpf-097", direntry != NULL, return -1);
    aal_assert("vpf-098", hint != NULL, return -1);
    
    direntry_hint = (reiserfs_direntry_hint_t *)hint->hint;
    key_plugin = direntry_hint->key_plugin;
    
    de40_set_count(direntry, direntry_hint->count);
    offset = sizeof(reiserfs_direntry40_t) + 
	direntry_hint->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < direntry_hint->count; i++) {	
	e40_set_offset(&direntry->entry[i], offset);

	libreiser4_plugin_call(return -1, key_plugin->key, build_dir_short_key, 
	    &direntry->entry[i].entryid, direntry_hint->entry[i].name, 
	    direntry_hint->hash_plugin, sizeof(reiserfs_entryid_t));

	libreiser4_plugin_call(return -1, key_plugin->key, build_file_short_key, 
	    (reiserfs_objid_t *)((char *)direntry + offset), KEY40_STATDATA_MINOR, 
	    direntry_hint->entry[i].locality, direntry_hint->entry[i].objectid, 
	    sizeof(reiserfs_objid_t));
	
	len = aal_strlen(direntry_hint->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, direntry_hint->entry[i].name, len);
	
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static errno_t direntry40_estimate(reiserfs_item_hint_t *hint, 
    reiserfs_pos_t *pos) 
{
    int i;
    reiserfs_direntry_hint_t *direntry_hint;
	    
    aal_assert("vpf-095", hint != NULL, return -1);
    aal_assert("umka-608", pos != NULL, return -1);
    
    direntry_hint = (reiserfs_direntry_hint_t *)hint->hint;
    hint->length = direntry_hint->count * sizeof(reiserfs_entry40_t);
    
    for (i = 0; i < direntry_hint->count; i++) {
	hint->length += aal_strlen(direntry_hint->entry[i].name) + 
	    sizeof(reiserfs_objid_t) + 1;
    }

    if (pos == NULL || pos->unit == -1)
	hint->length += sizeof(reiserfs_direntry40_t);
    
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
static void *callback_elem_for_lookup(void *direntry, 
    uint32_t pos, void *data) 
{
    return &((reiserfs_direntry40_t *)direntry)->entry[pos].entryid;
}

/* 
    Helper function that is used by lookup method
    for comparing given key with passed dirid.
*/
static int callback_comp_for_lookup(const void *key1, 
    const void *key2, void *data) 
{
    oid_t locality;
    oid_t objectid;
    uint64_t offset;
    reiserfs_key_t key;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-657", key1 != NULL, return -1);
    aal_assert("umka-658", key2 != NULL, return -1);
    aal_assert("umka-659", data != NULL, return -1);
    
    plugin = (reiserfs_plugin_t *)data;
    
    locality = libreiser4_plugin_call(return -1, plugin->key, 
	get_locality, (void *)key2);
   
    objectid = entryid_get_objectid(((reiserfs_entryid_t *)key1));
    offset = entryid_get_offset(((reiserfs_entryid_t *)key1));
    
    libreiser4_plugin_call(return -1, plugin->key, build_file_key, 
	&key, KEY40_STATDATA_MINOR, locality, objectid, offset);
    
    return libreiser4_plugin_call(return -1, plugin->key, 
	compare, &key, key2);
}

static int direntry40_lookup(reiserfs_direntry40_t *direntry, 
    reiserfs_key_t *key, reiserfs_pos_t *pos)
{
    int lookup;
    uint64_t unit;
    
    aal_assert("umka-610", key != NULL, return -2);
    aal_assert("umka-717", key->plugin != NULL, return -2);
    
    aal_assert("umka-609", direntry != NULL, return -2);
    aal_assert("umka-629", pos != NULL, return -2);
    
    if ((lookup = reiserfs_misc_bin_search((void *)direntry, 
	    direntry->count, key->body, callback_elem_for_lookup, 
	    callback_comp_for_lookup, key->plugin, &unit)) != -1)
	pos->unit = (uint32_t)pos;

    return lookup;
}

static int direntry40_internal(void) {
    return 0;
}

static errno_t direntry40_max_key(reiserfs_key_t *key) {
    aal_assert("umka-716", key->plugin != NULL, return -1);
    aal_assert("vpf-121", key->plugin->key.set_objectid != NULL, return -1);
    aal_assert("vpf-122", key->plugin->key.get_objectid != NULL, return -1);
    aal_assert("vpf-123", key->plugin->key.maximal != NULL, return -1);
    
    key->plugin->key.set_objectid(key->body, 
	key->plugin->key.get_objectid(key->plugin->key.maximal()));
    
    key->plugin->key.set_offset(key->body, 
	key->plugin->key.get_offset(key->plugin->key.maximal()));
    
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
	    .type = REISERFS_DIRENTRY_ITEM,

#ifndef ENABLE_COMPACT	    
	    .create = (errno_t (*)(void *, void *))direntry40_create,
	    .estimate = (errno_t (*)(void *, void *))direntry40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .minsize = (uint16_t (*)(void))direntry40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))direntry40_print,
	    .lookup = (int (*) (void *, void *, void *))direntry40_lookup,
	    .internal = (int (*)(void))direntry40_internal,
	    .max_key = (errno_t (*)(void *))direntry40_max_key,
	    
	    .confirm = NULL,
	    .check = NULL,

	    .insert = NULL,
	    .count = NULL,
	    .remove = NULL
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
