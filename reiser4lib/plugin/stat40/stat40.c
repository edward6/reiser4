/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/reiserfs.h>

#include "stat40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_stat40_confirm(reiserfs_stat40_t *stat) {
    return 0;
}

static error_t reiserfs_stat40_create(reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info) 
{
    return 0;
}

static error_t reiserfs_stat40_check(reiserfs_stat40_t *stat) {
    return 0;
}

static void reiserfs_stat40_print(reiserfs_stat40_t *stat, char *buff) {
}

#define STAT40_ID 0x0

static reiserfs_plugin_t stat40_plugin = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = STAT_DATA_ITEM,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "StatData40",
	    .desc = "Stat Data for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .item_type = STAT40_ID,
	    .create = (error_t (*)(reiserfs_opaque_t *coord, 
		reiserfs_opaque_t *item_info))reiserfs_stat40_create,
	    .open = NULL,
	    .close = NULL,
	    .lookup = NULL,
	    .add_unit = NULL,
	    .confirm = (error_t (*)(reiserfs_opaque_t *))reiserfs_stat40_confirm,
	    .check = (error_t (*)(reiserfs_opaque_t *))reiserfs_stat40_check,
	    .print = (void (*)(reiserfs_opaque_t *, char *))reiserfs_stat40_print,
	    .units_count = NULL,
	    .remove_units = NULL,
	    .estimate = NULL,
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

