/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "stat40.h"

static reiserfs_core_t *core = NULL;

static errno_t stat40_confirm(reiserfs_stat40_t *stat) {
    return 0;
}

static aal_list_t *stat40_ext_init(uint64_t extmask) {
    int i;
    aal_list_t *plugins = NULL;
    
    for (i = 0; i < SDEXT_LAST_ID; i++) {
	if ((1 << i) & extmask) {
	    reiserfs_plugin_t *plugin;

	    if (!(plugin = core->factory_ops.plugin_find(SDEXT_PLUGIN_TYPE, i))) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, 
		    "Can't find stat data extention plugin by its id %x.", i);
		continue;
	    }
	    plugins = aal_list_append(plugins, plugin);
	}
    }
    return aal_list_first(plugins);
}

static void stat40_ext_done(aal_list_t *list) {
    aal_assert("umka-888", list != NULL, return);
    aal_list_free(list);
}

#ifndef ENABLE_COMPACT

static errno_t stat40_create(reiserfs_stat40_t *stat, 
    reiserfs_item_hint_t *hint)
{
    void *ext;
    aal_list_t *ext_plugins = NULL;
    reiserfs_stat_hint_t *stat_hint;
    
    aal_assert("vpf-076", stat != NULL, return -1); 
    aal_assert("vpf-075", hint != NULL, return -1);
    
    stat_hint = (reiserfs_stat_hint_t *)hint->hint;
    
    st40_set_mode(stat, stat_hint->mode);
    st40_set_extmask(stat, stat_hint->extmask);
    st40_set_nlink(stat, stat_hint->nlink);
    st40_set_size(stat, stat_hint->size);
 
    if (stat_hint->extmask) {
	int i = 0;
	aal_list_t *walk = NULL;
	
	if (!(ext_plugins = stat40_ext_init(stat_hint->extmask))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't initialize stat data extention plugins.");
	    return -1;
	}

	if (aal_list_length(ext_plugins) != stat_hint->ext.count) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Invalid extmask or stat data hint detected.");
	    return -1;
	}
    
	ext = ((void *)stat) + sizeof(reiserfs_stat40_t);
	aal_list_foreach_forward(walk, ext_plugins) {
	    reiserfs_plugin_t *plugin = (reiserfs_plugin_t *)walk->item;
	
	    libreiser4_plugin_call(return -1, plugin->sdext_ops, create, 
		ext, stat_hint->ext.hint[i++]);
	
	    /* 
		Getting pointer to the next extention. It is evaluating as previous 
		pointer plus its size.
	    */
	    ext += libreiser4_plugin_call(return -1, plugin->sdext_ops, length,);

	    /* FIXME-UMKA: Here also should be support for more then 16 extentions */
	}
	stat40_ext_done(ext_plugins);
    }
    
    return 0;
}

static errno_t stat40_estimate(uint32_t pos, reiserfs_item_hint_t *hint) {
    reiserfs_stat_hint_t *stat_hint;
    aal_list_t *ext_plugins = NULL;
    
    aal_assert("vpf-074", hint != NULL, return -1);

    hint->len = sizeof(reiserfs_stat40_t);
    stat_hint = (reiserfs_stat_hint_t *)hint->hint;
    
    if (stat_hint->extmask) {
	aal_list_t *walk = NULL;
	
	if (!(ext_plugins = stat40_ext_init(stat_hint->extmask))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't initialize stat data extention plugins.");
	    return -1;
	}

	/* Estimating the all stat data extentions */
	aal_list_foreach_forward(walk, ext_plugins) {
	    reiserfs_plugin_t *plugin = (reiserfs_plugin_t *)walk->item;
	    hint->len += libreiser4_plugin_call(return -1, plugin->sdext_ops, 
		length,);
	}
	stat40_ext_done(ext_plugins);
    }
    
    return 0;
}

#endif

static errno_t stat40_check(reiserfs_stat40_t *stat, int flags) {
    return 0;
}

static errno_t stat40_print(reiserfs_stat40_t *stat, 
    char *buff, uint32_t n)
{
    aal_assert("umka-546", stat != NULL, return -1);
    aal_assert("umka-547", buff != NULL, return -1);

    return -1;
}

static uint32_t stat40_minsize(void) {
    return sizeof(reiserfs_stat40_t);
}

static int stat40_internal(void) {
    return 0;
}

static uint16_t stat40_get_mode(reiserfs_stat40_t *stat) {
    aal_assert("umka-710", stat != NULL, return 0);
    return st40_get_mode(stat);
}

static void stat40_set_mode(reiserfs_stat40_t *stat, uint16_t mode) {
    aal_assert("umka-711", stat != NULL, return);
    st40_set_mode(stat, mode);
}

static reiserfs_plugin_t stat40_plugin = {
    .item_ops = {
	.h = {
	    .handle = NULL,
	    .id = ITEM_STATDATA40_ID,
	    .type = ITEM_PLUGIN_TYPE,
	    .label = "stat40",
	    .desc = "Stat data for reiserfs 4.0, ver. " VERSION,
	},
	.common = {
		
#ifndef ENABLE_COMPACT
	    .create = (errno_t (*)(const void *, reiserfs_item_hint_t *))stat40_create,
	    .estimate = (errno_t (*)(uint32_t, reiserfs_item_hint_t *))stat40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .confirm = (errno_t (*)(const void *))stat40_confirm,
	    .check = (errno_t (*)(const void *, int))stat40_check,
	    .print = (errno_t (*)(const void *, char *, uint32_t))stat40_print,
	    .minsize = (uint32_t (*)(void))stat40_minsize,
	    .internal = stat40_internal,

	    .maxkey = NULL,
	    .lookup = NULL,
	    
	    .insert = NULL,
	    .count = NULL,
	    .remove = NULL
	},
	.specific = {
	    .statdata = {
		.get_mode = (uint16_t (*)(const void *))stat40_get_mode,
		.set_mode = (void (*)(const void *, uint16_t))stat40_get_mode
	    }
	}
    }
};

static reiserfs_plugin_t *stat40_entry(reiserfs_core_t *c) {
    core = c;
    return &stat40_plugin;
}

libreiser4_factory_register(stat40_entry);

