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

static reiserfs_plugins_factory_t *factory = NULL;

/* Forms internal item in given memory area */
static error_t reiserfs_internal40_create(reiserfs_internal40_t *internal, 
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

static uint32_t reiserfs_internal40_minsize(void) {
    return sizeof(reiserfs_internal40_t);
}

static void reiserfs_internal40_estimate(reiserfs_item_info_t *info, 
    reiserfs_item_coord_t *coord) 
{
    aal_assert("vpf-068", info != NULL, return);
    info->length = sizeof(reiserfs_internal40_t);
}

static int reiserfs_internal40_internal(void) {
    return 1;
}

static void reiserfs_internal40_print(reiserfs_internal40_t *internal, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-544", internal != NULL, return);
    aal_assert("umka-545", buff != NULL, return);
}

static void reiserfs_internal40_set_pointer(reiserfs_internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-605", internal != NULL, return);
    int40_set_blk(internal, blk);
}

static blk_t reiserfs_internal40_get_pointer(reiserfs_internal40_t *internal) {
    aal_assert("umka-606", internal != NULL, return 0);
    return int40_get_blk(internal);
}

static int reiserfs_internal40_has_pointer(reiserfs_internal40_t *internal, 
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
	    
	    .create = (error_t (*)(void *, void *))reiserfs_internal40_create,
	    .estimate = (void (*)(void *, void *))reiserfs_internal40_estimate,
	    .minsize = (uint32_t (*)(void))reiserfs_internal40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))reiserfs_internal40_print,
	    .internal = (int (*)(void))reiserfs_internal40_internal,
	    
	    .lookup = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL
	},
	.specific = {
	    .internal = {
		.set_pointer = (void (*)(void *, blk_t))reiserfs_internal40_set_pointer,
		.get_pointer = (blk_t (*)(void *))reiserfs_internal40_get_pointer,
		.has_pointer = (int (*)(void *, blk_t))reiserfs_internal40_has_pointer
	    }
	}
    }
};

reiserfs_plugin_t *reiserfs_internal40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &internal40_plugin;
}

libreiserfs_plugins_register(reiserfs_internal40_entry);

