/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include <misc/misc.h>

#include "direntry40.h"

#define	DIRENTRY40_ID 0x2

static reiserfs_plugins_factory_t *factory = NULL;

/* This will build sd key as objid has SD_MINOR */
static void build_key_by_objid(reiserfs_key40_t *key, 
    reiserfs_objid_t *id)
{
    aal_assert("vpf-086", key != NULL, return);
    aal_assert("vpf-087", id != NULL, return);
   
    aal_memset(key, 0, sizeof(*key));    
    aal_memcpy(key, id, sizeof(*id));
}

/* Will build sd key. */
static void build_objid_by_ids(reiserfs_objid_t *objid, 
    uint64_t loc, uint64_t id)
{
    reiserfs_key40_t sd_key;
    
    aal_assert("vpf-089", objid != NULL, return);

    reiserfs_key40_init(&sd_key);
    set_key40_locality(&sd_key, loc);
    set_key40_objectid(&sd_key, id);
    set_key40_type(&sd_key, KEY40_SD_MINOR);
    
    aal_memcpy(objid, &sd_key, sizeof(*objid));
}

static error_t reiserfs_direntry40_create(reiserfs_direntry40_t *direntry, 
    reiserfs_item_info_t *info)
{
    int i;
    uint16_t len, offset;
    reiserfs_direntry_info_t *direntry_info;
    
    aal_assert("vpf-097", direntry != NULL, return -1);
    aal_assert("vpf-098", info != NULL, return -1);
    aal_assert("vpf-099", info->info != NULL, return -1);
    
    direntry_info = info->info;
    
    direntry40_set_count(direntry, direntry_info->count);
    
    offset = sizeof(reiserfs_direntry40_t) + 
	direntry_info->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < direntry_info->count; i++) {	
	entry40_set_offset(&direntry->entry[i], offset);
	
	build_entryid_by_info(&direntry->entry[i].entryid, &direntry_info->entry[i]);
	build_objid_by_ids((reiserfs_objid_t *)((char *)direntry + offset), 
	    direntry_info->entry[i].parent_id, direntry_info->entry[i].object_id);
	
	len = aal_strlen(direntry_info->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, 
	    direntry_info->entry[i].name, len);
	
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static void reiserfs_direntry40_estimate(reiserfs_item_info_t *info, 
    reiserfs_item_coord_t *coord) 
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

static void reiserfs_direntry40_print(reiserfs_direntry40_t *direntry, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-548", direntry != NULL, return);
    aal_assert("umka-549", buff != NULL, return);
}

static uint32_t reiserfs_direntry40_minsize(void) {
    return sizeof(reiserfs_direntry40_t);
}

/* 
    Helper function that is used by lookup method 
    for getting n-th element of direntry.
*/
static void *callback_key_at(reiserfs_direntry40_t *direntry, 
    uint32_t pos) 
{
    return &direntry->entry[pos].entryid;
}

/* 
    Helper function that is used by lookup method
    for comparing given key with passed dirid.
*/
static int callback_key_cmp(reiserfs_entryid_t *dirid, 
    reiserfs_key40_t *key)
{
    reiserfs_key40_t entkey;

    aal_memset(&entkey, 0, sizeof(entkey));
    
    aal_memcpy(((uint64_t *)&entkey) + 1, dirid, sizeof(entkey));
    set_key40_locality(&entkey, get_key40_locality(key));
    set_key40_type(&entkey, KEY40_FILE_NAME_MINOR);

    return reiserfs_key40_cmp(&entkey, key);
}

static int reiserfs_direntry40_lookup(reiserfs_direntry40_t *direntry, 
    reiserfs_key40_t *key, reiserfs_item_coord_t *coord)
{
    int found;
    uint64_t pos;
    
    aal_assert("umka-609", direntry != NULL, return 0);
    aal_assert("umka-610", key != NULL, return 0);
    aal_assert("umka-629", coord != NULL, return 0);
    
    if ((found = reiserfs_misc_bin_search((void *)direntry, 
	    direntry->count, key, (void *(*)(void *, uint32_t))callback_key_at, 
	    (int (*)(const void *, const void *))callback_key_cmp, &pos)) == -1)
	return -1;

    coord->unit_pos = (uint32_t)pos;

    return found;
}

static int reiserfs_direntry40_internal(void) {
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
	    
	    .create = (error_t (*)(void *, void *))reiserfs_direntry40_create,
	    .estimate = (void (*)(void *, void *))reiserfs_direntry40_estimate,
	    .minsize = (uint32_t (*)(void))reiserfs_direntry40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))reiserfs_direntry40_print,
	    .lookup = (int (*) (void *, void *, void *))reiserfs_direntry40_lookup,
	    .internal = (int (*)(void))reiserfs_direntry40_internal,
	    
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

reiserfs_plugin_t *reiserfs_direntry40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &direntry40_plugin;
}

libreiserfs_plugins_register(reiserfs_direntry40_entry);

