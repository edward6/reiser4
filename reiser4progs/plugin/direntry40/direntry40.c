/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/reiserfs.h>

#include "direntry40.h"

/* This will build sd key as objid has SD_MINOR */
static void build_key_by_objid(reiserfs_key_t *key, reiserfs_objid_t *id)
{
    aal_assert("vpf-086", key != NULL, return);
    aal_assert("vpf-087", id != NULL, return);
   
    aal_memset(key, 0, sizeof *key);    
    aal_memcpy(key, id, sizeof *id);
}

/* Will build sd key. */
static void build_objid_by_ids(reiserfs_objid_t *objid, uint64_t loc, uint64_t id)
{
    reiserfs_key_t sd_key;
    
    aal_assert("vpf-089", objid != NULL, return);

    reiserfs_key_init(&sd_key);
    set_key_locality(&sd_key, loc);
    set_key_objectid(&sd_key, id);
    set_key_type(&sd_key, KEY_SD_MINOR);
    
    aal_memcpy (objid, &sd_key, sizeof *objid);
}

static error_t direntry40_create (reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    int i;
    uint16_t len, offset;
    reiserfs_direntry40_t *body;
    reiserfs_dir_info_t *info;    
    
    aal_assert("vpf-097", coord != NULL, return -1);
    aal_assert("vpf-098", item_info != NULL, return -1);
    aal_assert("vpf-099", item_info->info != NULL, return -1);
    
    info = item_info->info;
    
    reiserfs_check_method(coord->node->plugin->node, item, return -1);
    body = coord->node->plugin->node.item(coord->node, coord->item_pos);
    
    direntry40_set_count(body, info->count);
    
    offset = sizeof(reiserfs_direntry40_t) + 
	info->count * sizeof(reiserfs_entry40_t);

    for (i = 0; i < info->count; i++) {	
	entry40_set_offset(&body->entry[i], offset);
	build_entryid_by_entry_info(&body->entry[i].entryid, &info->entry[i]);
	build_objid_by_ids((reiserfs_objid_t *)((char *)body + offset), 
	    info->entry[i].parent_id, info->entry[i].object_id);
	
	len = aal_strlen(info->entry[i].name);
	aal_memcpy(body + offset + sizeof(reiserfs_objid_t), 
	    &info->entry[i].name, len);
	*((char *)body + offset + sizeof(reiserfs_objid_t) + 1) = 0;

	offset += len + 1;
    }
    
    return 0;
}

static error_t direntry40_estimate(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    int i;
    reiserfs_dir_info_t *info;    
	    
    aal_assert("vpf-094", coord != NULL, return -1);
    aal_assert("vpf-095", item_info != NULL, return -1);
    aal_assert("vpf-096", item_info->info != NULL, return -1);
    
    info = item_info->info;
    item_info->length = info->count * sizeof(reiserfs_direntry40_t);
    
    for (i = 0; i < info->count; i++)
	item_info->length += aal_strlen(info->entry[i].name) + 1;
    
    if (coord == NULL)
	item_info->length += sizeof(reiserfs_direntry40_t);
	
    return 0;    
}

static reiserfs_plugins_factory_t *factory = NULL;

#define	DIRENTRY40_ID 0x0

static reiserfs_plugin_t direntry40_plugin = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = DIR_ENTRY_ITEM,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "direntry40",
	    .desc = "Directory plugin for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .item_type = DIRENTRY40_ID,
	    .create = (error_t (*)(reiserfs_opaque_t *coord, reiserfs_opaque_t *))
		direntry40_create,
	    .open = NULL,
	    .close = NULL,
	    .lookup = NULL,
	    .add_unit = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .print = NULL,
	    .units_count = NULL,
	    .remove_units = NULL,
	    .estimate = (error_t (*)(reiserfs_opaque_t *, reiserfs_opaque_t *))
		direntry40_estimate,
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

