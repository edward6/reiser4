/*
    internal40.c -- reiser4 default internal item plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "internal40.h"

static reiser4_core_t *core = NULL;

#ifndef ENABLE_COMPACT

static errno_t internal40_init(reiser4_body_t *body, 
    reiser4_item_hint_t *hint)
{
    aal_assert("vpf-063", body != NULL, return -1); 
    aal_assert("vpf-064", hint != NULL, return -1);

    it40_set_ptr((internal40_t *)body, 
	((reiser4_internal_hint_t *)hint->hint)->ptr);
	    
    return 0;
}

static errno_t internal40_estimate(uint32_t pos, 
    reiser4_item_hint_t *hint) 
{
    aal_assert("vpf-068", hint != NULL, return -1);
    
    hint->len = sizeof(internal40_t);
    return 0;
}

extern errno_t internal40_check(reiser4_body_t *, uint16_t);

#endif

static errno_t internal40_print(reiser4_body_t *body, 
    char *buff, uint32_t n, uint16_t options) 
{
    aal_assert("umka-544", body != NULL, return -1);
    aal_assert("umka-545", buff != NULL, return -1);

    return -1;
}

#ifndef ENABLE_COMPACT

static errno_t internal40_set_ptr(reiser4_body_t *body, 
    blk_t blk)
{
    aal_assert("umka-605", body != NULL, return -1);
    it40_set_ptr((internal40_t *)body, blk);

    return 0;
}

#endif

static blk_t internal40_get_ptr(reiser4_body_t *body) {
    aal_assert("umka-606", body != NULL, return 0);
    return it40_get_ptr((internal40_t *)body);
}

static reiser4_plugin_t internal40_plugin = {
    .item_ops = {
	.h = {
    	    .handle = NULL,
	    .id = ITEM_INTERNAL40_ID,
	    .type = ITEM_PLUGIN_TYPE,
	    .label = "internal40",
	    .desc = "Internal item for reiserfs 4.0, ver. " VERSION,
	},
	.group = INTERNAL_ITEM_GROUP,
	.common = {
#ifndef ENABLE_COMPACT	    
	    .init	= internal40_init,
	    .estimate	= internal40_estimate,
	    .check	= internal40_check,
#else
	    .init	= NULL,
	    .estimate	= NULL,
	    .check	= NULL,
#endif
	    .lookup	= NULL,
	    .maxkey	= NULL,
	    .confirm	= NULL,
	    .valid	= NULL,
	    
	    .insert	= NULL,
	    .count	= NULL,
	    .remove	= NULL,
	    
	    .print	= internal40_print
	},
	.specific = {
	    .internal = {
		.get_ptr = internal40_get_ptr,
#ifndef ENABLE_COMPACT
		.set_ptr = internal40_set_ptr
#else
		.set_ptr = NULL
#endif
	    }
	}
    }
};

static reiser4_plugin_t *internal40_start(reiser4_core_t *c) {
    core = c;
    return &internal40_plugin;
}

plugin_register(internal40_start);
