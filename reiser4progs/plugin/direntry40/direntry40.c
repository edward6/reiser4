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

static reiserfs_core_t *core = NULL;

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
	en40_set_offset(&direntry->entry[i], offset);

	libreiser4_plugin_call(return -1, key_plugin->key_ops, build_entry_short, 
	    &direntry->entry[i].entryid, direntry_hint->hash_plugin, 
	    direntry_hint->entry[i].name);

	libreiser4_plugin_call(return -1, key_plugin->key_ops, build_generic_short, 
	    (reiserfs_objid_t *)((char *)direntry + offset), KEY40_STATDATA_MINOR, 
	    direntry_hint->entry[i].locality, direntry_hint->entry[i].objectid);
	
	len = aal_strlen(direntry_hint->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, 
	    direntry_hint->entry[i].name, len);
	
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static errno_t direntry40_insert(reiserfs_direntry40_t *direntry, 
    uint16_t pos, reiserfs_item_hint_t *hint)
{
    aal_assert("umka-791", direntry != NULL, return -1);
    aal_assert("umka-792", hint != NULL, return -1);
    
    de40_set_count(direntry, de40_get_count(direntry) + 1);
    
    return 0;
}

static errno_t direntry40_estimate(uint16_t pos, reiserfs_item_hint_t *hint) {
    int i;
    reiserfs_direntry_hint_t *direntry_hint;
	    
    aal_assert("vpf-095", hint != NULL, return -1);
    
    direntry_hint = (reiserfs_direntry_hint_t *)hint->hint;
    hint->len = direntry_hint->count * sizeof(reiserfs_entry40_t);
    
    for (i = 0; i < direntry_hint->count; i++) {
	hint->len += aal_strlen(direntry_hint->entry[i].name) + 
	    sizeof(reiserfs_objid_t) + 1;
    }

    if (pos == 0xffff)
	hint->len += sizeof(reiserfs_direntry40_t);
    
    return 0;
}

#endif

static errno_t direntry40_print(reiserfs_direntry40_t *direntry, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-548", direntry != NULL, return -1);
    aal_assert("umka-549", buff != NULL, return -1);

    return -1;
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
    
    locality = libreiser4_plugin_call(return -1, plugin->key_ops, 
	get_locality, (void *)key2);
   
    objectid = entryid_get_objectid(((reiserfs_entryid_t *)key1));
    offset = entryid_get_offset(((reiserfs_entryid_t *)key1));
    
    libreiser4_plugin_call(return -1, plugin->key_ops, build_generic_full, 
	&key, KEY40_STATDATA_MINOR, locality, objectid, offset);
    
    return libreiser4_plugin_call(return -1, plugin->key_ops, 
	compare, &key, key2);
}

static int direntry40_lookup(reiserfs_direntry40_t *direntry, 
    reiserfs_key_t *key, uint16_t *pos)
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
	*pos = (uint16_t)unit;

    return lookup;
}

static errno_t direntry40_maxkey(reiserfs_key_t *key) {
    aal_assert("umka-716", key->plugin != NULL, return -1);

    aal_assert("vpf-121", key->plugin->key_ops.set_objectid != NULL, return -1);
    aal_assert("vpf-122", key->plugin->key_ops.get_objectid != NULL, return -1);
    aal_assert("vpf-123", key->plugin->key_ops.maximal != NULL, return -1);
    
    key->plugin->key_ops.set_objectid(key->body, 
	key->plugin->key_ops.get_objectid(key->plugin->key_ops.maximal()));
    
    key->plugin->key_ops.set_offset(key->body, 
	key->plugin->key_ops.get_offset(key->plugin->key_ops.maximal()));
    
    return 0;
}

static reiserfs_plugin_t direntry40_plugin = {
    .item_ops = {
	.h = {
	    .handle = NULL,
	    .id = REISERFS_CDE_ITEM,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "direntry40",
	    .desc = "Directory plugin for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
#ifndef ENABLE_COMPACT	    
	    .create = (errno_t (*)(const void *, reiserfs_item_hint_t *))
		direntry40_create,
	    
	    .estimate = (errno_t (*)(uint16_t, reiserfs_item_hint_t *))
		direntry40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .minsize = (uint16_t (*)(void))direntry40_minsize,
	    
	    .print = (errno_t (*)(const void *, char *, uint16_t))
		direntry40_print,
	    
	    .lookup = (int (*) (const void *, reiserfs_key_t *, uint16_t *))
		direntry40_lookup,
	    
	    .maxkey = (errno_t (*)(const void *))direntry40_maxkey,
	    
	    .insert = (errno_t (*)(const void *, uint16_t, reiserfs_item_hint_t *))
		direntry40_insert,
	    
	    .count = NULL,
	    .remove = NULL,
	    
	    .confirm = NULL,
	    .check = NULL
	},
	.specific = {
	    .dir = { }
	}
    }
};

static reiserfs_plugin_t *direntry40_entry(reiserfs_core_t *c) {
    core = c;
    return &direntry40_plugin;
}

libreiser4_factory_register(direntry40_entry);

