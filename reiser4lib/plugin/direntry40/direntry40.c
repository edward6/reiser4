/*
    direntry40.c -- reiserfs default direntry plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/reiserfs.h>

#include "direntry40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static reiserfs_plugin_t direntry40_plugin/* = {
    .item = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_ITEM_PLUGIN,
	    .label = "direntry40",
	    .desc = "Directory plugin for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.common = {
	    .item_type = DIRENTRY_DIRENRTY_TYPE,
	}
    }
}*/;

reiserfs_plugin_t *reiserfs_direntry40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &direntry40_plugin;
}

reiserfs_plugin_register(reiserfs_direntry40_entry);

