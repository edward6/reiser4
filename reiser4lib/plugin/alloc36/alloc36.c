/*
    alloc36.c -- Space allocator plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc36.h"

static reiserfs_plugins_factory_t *factory = NULL;

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

static error_t reiserfs_alloc36_sync(reiserfs_alloc36_t *alloc) {
    /* Synchronizing code must be here */
    return -1;
}

static void reiserfs_alloc36_close(reiserfs_alloc36_t *alloc, int sync) {
    if (sync)
	reiserfs_alloc36_sync(alloc);
    
    aal_free(alloc);
}

static reiserfs_plugin_t alloc36_plugin = {
    .alloc = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "Alloc36",
	    .desc = "Space allocator for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc36_open,
	.create = (reiserfs_alloc_opaque_t *(*)(aal_device_t *))reiserfs_alloc36_create,
	.close = (void (*)(reiserfs_alloc_opaque_t *, int))reiserfs_alloc36_close,
	.sync = (error_t (*)(reiserfs_alloc_opaque_t *))reiserfs_alloc36_sync
    }
};

reiserfs_plugin_t *reiserfs_alloc36_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &alloc36_plugin;
}

reiserfs_plugin_register(reiserfs_alloc36_entry);

