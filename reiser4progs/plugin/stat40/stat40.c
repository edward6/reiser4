/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>

#include "stat40.h"

#define STAT40_ID 0x0

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_stat40_confirm(void *body) {
    return 0;
}

static error_t reiserfs_stat40_create(void *body, reiserfs_item_info_t *item_info) {
    reiserfs_stat40_base_t *stat;
    reiserfs_stat_info_t *stat_info;
    
    aal_assert("vpf-076", body != NULL, return -1); 
    aal_assert("vpf-075", item_info != NULL, return -1);
    aal_assert("vpf-078", item_info->info != NULL, return -1);
    
    stat_info = item_info->info;
    stat = (reiserfs_stat40_base_t *)body;
    
    stat40_set_mode(stat, stat_info->mode);
    stat40_set_extmask(stat, stat_info->extmask);
    stat40_set_nlink(stat, stat_info->nlink);
    stat40_set_size(stat, stat_info->size);
  
    /* And its extentions should be created here also. */
    
    return 0;
}

static void reiserfs_stat40_estimate(void *body, 
    reiserfs_item_info_t *item_info, reiserfs_item_coord_t *coord) {
    aal_assert("vpf-074", item_info != NULL, return);

    /* Should calculate extentions size also */
    item_info->length = sizeof(reiserfs_stat40_base_t);
}

static error_t reiserfs_stat40_check(void *body) {
    return 0;
}

static void reiserfs_stat40_print(void *body, char *buff, uint16_t n) {
    aal_assert("umka-546", body != NULL, return);
    aal_assert("umka-547", buff != NULL, return);
}

static uint32_t reiserfs_stat40_minsize(void *body) {
    return sizeof(reiserfs_stat40_base_t);
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
	    .type = STAT_DATA_ITEM,
	    
	    .create = (error_t (*)(void *, void *))reiserfs_stat40_create,
	    .confirm = (error_t (*)(void *))reiserfs_stat40_confirm,
	    .check = (error_t (*)(void *))reiserfs_stat40_check,
	    .print = (void (*)(void *, char *, uint16_t))reiserfs_stat40_print,
	    .estimate = (void (*)(void *, void *, reiserfs_item_coord_t *))reiserfs_stat40_estimate,
	    .minsize = (uint32_t (*)(void *))reiserfs_stat40_minsize,

	    .lookup = NULL,
	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL,

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

