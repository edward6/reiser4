/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/reiserfs.h>

#include "stat40.h"

#define STAT40_ID 0x0

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_stat40_confirm(reiserfs_coord_t *coord) {
    return 0;
}

static error_t reiserfs_stat40_create(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    reiserfs_stat40_base_t *stat;
    reiserfs_stat_info_t *stat_info;
	
    aal_assert("vpf-075", item_info != NULL, return -1);
    aal_assert("vpf-076", coord != NULL, return -1); 
    aal_assert("vpf-077", coord->node != NULL, return -1);
    aal_assert("vpf-078", item_info->info != NULL, return -1);
    
    stat_info = item_info->info;
	
    stat = coord->node->plugin->node.item(coord->node, coord->item_pos);
    stat40_set_mode(stat, stat_info->mode);
    stat40_set_extmask(stat, stat_info->extmask);
    stat40_set_nlink(stat, stat_info->nlink);
    stat40_set_size(stat, stat_info->size);
  
    /* And its extentions should be created here also. */
    
    return 0;
}

static error_t reiserfs_stat40_estimate(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    aal_assert("vpf-074", item_info != NULL, return -1);
    /* coord cannot be not NULL, because we cannot paste into internal40 */
    aal_assert("vpf-117", coord == NULL, return -1);
    
    /* should calculate extentions size also */
    item_info->length = sizeof(reiserfs_stat40_base_t);

    return 0;
}

static error_t reiserfs_stat40_check(reiserfs_coord_t *coord) {
    return 0;
}

static void reiserfs_stat40_print(reiserfs_coord_t *coord, char *buff) {
}

static reiserfs_plugin_t stat40_plugin = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = STAT40_ID,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "stat40",
	    .desc = "Stat data for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .item_type = STAT_DATA_ITEM,
	    .create = (error_t (*)(reiserfs_opaque_t *coord, reiserfs_opaque_t *))
		reiserfs_stat40_create,
	    .open = NULL,
	    .close = NULL,
	    .lookup = NULL,
	    .add_unit = NULL,
	    .confirm = (error_t (*)(reiserfs_opaque_t *))reiserfs_stat40_confirm,
	    .check = (error_t (*)(reiserfs_opaque_t *))reiserfs_stat40_check,
	    .print = (void (*)(reiserfs_opaque_t *, char *))reiserfs_stat40_print,
	    .units_count = NULL,
	    .remove_units = NULL,
	    .estimate = (error_t (*)(reiserfs_opaque_t *, reiserfs_opaque_t *))
		reiserfs_stat40_estimate,
	    .is_internal = NULL
	},
	.specific = {
	    .stat = { }
	}
    }
};

reiserfs_plugin_t *reiserfs_stat40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &stat40_plugin;
}

reiserfs_plugin_register(reiserfs_stat40_entry);

