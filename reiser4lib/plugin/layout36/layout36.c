/*
	layout36.c -- disk-layout plugin for reiserfs 3.6.x
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#include <dal/dal.h>
#include <reiserfs/plugin.h>

#include <stdlib.h>

#include "layout36.h"

static reiserfs_layout36_t *reiserfs_layout36_init(dal_t *dal) {
	reiserfs_layout36_t *layout;
	
	if (!(layout = malloc(sizeof(*layout))))
		return NULL;
	layout->dal = dal;

	/* Initializing superblock */
	
	return layout;
}

static void reiserfs_layout36_done(reiserfs_layout36_t *layout) {
	/* Syncing superblock and other */
	free(layout);
}

reiserfs_plugin_t plugin_info = {
	.h = {
		.handle = NULL,
		.id = 0x2,
		.type = REISERFS_LAYOUT_PLUGIN,
		.label = "layout36",
		.desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, Copyright (C) 1996-2002 Hans Reiser",
		.nlink = 0
	},
	.layout = {
		.init = (reiserfs_layout_opaque_t *(*)(dal_t *))reiserfs_layout36_init,
		.done = (void *(*)(void))reiserfs_layout36_done
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

