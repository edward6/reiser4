/*
	layout40.c -- default disk-layout plugin for reiserfs 4.0
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#include <stdlib.h>

#include <dal/dal.h>
#include <reiserfs/plugin.h>

#include "layout40.h"

static reiserfs_layout40_t *reiserfs_layout40_init(dal_t *dal) {
	reiserfs_layout40_t *layout;
	
	if (!(layout = malloc(sizeof(*layout))))
		return NULL;
	layout->dal = dal;

	/* Initializing superblock and other */
	
	return layout;
}

static void reiserfs_layout40_done(reiserfs_layout40_t *layout) {
	/* Syncing superblock and other */
	free(layout);
}

reiserfs_plugin_t plugin_info = {
	.h = {
		.handle = NULL,
		.id = 0x1,
		.type = REISERFS_LAYOUT_PLUGIN,
		.label = "layout40",
		.desc = "Reiserfs 4.0 disk-layout plugin, ver. 0.1, "
			"Copyright (C) 1996 - 2002 Hans Reiser",
		.nlink = 0
	},
	.layout = {
		.init = reiserfs_layout40_init,
		.done = reiserfs_layout40_done
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

