/*
    stat40.c -- reiser4 default stat data plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "stat40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static errno_t stat40_confirm(reiserfs_stat40_base_t *stat) {
    return 0;
}

#ifndef ENABLE_COMPACT

static errno_t stat40_create(reiserfs_stat40_base_t *stat, 
    reiserfs_item_hint_t *hint)
{
    reiserfs_stat_hint_t *stat_hint;
    
    aal_assert("vpf-076", stat != NULL, return -1); 
    aal_assert("vpf-075", hint != NULL, return -1);
    
    stat_hint = (reiserfs_stat_hint_t *)hint->hint;
    
    sd40_set_mode(stat, stat_hint->mode);
    sd40_set_extmask(stat, stat_hint->extmask);
    sd40_set_nlink(stat, stat_hint->nlink);
    sd40_set_size(stat, stat_hint->size);
  
    /* And its extentions should be created here also */
    
    return 0;
}

static errno_t stat40_estimate(uint16_t pos, reiserfs_item_hint_t *hint) {
    aal_assert("vpf-074", hint != NULL, return -1);

    /* Should calculate extentions size also */
    
    hint->len = sizeof(reiserfs_stat40_base_t);
    return 0;
}

#endif

static errno_t stat40_check(reiserfs_stat40_base_t *stat) {
    return 0;
}

static void stat40_print(reiserfs_stat40_base_t *stat, char *buff, uint16_t n) {
    aal_assert("umka-546", stat != NULL, return);
    aal_assert("umka-547", buff != NULL, return);
}

static uint32_t stat40_minsize(void) {
    return sizeof(reiserfs_stat40_base_t);
}

static uint16_t stat40_get_mode(reiserfs_stat40_base_t *stat) {
    aal_assert("umka-710", stat != NULL, return 0);
    return sd40_get_mode(stat);
}

static void stat40_set_mode(reiserfs_stat40_base_t *stat, uint16_t mode) {
    aal_assert("umka-711", stat != NULL, return);
    sd40_set_mode(stat, mode);
}

static reiserfs_plugin_t stat40_plugin = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = REISERFS_STATDATA_ITEM,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "stat40",
	    .desc = "Stat data for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
#ifndef ENABLE_COMPACT
	    .create = (errno_t (*)(void *, void *))stat40_create,
	    .estimate = (errno_t (*)(uint16_t, void *))stat40_estimate,
#else
	    .create = NULL,
	    .estimate = NULL,
#endif
	    .confirm = (errno_t (*)(void *))stat40_confirm,
	    .check = (errno_t (*)(void *))stat40_check,
	    .print = (void (*)(void *, char *, uint16_t))stat40_print,
	    .minsize = (uint16_t (*)(void))stat40_minsize,

	    .maxkey = NULL,
	    .lookup = NULL,
	    
	    .insert = NULL,
	    .count = NULL,
	    .remove = NULL
	},
	.specific = {
	    .stat = {
		.get_mode = (uint16_t (*)(void *))stat40_get_mode,
		.set_mode = (void (*)(void *, uint16_t))stat40_get_mode
	    }
	}
    }
};

static reiserfs_plugin_t *stat40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &stat40_plugin;
}

libreiser4_factory_register(stat40_entry);

