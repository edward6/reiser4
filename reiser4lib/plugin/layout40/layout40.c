/*
	layout40.c -- default disk-layout plugin for reiserfs 4.0
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <stdlib.h>

#include <aal/aal.h>
#include <reiserfs/plugin.h>

#include "layout40.h"

static reiserfs_layout40_t *reiserfs_layout40_init(aal_device_t *device) {
	reiserfs_layout40_t *layout;
	
	if (!(layout = malloc(sizeof(*layout))))
		return NULL;

	layout->device = device;

	/* Initializing superblock */
	
	return layout;
}

static void reiserfs_layout40_done(reiserfs_layout40_t *layout) {
	/* Syncing superblock and other */
	free(layout);
}

reiserfs_plugin_t plugin_info = {
	.layout = {
		.h = {
			.handle = NULL,
			.id = 0x1,
			.type = REISERFS_LAYOUT_PLUGIN,
			.label = "layout40",
			.desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
			.nlink = 0
		},
		.init = (reiserfs_layout_opaque_t *(*)(aal_device_t *))reiserfs_layout40_init,
		.done = (void (*)(reiserfs_layout_opaque_t *))reiserfs_layout40_done
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

