/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/reiserfs.h>

#include "direntry40.h"

static void build_key_by_objid(reiserfs_key_t *key, reiserfs_objid_t *id)
{
    aal_assert("vpf-086", key != NULL, return);
    aal_assert("vpf-087", id != NULL, return);
    
    aal_memcpy(key, id, sizeof *id);
}

static void build_objid_by_key(reiserfs_objid_t *id, reiserfs_key_t *key)
{
    aal_assert("vpf-088", key != NULL, return);
    aal_assert("vpf-089", id != NULL, return);

    aal_memcpy (id, key, sizeof *id);
}

static void build_key_by_dirid(reiserfs_key_t *key, reiserfs_dirid_t *dirid)
{
    aal_assert("vpf-090", key != NULL, return);
    aal_assert("vpf-091", dirid != NULL, return);

    aal_memcpy (&key[1], dirid, sizeof *dirid);
}

static void build_dirid_by_key(reiserfs_dirid_t *dirid, reiserfs_key_t *key)
{
    aal_assert("vpf-092", key != NULL, return);
    aal_assert("vpf-093", dirid != NULL, return);

    aal_memcpy (dirid, &key[1], sizeof *dirid);
}

static error_t direntry40_create (reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    return 0;
}

static error_t direntry40_estimate(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
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

