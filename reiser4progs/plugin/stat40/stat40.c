/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "stat40.h"

static reiser4_core_t *core = NULL;

static errno_t stat40_confirm(reiser4_body_t *body) {
    aal_assert("umka-1008", body != NULL, return -1);
    return 0;
}

static aal_list_t *stat40_extentions_init(uint64_t extmask) {
    uint8_t i;
    aal_list_t *plugins = NULL;
    
    /* 
	Going through the all stat data extention bits in order to check if 
	particular stat data extention is in use.
    */
    for (i = 0; i < (sizeof(extmask) * 8); i++) {
	if (((uint64_t)1 << i) & extmask) {
	    reiser4_plugin_t *plugin;

	    if (!(plugin = core->factory_ops.plugin_ifind(SDEXT_PLUGIN_TYPE, i))) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
		    "Can't find stat data extention plugin by its id 0x%x.", i);
		continue;
	    }
	    
	    plugins = aal_list_append(plugins, plugin);
	}
    }
    
    return aal_list_first(plugins);
}

static void stat40_extentions_done(aal_list_t *list) {
    aal_assert("umka-888", list != NULL, return);
    aal_list_free(list);
}

#ifndef ENABLE_COMPACT

static errno_t stat40_init(reiser4_body_t *body, 
    reiser4_item_hint_t *hint)
{
    reiser4_stat40_t *stat;
    aal_list_t *extentions;
    
    reiser4_body_t *extention;
    reiser4_statdata_hint_t *stat_hint;
    
    aal_assert("vpf-076", body != NULL, return -1); 
    aal_assert("vpf-075", hint != NULL, return -1);
    
    stat = (reiser4_stat40_t *)body;
    stat_hint = (reiser4_statdata_hint_t *)hint->hint;
    
    st40_set_mode(stat, stat_hint->mode);
    st40_set_extmask(stat, stat_hint->extmask);
    st40_set_nlink(stat, stat_hint->nlink);
    st40_set_size(stat, stat_hint->size);
 
    if (stat_hint->extmask) {
	uint8_t i = 0;
	aal_list_t *walk = NULL;
	
	/* There may be stat data items without any extentions */
	if (!(extentions = stat40_extentions_init(stat_hint->extmask)))
	    return 0;

	if (aal_list_length(extentions) != stat_hint->extentions.count) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Invalid extmask or stat data hint detected.");
	    return -1;
	}
    
	extention = ((void *)stat) + sizeof(reiser4_stat40_t);
	aal_list_foreach_forward(walk, extentions) {
	    reiser4_plugin_t *plugin = (reiser4_plugin_t *)walk->item;
	
	    plugin_call(return -1, plugin->sdext_ops, init, 
		extention, stat_hint->extentions.hint[i++]);
	
	    /* 
		Getting pointer to the next extention. It is evaluating as previous 
		pointer plus its size.
	    */
	    extention += plugin_call(return -1, plugin->sdext_ops, length,);
	}
	
	stat40_extentions_done(extentions);
    }
    
    return 0;
}

static errno_t stat40_estimate(uint32_t pos, 
    reiser4_item_hint_t *hint) 
{
    aal_list_t *extentions = NULL;
    reiser4_statdata_hint_t *stat_hint;
    
    aal_assert("vpf-074", hint != NULL, return -1);

    hint->len = sizeof(reiser4_stat40_t);
    stat_hint = (reiser4_statdata_hint_t *)hint->hint;
    
    if (stat_hint->extmask) {
	aal_list_t *walk = NULL;
	
	/* There may be stat data items without extentions */
	if (!(extentions = stat40_extentions_init(stat_hint->extmask)))
	    return 0;

	/* Estimating the all stat data extentions */
	aal_list_foreach_forward(walk, extentions) {
	    reiser4_plugin_t *plugin = (reiser4_plugin_t *)walk->item;
	    
	    hint->len += plugin_call(return -1, plugin->sdext_ops, 
		length,);
	}
	stat40_extentions_done(extentions);
    }
    
    return 0;
}

#endif

static errno_t stat40_valid(reiser4_body_t *body) {
    aal_assert("umka-1007", body != NULL, return -1);
    return 0;
}

static errno_t stat40_print(reiser4_body_t *body, 
    char *buff, uint32_t n, uint16_t options)
{
    aal_assert("umka-546", body != NULL, return -1);
    aal_assert("umka-547", buff != NULL, return -1);

    return -1;
}

static uint16_t stat40_get_mode(reiser4_body_t *body) {
    aal_assert("umka-710", body != NULL, return 0);
    return st40_get_mode((reiser4_stat40_t *)body);
}

static errno_t stat40_set_mode(reiser4_body_t *body, 
    uint16_t mode)
{
    aal_assert("umka-711", body != NULL, return -1);
    st40_set_mode((reiser4_stat40_t *)body, mode);

    return 0;
}

static reiser4_plugin_t stat40_plugin = {
    .item_ops = {
	.h = {
	    .handle = NULL,
	    .id = ITEM_STATDATA40_ID,
	    .type = ITEM_PLUGIN_TYPE,
	    .label = "stat40",
	    .desc = "Stat data for reiserfs 4.0, ver. " VERSION,
	},
	.group = STATDATA_ITEM_GROUP,
	.common = {
		
#ifndef ENABLE_COMPACT
	    .init	= stat40_init,
	    .estimate	= stat40_estimate,
#else
	    .init	= NULL,
	    .estimate	= NULL,
#endif
	    .maxkey	= NULL,
	    .lookup	= NULL,
	    
	    .insert	= NULL,
	    .count	= NULL,
	    .remove	= NULL,
	    
	    .confirm	= stat40_confirm,
	    .valid	= stat40_valid,
	    .print	= stat40_print
	},
	
	.specific = {
	    .statdata = {
		.get_mode = stat40_get_mode,
		.set_mode = stat40_set_mode
	    }
	}
    }
};

static reiser4_plugin_t *stat40_start(reiser4_core_t *c) {
    core = c;
    return &stat40_plugin;
}

plugin_register(stat40_start);

