/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include "internal40.h"

#define INTERNAL40_ID 0x3

/*
    All items operate on given memory area "body". The highest
    level should be concering about validness this memory area.
*/

static reiserfs_plugins_factory_t *factory = NULL;

/* Forms internal item in given memory area */
static error_t reiserfs_internal40_create(void *body, 
    reiserfs_item_info_t *item_info) 
{
    reiserfs_internal40_t *inter;
    reiserfs_internal_info_t *inter_info; 
    
    aal_assert("vpf-063", body != NULL, return -1); 
    aal_assert("vpf-064", item_info != NULL, return -1);
    aal_assert("vpf-065", item_info->info != NULL, return -1);

    inter_info = item_info->info; 
    inter = (reiserfs_internal40_t *)body;
    int40_set_blk(inter, *inter_info->blk);
	    
    return 0;
}

static uint32_t reiserfs_internal40_minsize(void *body) {
    return sizeof(reiserfs_internal40_t);
}

static void reiserfs_internal40_estimate(reiserfs_item_info_t *item_info, 
    reiserfs_item_coord_t *coord) 
{
    aal_assert("vpf-068", item_info != NULL, return);
    item_info->length = sizeof(reiserfs_internal40_t);
}

static int reiserfs_internal40_is_internal(void) {
    return 1;
}

static void reiserfs_internal40_print(void *body, char *buff, uint16_t n) {
    aal_assert("umka-544", body != NULL, return);
    aal_assert("umka-545", buff != NULL, return);
}

static reiserfs_plugin_t internal40_plugin = {
    .item = {
	.h = {
    	    .handle = NULL,
	    .id = INTERNAL40_ID,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "internal40",
	    .desc = "Internal item for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .type = INTERNAL_ITEM,
	    
	    .create = (error_t (*)(void *, void *))reiserfs_internal40_create,
	    
	    .estimate = (void (*)(void *, reiserfs_item_coord_t *))
		reiserfs_internal40_estimate,
	    
	    .minsize = (uint32_t (*)(void *))reiserfs_internal40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))reiserfs_internal40_print,
	    
	    .lookup = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL,
    
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

