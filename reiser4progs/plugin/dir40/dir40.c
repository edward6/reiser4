/*
    dir40.c -- reiser4 default directory object plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include "dir40.h"

#define DIR40_ID 0x0

static reiserfs_plugin_factory_t *factory = NULL;

static reiserfs_plugin_t dir40_plugin = {
    .dir = {
	.h = {
	    .handle = NULL,
	    .id = DIR40_ID,
	    .type = REISERFS_DIR_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.create = NULL,
	.open = NULL,
	.close = NULL
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &dir40_plugin;
}

libreiser4_plugins_register(dir40_entry);
