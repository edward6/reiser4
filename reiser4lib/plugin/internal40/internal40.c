/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "internal40.h"

#define INTERNAL40_ID 0x0

static reiserfs_plugins_factory_t *factory = NULL;

static reiserfs_plugin_t internal40_plugin = {
    .item = {
	.h = {
    	    .handle = NULL,
	    .id = INTERNAL_ITEM,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "internal40",
	    .desc = "Internal item for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .item_type = INTERNAL40_ID,
	    .create = NULL,
	    .open = NULL,
	    .close = NULL,
	    .lookup = NULL,
	    .add_unit = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .print = NULL,
	    .units_count = NULL,
	    .remove_units = NULL,
	    .estimate = NULL,
	    .is_internal = NULL		
	},
	.specific = {
	    .internal = {
		.down_link = NULL, 
		.has_pointer_to = NULL
	    }
	}
    }
};

reiserfs_plugin_t *reiserfs_internal40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &internal40_plugin;
}

reiserfs_plugin_register(reiserfs_internal40_entry);
