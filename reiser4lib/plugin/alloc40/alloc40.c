/*
    alloc40.c -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc40.h"

static reiserfs_alloc40_t *reiserfs_alloc40_open(aal_device_t *device) {
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

static reiserfs_alloc40_t *reiserfs_alloc40_create(aal_device_t *device) {
    reiserfs_alloc40_t *alloc;
	
    if (!device)
	return NULL;
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
	
    /* Creating disk structures must be here. Probably bitmap? */
	
    alloc->device = device;
    return alloc;

error_free_format:
    aal_free(alloc);
error:
    return NULL;
}

static error_t reiserfs_alloc40_sync(reiserfs_alloc40_t *alloc) {
    /* Synchronizing code will be here */
    return -1;
}

static void reiserfs_alloc40_close(reiserfs_alloc40_t *alloc, int sync) {
    if (sync)
	reiserfs_alloc40_sync(alloc);
    
    aal_free(alloc);
}

static reiserfs_plugin_t alloc40_plugin = {
    .alloc = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "Alloc40",
	    .desc = "Space allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc40_open,
	.create = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc40_create,
	.close = (void (*)(reiserfs_alloc_opaque_t *, int))reiserfs_alloc40_close,
	.sync = (error_t (*)(reiserfs_alloc_opaque_t *))reiserfs_alloc40_sync
    }
};

reiserfs_plugin_register(alloc40_plugin);

