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

static reiserfs_core_t *core = NULL;

#ifndef ENABLE_COMPACT

/* Forms internal item in given memory area */
static errno_t internal40_create(reiserfs_internal40_t *internal, 
    reiserfs_item_hint_t *hint) 
{
    aal_assert("vpf-063", internal != NULL, return -1); 
    aal_assert("vpf-064", hint != NULL, return -1);

    int40_set_pointer(internal, ((reiserfs_internal_hint_t *)hint->hint)->pointer);
	    
    return 0;
}

static errno_t internal40_estimate(uint32_t pos, reiserfs_item_hint_t *hint) {
    aal_assert("vpf-068", hint != NULL, return -1);

    hint->len = sizeof(reiserfs_internal40_t);
    return 0;
}

#endif

static uint32_t internal40_minsize(void) {
    return sizeof(reiserfs_internal40_t);
}

static int internal40_internal(void) {
    return 1;
}

static int internal40_compound(void) {
    return 0;
}

static errno_t internal40_print(reiserfs_internal40_t *internal, 
    char *buff, uint32_t n) 
{
    aal_assert("umka-544", internal != NULL, return -1);
    aal_assert("umka-545", buff != NULL, return -1);

    return -1;
}

#ifndef ENABLE_COMPACT

static errno_t internal40_set_pointer(reiserfs_internal40_t *internal, 
    blk_t blk) 
{
    aal_assert("umka-605", internal != NULL, return -1);
    int40_set_pointer(internal, blk);

    return 0;
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
    .item_ops = {
	.h = {
    	    .handle = NULL,
	    .id = ITEM_INTERNAL40_ID,
	    .type = ITEM_PLUGIN_TYPE,
	    .label = "internal40",
	    .desc = "Internal item for reiserfs 4.0, ver. " VERSION,
	},
	.common = {
#ifndef ENABLE_COMPACT	    
	    .create = (errno_t (*)(const void *, reiserfs_item_hint_t *))
		internal40_create,
	    
	    .estimate = (errno_t (*)(uint32_t, reiserfs_item_hint_t *))
		internal40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .print = (errno_t (*)(const void *, char *, uint32_t))
		internal40_print,

	    .minsize = (uint32_t (*)(void))internal40_minsize,
	    .internal = internal40_internal,
	    .compound = internal40_compound,

	    .lookup = NULL,
	    .maxkey = NULL,
	    .confirm = NULL,
	    .check = NULL,
	    
	    .insert = NULL,
	    .count = NULL,
	    .remove = NULL
	},
	.specific = {
	    .internal = {
#ifndef ENABLE_COMPACT
		.set_pointer = (errno_t (*)(const void *, blk_t))internal40_set_pointer,
#else
		.set_pointer = NULL,
#endif
		.get_pointer = (blk_t (*)(const void *))internal40_get_pointer,
		.has_pointer = (int (*)(const void *, blk_t))internal40_has_pointer
	    }
	}
    }
};

static reiserfs_plugin_t *internal40_entry(reiserfs_core_t *c) {
    core = c;
    return &internal40_plugin;
}

libreiser4_factory_register(internal40_entry);

