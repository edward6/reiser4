/*
    alloc40.c -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc40.h"

static reiserfs_plugins_factory_t *factory = NULL;

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

static error_t reiserfs_alloc40_allocate(reiserfs_alloc40_t *alloc, 
    reiserfs_segment_t *request, reiserfs_segment_t *response)
{
    return -1;
}

static error_t reiserfs_alloc40_deallocate(reiserfs_alloc40_t *alloc, 
    reiserfs_segment_t *request, reiserfs_segment_t *response)
{
    return -1;
}

static reiserfs_plugin_t alloc40_plugin = {
    .alloc = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "alloc40",
	    .desc = "Space allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))reiserfs_alloc40_open,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *))reiserfs_alloc40_create,
	.close = (void (*)(reiserfs_opaque_t *, int))reiserfs_alloc40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_sync,

	.allocate = (error_t (*)(reiserfs_opaque_t *, reiserfs_segment_t *, reiserfs_segment_t *))
	    reiserfs_alloc40_allocate,
	
	.deallocate = (error_t (*)(reiserfs_opaque_t *, reiserfs_segment_t *, reiserfs_segment_t *))
	    reiserfs_alloc40_deallocate
    }
};

reiserfs_plugin_t *reiserfs_alloc40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &alloc40_plugin;
}

reiserfs_plugin_register(reiserfs_alloc40_entry);

