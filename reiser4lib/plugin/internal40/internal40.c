/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "internal40.h"

#define INTERNAL40_ID 0x0

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_internal40_create(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    reiserfs_internal40_t *inter;
    reiserfs_internal_info_t *inter_info; 
    
    aal_assert("vpf-063", coord != NULL, return -1); 
    aal_assert("vpf-064", item_info != NULL, return -1);
    aal_assert("vpf-065", coord->node != NULL, return -1);
    aal_assert("vpf-065", item_info->info != NULL, return -1);

    inter_info = item_info->info; 
    
    inter = coord->node->plugin->node.item(coord->node, coord->item_pos);
    int40_set_blk(inter, *inter_info->block);
	    
    return 0;
}

static error_t reiserfs_internal40_estimate (reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    aal_assert("vpf-068", item_info != NULL, return -1);

    item_info->length = sizeof(reiserfs_internal40_t);
    return 0;
}

static int reiserfs_internal40_is_internal () {
    return 1;
}



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
	    .create = (error_t (*)(reiserfs_opaque_t *, reiserfs_opaque_t *))
		reiserfs_internal40_create,
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
