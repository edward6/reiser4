/*
    sd40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "sd40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_sd40_confirm(reiserfs_sd40_t *sd) {
    return 0;
}

static reiserfs_sd40_t *reiserfs_sd40_create(reiserfs_key_t *key) {
    reiserfs_sd40_t *sd;
    

    if (!(sd = aal_calloc(sizeof(*sd), 0)))
	return NULL;
    
    set_key_type(key, KEY_SD_MINOR);
    set_key_locality(key, 1ull);
    set_key_objectid(key, 2ull);
		
    return sd;
}

static error_t reiserfs_sd40_check(reiserfs_sd40_t *sd) {
    return 0;
}

static void reiserfs_sd40_print(reiserfs_sd40_t *sd) {
}



#define STATIC_SD40_ID 0x1

static reiserfs_plugin_t sd40_plugin = {
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
	    .item_type = STATIC_SD40_ID,
	    .create = (reiserfs_opaque_t *(*)(reiserfs_key_t *key))reiserfs_sd40_create,
	    .paste = NULL,
	    .confirm_format = (error_t (*)(reiserfs_opaque_t *))reiserfs_sd40_confirm,
	    .check = (error_t (*)(reiserfs_opaque_t *))reiserfs_sd40_check,
	    .print = (void (*)(reiserfs_opaque_t *))reiserfs_sd40_print,
	    .nr_units = NULL,
	    .remove_units = NULL,
	    .estimate = NULL
	},
	.ops = {
	    .sd = {
	    }
	}
    }
};

reiserfs_plugin_t *reiserfs_sd40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &sd40_plugin;
}

reiserfs_plugin_register(reiserfs_sd40_entry);

