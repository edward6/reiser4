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

static uint32_t direntry40_count(reiserfs_direntry40_t *direntry) {
    aal_assert("umka-865", direntry != NULL, return 0);
    return de40_get_count(direntry);
}

static errno_t direntry40_get_entry(reiserfs_direntry40_t *direntry, 
    uint32_t pos, reiserfs_entry_hint_t *entry)
{
    uint32_t offset;
    reiserfs_entry40_t *en;
    reiserfs_objid_t *objid;
    
    aal_assert("umka-866", direntry != NULL, return -1);
    
    if (pos > de40_get_count(direntry))
	return -1;
    
    offset = sizeof(reiserfs_direntry40_t) + 
	pos * sizeof(reiserfs_entry40_t);
    
    en = (reiserfs_entry40_t *)(((char *)direntry) + offset);
    
    entry->entryid.objectid = eid_get_objectid((reiserfs_entryid_t *)(&en->entryid));
    entry->entryid.offset = eid_get_offset((reiserfs_entryid_t *)(&en->entryid));
    
    offset = en40_get_offset(en); 
    objid = (reiserfs_objid_t *)(((char *)direntry) + offset);
    
    entry->objid.objectid = oid_get_objectid(objid);
    entry->objid.locality = oid_get_locality(objid);

    /* Here will be also filling up of entry->entryid */
    
    offset += sizeof(*objid);
    entry->name = ((char *)direntry) + offset;
    
    return 0;
}

#ifndef ENABLE_COMPACT

static errno_t direntry40_estimate(uint32_t pos, reiserfs_item_hint_t *hint) {
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

static errno_t direntry40_insert(reiserfs_direntry40_t *direntry, 
    uint32_t pos, reiserfs_item_hint_t *hint)
{
    uint32_t i, offset, len1, len2;
    reiserfs_direntry_hint_t *direntry_hint;
    
    aal_assert("umka-791", direntry != NULL, return -1);
    aal_assert("umka-792", hint != NULL, return -1);
    aal_assert("umka-897", pos != 0xffff, return -1);

    if (pos > de40_get_count(direntry))
	return -1;

    direntry_hint = (reiserfs_direntry_hint_t *)hint->hint;
    
    /* Getting offset area of new entry body will be created at */
    if (de40_get_count(direntry) > 0) {
	char *name;
	
	if (pos < de40_get_count(direntry))
	    offset = en40_get_offset(&direntry->entry[pos]);
	else {
	    offset = en40_get_offset(&direntry->entry[de40_get_count(direntry) - 1]);
	
	    name = (char *)((char *)direntry) + offset + 
		sizeof(reiserfs_objid_t);
	
	    offset += sizeof(reiserfs_entry40_t) + aal_strlen(name) + 
		sizeof(reiserfs_objid_t) + 1;
	}
    } else
	offset = sizeof(reiserfs_direntry40_t) + 
	    direntry_hint->count * sizeof(reiserfs_entry40_t);
    
    /* Updating offsets */
    if (direntry40_estimate(pos, hint))
	return -1;
	
    len1 = (de40_get_count(direntry) - pos)*sizeof(reiserfs_entry40_t);
	
    for (i = 0; i < de40_get_count(direntry); i++) {
        char *name = (char *)(((char *)direntry) + 
	    en40_get_offset(&direntry->entry[i]) + 
	    sizeof(reiserfs_objid_t));
	    
	len1 += aal_strlen(name) + sizeof(reiserfs_objid_t) + 1;
    }
	
    len2 = 0;
    for (i = pos; i < de40_get_count(direntry); i++) {
	char *name = (char *)(((char *)direntry) + 
	    en40_get_offset(&direntry->entry[i]) + 
	    sizeof(reiserfs_objid_t));
	    
	len2 += aal_strlen(name) + sizeof(reiserfs_objid_t) + 1;
    }
	
    for (i = 0; i < pos; i++) {
	en40_set_offset(&direntry->entry[i], 
	    en40_get_offset(&direntry->entry[i]) + 
	    direntry_hint->count * sizeof(reiserfs_entry40_t));
    }
    
    for (i = pos; i < de40_get_count(direntry); i++) {
	en40_set_offset(&direntry->entry[i], 
	    en40_get_offset(&direntry->entry[i]) + hint->len);
    }
    
    /* Preparing area for new unit headers and unit bodies */
    aal_memmove(&direntry->entry[pos] + direntry_hint->count, 
        &direntry->entry[pos], len1);
	
    if (pos < de40_get_count(direntry)) {
	
	aal_memmove(((char *)direntry) + offset + hint->len - 
	    sizeof(reiserfs_entry40_t), ((char *)direntry) + offset, len2);
    }
    
    /* Creating new entries */
    for (i = 0; i < direntry_hint->count; i++) {
	en40_set_offset(&direntry->entry[pos + i], offset);

	aal_memcpy(&direntry->entry[pos + i].entryid, 
	    &direntry_hint->entry[i].entryid, sizeof(reiserfs_entryid_t));
	
	aal_memcpy(((char *)direntry) + offset, 
	    &direntry_hint->entry[i].objid, sizeof(reiserfs_objid_t));
	
	offset += sizeof(reiserfs_objid_t);
	
	len1 = aal_strlen(direntry_hint->entry[i].name);
	aal_memcpy((char *)(direntry) + offset, 
	    direntry_hint->entry[i].name, len1);
	
	offset += len1;
	
	*((char *)(direntry) + ++offset) = 0;
    }
    
    /* Updating direntry count field */
    de40_set_count(direntry, de40_get_count(direntry) + 
	direntry_hint->count);
    
    return 0;
}

static errno_t direntry40_create(reiserfs_direntry40_t *direntry, 
    reiserfs_item_hint_t *hint)
{
    return direntry40_insert(direntry, 0, hint);
}

#endif

static errno_t direntry40_print(reiserfs_direntry40_t *direntry, 
    char *buff, uint32_t n) 
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
    reiserfs_key_t key;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-657", key1 != NULL, return -1);
    aal_assert("umka-658", key2 != NULL, return -1);
    aal_assert("umka-659", data != NULL, return -1);
    
    plugin = (reiserfs_plugin_t *)data;
    
    libreiser4_plugin_call(return -1, plugin->key_ops, 
	build_by_entry, (void *)&key, (void *)key1);

    return libreiser4_plugin_call(return -1, plugin->key_ops, 
	compare_full, &key, key2);
}

static int direntry40_lookup(reiserfs_direntry40_t *direntry, 
    reiserfs_key_t *key, uint32_t *pos)
{
    int lookup;
    uint64_t unit;
    
    aal_assert("umka-610", key != NULL, return -1);
    aal_assert("umka-717", key->plugin != NULL, return -1);
    
    aal_assert("umka-609", direntry != NULL, return -1);
    aal_assert("umka-629", pos != NULL, return -1);
    
    if ((lookup = reiserfs_misc_bin_search((void *)direntry, 
	    de40_get_count(direntry), key->body, callback_elem_for_lookup, 
	    callback_comp_for_lookup, key->plugin, &unit)) != -1)
	*pos = (uint32_t)unit;

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
	    
	    .estimate = (errno_t (*)(uint32_t, reiserfs_item_hint_t *))
		direntry40_estimate,
	    
	    .insert = (errno_t (*)(const void *, uint32_t, reiserfs_item_hint_t *))
		direntry40_insert,
	    
#else
	    .create = NULL,
	    .estimate = NULL,
	    .insert = NULL,
#endif
	    .minsize = (uint32_t (*)(void))direntry40_minsize,
	    
	    .print = (errno_t (*)(const void *, char *, uint32_t))
		direntry40_print,
	    
	    .lookup = (int (*) (const void *, reiserfs_key_t *, uint32_t *))
		direntry40_lookup,
	    
	    .maxkey = (errno_t (*)(const void *))direntry40_maxkey,
	    
	    .count = (uint32_t (*)(const void *))direntry40_count,
	    .remove = NULL,
	    
	    .confirm = NULL,
	    .check = NULL
	},
	.specific = {
	    .direntry = { 
		.get_entry = (errno_t (*)(const void *, uint32_t, 
		    reiserfs_entry_hint_t *))direntry40_get_entry
	    }
	}
    }
};

static reiserfs_plugin_t *direntry40_entry(reiserfs_core_t *c) {
    core = c;
    return &direntry40_plugin;
}

libreiser4_factory_register(direntry40_entry);

