/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "internal40.h"

#define INTERNAL40_ID 0x3

static reiserfs_plugin_factory_t *factory = NULL;

#ifndef ENABLE_COMPACT

/* Forms internal item in given memory area */
static error_t internal40_create(reiserfs_internal40_t *internal, 
    reiserfs_item_hint_t *hint) 
{
    aal_assert("vpf-063", internal != NULL, return -1); 
    aal_assert("vpf-064", hint != NULL, return -1);

    int40_set_pointer(internal, ((reiserfs_internal_hint_t *)hint->hint)->pointer);
	    
    return 0;
}

static error_t internal40_estimate(reiserfs_item_hint_t *hint, 
    reiserfs_unit_coord_t *coord) 
{
    aal_assert("vpf-068", hint != NULL, return -1);

    hint->length = sizeof(reiserfs_internal40_t);
    return 0;
}

#endif

static uint32_t internal40_minsize(void) {
    return sizeof(reiserfs_internal40_t);
}

static int internal40_internal(void) {
    return 1;
}

static void internal40_print(reiserfs_internal40_t *internal, 
    char *buff, uint16_t n) 
{
    aal_assert("umka-544", internal != NULL, return);
    aal_assert("umka-545", buff != NULL, return);
}

#ifndef ENABLE_COMPACT

static void internal40_set_pointer(reiserfs_internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-605", internal != NULL, return);
    int40_set_pointer(internal, blk);
}

#endif

static blk_t internal40_get_pointer(reiserfs_internal40_t *internal) {
    aal_assert("umka-606", internal != NULL, return 0);
    return int40_get_pointer(internal);
}

static int internal40_has_pointer(reiserfs_internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-628", internal != NULL, return 0);
    return (blk == int40_get_pointer(internal));
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
	    .type = REISERFS_INTERNAL_ITEM,

#ifndef ENABLE_COMPACT	    
	    .create = (error_t (*)(void *, void *))internal40_create,
	    .estimate = (error_t (*)(void *, void *))internal40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .minsize = (uint32_t (*)(void))internal40_minsize,
	    .print = (void (*)(void *, char *, uint16_t))internal40_print,
	    .internal = (int (*)(void))internal40_internal,

	    .lookup = NULL,
	    .max_key = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    .unit_add = NULL,
	    .unit_count = NULL,
	    .unit_remove = NULL
	},
	.specific = {
	    .internal = {
#ifndef ENABLE_COMPACT
		.set_pointer = (void (*)(void *, blk_t))internal40_set_pointer,
#else
		.set_pointer = NULL,
#endif
		.get_pointer = (blk_t (*)(void *))internal40_get_pointer,
		.has_pointer = (int (*)(void *, blk_t))internal40_has_pointer
	    }
	}
    }
};

static reiserfs_plugin_t *internal40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &internal40_plugin;
}

libreiser4_factory_register(internal40_entry);

