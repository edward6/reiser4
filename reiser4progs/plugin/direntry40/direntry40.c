/*
    direntry40.c -- reiser4 default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include <misc/misc.h>

#include "direntry40.h"

static reiser4_core_t *core = NULL;

static uint32_t direntry40_count(reiser4_body_t *body) {
    aal_assert("umka-865", body != NULL, return 0);
    return de40_get_count((reiser4_direntry40_t *)body);
}

static errno_t direntry40_entry(reiser4_body_t *body, 
    uint32_t pos, reiser4_entry_hint_t *entry)
{
    uint32_t offset;
    reiser4_entry40_t *en;
    reiser4_objid_t *objid;
    
    aal_assert("umka-866", body != NULL, return -1);
    
    if (pos > direntry40_count(body))
	return -1;
    
    offset = sizeof(reiser4_direntry40_t) + 
	pos * sizeof(reiser4_entry40_t);
    
    en = (reiser4_entry40_t *)(((char *)body) + offset);
    
    entry->entryid.objectid = eid_get_objectid((reiser4_entryid_t *)(&en->entryid));
    entry->entryid.offset = eid_get_offset((reiser4_entryid_t *)(&en->entryid));
    
    offset = en40_get_offset(en); 
    objid = (reiser4_objid_t *)(((char *)body) + offset);
    
    entry->objid.objectid = oid_get_objectid(objid);
    entry->objid.locality = oid_get_locality(objid);

    offset += sizeof(*objid);
    entry->name = ((char *)body) + offset;
    
    return 0;
}

#ifndef ENABLE_COMPACT

static errno_t direntry40_estimate(uint32_t pos, reiser4_item_hint_t *hint) {
    int i;
    reiser4_direntry_hint_t *direntry_hint;
	    
    aal_assert("vpf-095", hint != NULL, return -1);
    
    direntry_hint = (reiser4_direntry_hint_t *)hint->hint;
    hint->len = direntry_hint->count * sizeof(reiser4_entry40_t);
    
    for (i = 0; i < direntry_hint->count; i++) {
	hint->len += aal_strlen(direntry_hint->entry[i].name) + 
	    sizeof(reiser4_objid_t) + 1;
    }

    if (pos == ~0ul)
	hint->len += sizeof(reiser4_direntry40_t);
    
    return 0;
}

static uint32_t direntry40_unitlen(reiser4_direntry40_t *direntry, 
    uint32_t pos) 
{
    char *name;
    uint32_t offset;
    
    aal_assert("umka-936", 
	pos < de40_get_count(direntry), return 0);
    
    offset = en40_get_offset(&direntry->entry[pos]);
    name = (char *)(((char *)direntry) + offset + sizeof(reiser4_objid_t));
	    
    return (aal_strlen(name) + sizeof(reiser4_objid_t) + 1);
}

static errno_t direntry40_insert(reiser4_body_t *body, uint32_t pos, 
    reiser4_item_hint_t *hint)
{
    uint32_t i, offset;
    uint32_t len_before = 0;
    uint32_t len_after = 0;
    
    reiser4_direntry40_t *direntry;
    reiser4_direntry_hint_t *direntry_hint;
    
    aal_assert("umka-791", body != NULL, return -1);
    aal_assert("umka-792", hint != NULL, return -1);
    aal_assert("umka-897", pos != ~0ul, return -1);

    direntry = (reiser4_direntry40_t *)body;
    direntry_hint = (reiser4_direntry_hint_t *)hint->hint;
    
    if (pos > de40_get_count(direntry))
	return -1;
    
    /* Getting offset area of new entry body will be created at */
    if (de40_get_count(direntry) > 0) {
	if (pos < de40_get_count(direntry)) {
	    offset = en40_get_offset(&direntry->entry[pos]) + 
		(direntry_hint->count * sizeof(reiser4_entry40_t));
	} else {
	    offset = en40_get_offset(&direntry->entry[de40_get_count(direntry) - 1]);
	    offset += sizeof(reiser4_entry40_t) + 
		direntry40_unitlen(direntry, de40_get_count(direntry) - 1);
	}
    } else
	offset = sizeof(reiser4_direntry40_t) + 
	    direntry_hint->count * sizeof(reiser4_entry40_t);
    
    if (direntry40_estimate(pos, hint))
	return -1;
    
    /* Calculating length of areas to be moved */
    len_before = (de40_get_count(direntry) - pos)*sizeof(reiser4_entry40_t);
	
    for (i = 0; i < pos; i++)
	len_before += direntry40_unitlen(direntry, i);
	
    for (i = pos; i < de40_get_count(direntry); i++)
	len_after += direntry40_unitlen(direntry, i);
	
    /* Updating offsets */
    for (i = 0; i < pos; i++) {
	en40_set_offset(&direntry->entry[i], 
	    en40_get_offset(&direntry->entry[i]) + 
	    direntry_hint->count * sizeof(reiser4_entry40_t));
    }
    
    for (i = pos; i < de40_get_count(direntry); i++) {
	en40_set_offset(&direntry->entry[i], 
	    en40_get_offset(&direntry->entry[i]) + hint->len);
    }
    
    /* Moving unit bodies */
    if (pos < de40_get_count(direntry)) {
	uint32_t headers = (direntry_hint->count * sizeof(reiser4_entry40_t));
		
	aal_memmove(((char *)direntry) + offset + hint->len - headers, 
	    ((char *)direntry) + offset - headers, len_after + headers);
    }
    
    /* Moving unit headers headers */
    if (len_before) {
	aal_memmove(&direntry->entry[pos] + direntry_hint->count, 
	    &direntry->entry[pos], len_before);
    }
    
    /* Creating new entries */
    for (i = 0; i < direntry_hint->count; i++) {
	en40_set_offset(&direntry->entry[pos + i], offset);

	aal_memcpy(&direntry->entry[pos + i].entryid, 
	    &direntry_hint->entry[i].entryid, sizeof(reiser4_entryid_t));
	
	aal_memcpy(((char *)direntry) + offset, 
	    &direntry_hint->entry[i].objid, sizeof(reiser4_objid_t));
	
	offset += sizeof(reiser4_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, 
	    direntry_hint->entry[i].name, 
	    aal_strlen(direntry_hint->entry[i].name));

	offset += aal_strlen(direntry_hint->entry[i].name);
	*((char *)(direntry) + offset) = '\0';
	offset++;
    }
    
    /* Updating direntry count field */
    de40_set_count(direntry, de40_get_count(direntry) + 
	direntry_hint->count);
    
    return 0;
}

static errno_t direntry40_init(reiser4_body_t *body, 
    reiser4_item_hint_t *hint)
{
    aal_assert("umka-1010", body != NULL, return -1);

    de40_set_count((reiser4_direntry40_t *)body, 0);
    return direntry40_insert(body, 0, hint);
}

static uint32_t direntry40_remove(reiser4_body_t *body, 
    uint32_t pos)
{
    uint32_t i, head_len;
    uint32_t offset, rem_len;

    reiser4_direntry40_t *direntry;
    
    aal_assert("umka-934", body != NULL, return 0);

    direntry = (reiser4_direntry40_t *)body;
    
    if (pos >= de40_get_count(direntry))
	return 0;

    offset = en40_get_offset(&direntry->entry[pos]);
    head_len = offset - sizeof(reiser4_entry40_t) -
	(((char *)&direntry->entry[pos]) - ((char *)direntry));

    rem_len = direntry40_unitlen(direntry, pos);

    aal_memmove(&direntry->entry[pos], 
	&direntry->entry[pos + 1], head_len);

    for (i = 0; i < pos; i++) {
	en40_set_offset(&direntry->entry[i], 
	    en40_get_offset(&direntry->entry[i]) - 
	    sizeof(reiser4_entry40_t));
    }
    
    if (pos < (uint32_t)de40_get_count(direntry) - 1) {
	uint32_t foot_len = 0;
	
	offset = en40_get_offset(&direntry->entry[pos]);
	
	for (i = pos; i < (uint32_t)de40_get_count(direntry) - 1; i++)
	    foot_len += direntry40_unitlen(direntry, i);
	
	aal_memmove((((char *)direntry) + offset) - (sizeof(reiser4_entry40_t) +
	    rem_len), ((char *)direntry) + offset, foot_len);

	for (i = pos; i < (uint32_t)de40_get_count(direntry) - 1; i++) {
	    en40_set_offset(&direntry->entry[i], 
		en40_get_offset(&direntry->entry[i]) - 
		(sizeof(reiser4_entry40_t) + rem_len));
	}
    }
    
    de40_set_count(direntry, de40_get_count(direntry) - 1);
    
    return rem_len + sizeof(reiser4_entry40_t);
}

#endif

static errno_t direntry40_print(reiser4_body_t *body, 
    char *buff, uint32_t n) 
{
    aal_assert("umka-548", body != NULL, return -1);
    aal_assert("umka-549", buff != NULL, return -1);

    return -1;
}

static int direntry40_internal(void) {
    return 0;
}

static int direntry40_compound(void) {
    return 1;
}

/* 
    Helper function that is used by lookup method for getting n-th element of 
    direntry.
*/
static void *callback_elem_for_lookup(void *direntry, 
    uint32_t pos, void *data) 
{
    return &((reiser4_direntry40_t *)direntry)->entry[pos].entryid;
}

/* 
    Helper function that is used by lookup method for comparing given key with 
    passed dirid.
*/
static int callback_comp_for_lookup(void *key1, void *key2, void *data) {
    oid_t locality;
    reiser4_key_t key;
    reiser4_plugin_t *plugin;

    aal_assert("umka-657", key1 != NULL, return -1);
    aal_assert("umka-658", key2 != NULL, return -1);
    aal_assert("umka-659", data != NULL, return -1);
    
    plugin = (reiser4_plugin_t *)data;
    
    libreiser4_plugin_call(return -1, plugin->key_ops, 
	build_by_entry, (void *)&key, (void *)key1);

    locality = libreiser4_plugin_call(return -1, plugin->key_ops,
	get_locality, key2);

    libreiser4_plugin_call(return -1, plugin->key_ops,
	set_locality, (void *)&key, locality);
    
    return libreiser4_plugin_call(return -1, plugin->key_ops, 
	compare, &key, key2);
}

static int direntry40_lookup(reiser4_body_t *body, 
    reiser4_key_t *key, uint32_t *pos)
{
    int lookup;
    uint64_t unit;

    aal_assert("umka-610", key != NULL, return -1);
    aal_assert("umka-717", key->plugin != NULL, return -1);
    
    aal_assert("umka-609", body != NULL, return -1);
    aal_assert("umka-629", pos != NULL, return -1);
    
    if ((lookup = reiser4_misc_bin_search((void *)body, direntry40_count(body), 
	    key->body, callback_elem_for_lookup, callback_comp_for_lookup, 
	    key->plugin, &unit)) != -1)
	*pos = (uint32_t)unit;

    return lookup;
}

static errno_t direntry40_maxkey(reiser4_key_t *key) {
    oid_t offset;
    oid_t objectid;
    reiser4_body_t *maxkey;
    
    aal_assert("umka-716", key->plugin != NULL, return -1);

    maxkey = libreiser4_plugin_call(return -1, key->plugin->key_ops,
	maximal,);
    
    objectid = key->plugin->key_ops.get_objectid(maxkey);
    offset = key->plugin->key_ops.get_objectid(maxkey);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops,
	set_objectid, key->body, objectid);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_offset, key->body, offset);
    
    return 0;
}

static reiser4_plugin_t direntry40_plugin = {
    .item_ops = {
	.h = {
	    .handle = NULL,
	    .id = ITEM_CDE40_ID,
	    .type = ITEM_PLUGIN_TYPE,
	    .label = "direntry40",
	    .desc = "Compound direntry for reiserfs 4.0, ver. " VERSION,
	},
	.common = {
#ifndef ENABLE_COMPACT	    
	    .init	= direntry40_init,
	    .insert	= direntry40_insert,
	    .remove	= direntry40_remove,
	    .estimate	= direntry40_estimate,
#else
	    .create	= NULL,
	    .estimate	= NULL,
	    .insert	= NULL,
	    .remove	= NULL,
#endif
	    .confirm	= NULL,
	    .valid	= NULL,
	    
	    .print	= direntry40_print,
	    .lookup	= direntry40_lookup,
	    .maxkey	= direntry40_maxkey,
	    .count	= direntry40_count,
	   
	    .internal	= direntry40_internal,
	    .compound	= direntry40_compound
	},
	.specific = {
	    .direntry = { 
		.entry = direntry40_entry
	    }
	}
    }
};

static reiser4_plugin_t *direntry40_start(reiser4_core_t *c) {
    core = c;
    return &direntry40_plugin;
}

libreiser4_factory_register(direntry40_start);

