/*
	alloc36.c -- Space allocator plugin for reiserfs 3.6.x
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc36.h"

static reiserfs_alloc36_t *reiserfs_alloc36_open(aal_device_t *device) {
    reiserfs_alloc36_t *alloc;
	
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

static reiserfs_alloc36_t *reiserfs_alloc36_create(aal_device_t *device) {
    reiserfs_alloc36_t *alloc;
	
    if (!device)
	return NULL;
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
	
    /* Creating of the disk structures must be here. */
	
    alloc->device = device;
    return alloc;

error_free_format:
    aal_free(alloc);
error:
    return NULL;
}

static void reiserfs_alloc36_close(reiserfs_alloc36_t *alloc, int sync) {
    if (sync) {
	/* Synchronizing code must be here */
    }
    aal_free(alloc);
}

reiserfs_plugin_t plugin_info = {
    .alloc = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "alloc36",
	    .desc = "Space allocator for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc36_open,
	.create = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc36_create,
	.close = (void (*)(reiserfs_alloc_opaque_t *, int))reiserfs_alloc36_close
    }
};

reiserfs_plugin_t *reiserfs_plugin_info() {
    return &plugin_info;
}

