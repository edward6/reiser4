/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include "internal40.h"

#define INTERNAL40_ID 0x3

/*
    All items operate on given memory area. The highest
    level should be concering about validness this memory area.
*/

static reiserfs_plugin_factory_t *factory = NULL;

/* Forms internal item in given memory area */
static error_t internal40_create(internal40_t *internal, 
    reiserfs_item_info_t *info) 
{
    reiserfs_internal_info_t *inter_info; 
    
    aal_assert("vpf-063", internal != NULL, return -1); 
    aal_assert("vpf-064", info != NULL, return -1);
    aal_assert("vpf-065", info->info != NULL, return -1);

    inter_info = info->info; 
    int40_set_blk(internal, inter_info->blk);
	    
    return 0;
}

static uint32_t internal40_minsize(void) {
    return sizeof(internal40_t);
}

static void internal40_estimate(reiserfs_item_info_t *info, 
    reiserfs_item_coord_t *coord) 
{
    aal_assert("vpf-068", info != NULL, return);
    info->length = sizeof(internal40_t);
}

static int internal40_internal(void) {
    return 1;
}

static void internal40_print(internal40_t *internal, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-544", internal != NULL, return);
    aal_assert("umka-545", buff != NULL, return);
}

static void internal40_set_pointer(internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-605", internal != NULL, return);
    int40_set_blk(internal, blk);
}

static blk_t internal40_get_pointer(internal40_t *internal) {
    aal_assert("umka-606", internal != NULL, return 0);
    return int40_get_blk(internal);
}

static int internal40_has_pointer(internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-628", internal != NULL, return 0);
    return blk = int40_get_blk(internal);
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
	    
	    .create = (error_t (*)(void *, void *))internal40_create,
	    .estimate = (void (*)(void *, void *))internal40_estimate,
	    .minsize = (uint32_t (*)(void))internal40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))internal40_print,
	    .internal = (int (*)(void))internal40_internal,
	    
	    .lookup = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .unit_add = NULL,
	    .units_count = NULL,
	    .unit_remove = NULL
	},
	.specific = {
	    .internal = {
		.set_pointer = (void (*)(void *, blk_t))internal40_set_pointer,
		.get_pointer = (blk_t (*)(void *))internal40_get_pointer,
		.has_pointer = (int (*)(void *, blk_t))internal40_has_pointer
	    }
	}
    }
};

reiserfs_plugin_t *internal40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &internal40_plugin;
}

libreiserfs_plugins_register(internal40_entry);

