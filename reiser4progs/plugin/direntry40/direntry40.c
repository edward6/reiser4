/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>

#include "direntry40.h"

#define	DIRENTRY40_ID 0x2

static reiserfs_plugins_factory_t *factory = NULL;

/* This will build sd key as objid has SD_MINOR */
static void build_key_by_objid(reiserfs_key_t *key, 
    reiserfs_objid_t *id)
{
    aal_assert("vpf-086", key != NULL, return);
    aal_assert("vpf-087", id != NULL, return);
   
    aal_memset(key, 0, sizeof *key);    
    aal_memcpy(key, id, sizeof *id);
}

/* Will build sd key. */
static void build_objid_by_ids(reiserfs_objid_t *objid, 
    uint64_t loc, uint64_t id)
{
    reiserfs_key_t sd_key;
    
    aal_assert("vpf-089", objid != NULL, return);

    reiserfs_key_init(&sd_key);
    set_key_locality(&sd_key, loc);
    set_key_objectid(&sd_key, id);
    set_key_type(&sd_key, KEY_SD_MINOR);
    
    aal_memcpy (objid, &sd_key, sizeof *objid);
}

static error_t reiserfs_direntry40_create(void *body, 
    reiserfs_item_info_t *item_info)
{
    int i;
    uint16_t len, offset;
    reiserfs_dir_info_t *info;    
    reiserfs_direntry40_t *direntry;
    
    aal_assert("vpf-097", body != NULL, return -1);
    aal_assert("vpf-098", item_info != NULL, return -1);
    aal_assert("vpf-099", item_info->info != NULL, return -1);
    
    info = item_info->info;
    
    direntry = (reiserfs_direntry40_t *)body;
    direntry40_set_count(direntry, info->count);
    
    offset = sizeof(reiserfs_direntry40_t) + 
	info->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < info->count; i++) {	
	entry40_set_offset(&direntry->entry[i], offset);
	
	build_entryid_by_entry_info(&direntry->entry[i].entryid, &info->entry[i]);
	build_objid_by_ids((reiserfs_objid_t *)((char *)direntry + offset), 
	    info->entry[i].parent_id, info->entry[i].object_id);
	
	len = aal_strlen(info->entry[i].name);
	offset += sizeof(reiserfs_objid_t);
	
	aal_memcpy((char *)(direntry) + offset, info->entry[i].name, len);
	offset += len;
	
	*((char *)(direntry) + offset + 1) = 0;
	offset++;
    }
    
    return 0;
}

static void reiserfs_direntry40_estimate(void *body, 
    reiserfs_item_info_t *item_info, reiserfs_item_coord_t *coord) 
{
    int i;
    reiserfs_dir_info_t *info;    
	    
    aal_assert("umka-540", body != NULL, return);
    aal_assert("vpf-095", item_info != NULL, return);
    aal_assert("vpf-096", item_info->info != NULL, return);
    
    info = item_info->info;
    item_info->length = info->count * sizeof(reiserfs_entry40_t);
    
    for (i = 0; i < info->count; i++) {
	item_info->length += aal_strlen(info->entry[i].name) + 
	    sizeof(reiserfs_objid_t) + 1;
    }

    if (coord == NULL || coord->unit_pos == -1)
	item_info->length += sizeof(reiserfs_direntry40_t);
}

static void reiserfs_direntry40_print(void *body, char *buff, uint16_t n) {
    aal_assert("umka-548", body != NULL, return);
    aal_assert("umka-549", buff != NULL, return);
}

static uint32_t reiserfs_direntry40_minsize(void *body) {
    aal_assert("umka-550", body != NULL, return 0);
    return sizeof(reiserfs_direntry40_t);
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
	    .type = DIR_ENTRY_ITEM,
	    
	    .create = (error_t (*)(void *, void *))reiserfs_direntry40_create,
	    
	    .estimate = (void (*)(void *, void *, reiserfs_item_coord_t *))
		reiserfs_direntry40_estimate,
	    
	    .minsize = (uint32_t (*)(void *))reiserfs_direntry40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))reiserfs_direntry40_print,
	    
	    .lookup = NULL,
	    .confirm = NULL,
	    .check = NULL,

	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL,
	    
	    .is_internal = NULL
	},
	.specific = {
	    .dir = { 
		.add_entry = NULL, 
		.max_name_len = NULL
	    }
	}
    }
};

reiserfs_plugin_t *reiserfs_direntry40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &direntry40_plugin;
}

reiserfs_plugin_register(reiserfs_direntry40_entry);

