/*
	alloc40.c -- Space allocator plugin for reiserfs 4.0
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc40.h"

static reiserfs_alloc40_t *reiserfs_alloc40_init(aal_device_t *device) {
	reiserfs_alloc40_t *alloc;
	
	if (!device)
		return NULL;
	
	if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
		return NULL;
	
	/* Allocator initialization must be here */
	
	alloc->device = device;
	return alloc;

error_free_format:
	aal_free(alloc);
error:
	return NULL;
}

static void reiserfs_alloc40_done(reiserfs_alloc40_t *alloc, int sync) {
	if (sync) {
		/* Synchronizing code must be here */
	}
	aal_free(alloc);
}

reiserfs_plugin_t plugin_info = {
	.alloc = {
		.h = {
			.handle = NULL,
			.id = 0x1,
			.type = REISERFS_ALLOC_PLUGIN,
			.label = "alloc40",
			.desc = "Space allocator for reiserfs 4.0, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
		},
		.init = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc40_init,
		.done = (void (*)(reiserfs_alloc_opaque_t *, int))reiserfs_alloc40_done
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

