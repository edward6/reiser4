/*
    alloc36.c -- Space allocator plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "alloc36.h"

static reiserfs_plugin_factory_t *factory = NULL;

static reiserfs_alloc36_t *alloc36_open(aal_device_t *device, count_t len) {
    reiserfs_alloc36_t *alloc;

    aal_assert("umka-413", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
	
    /* Allocator initialization must be here */
	
    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiserfs_alloc36_t *alloc36_create(aal_device_t *device, count_t len) {
    reiserfs_alloc36_t *alloc;

    aal_assert("umka-414", device != NULL, return NULL);
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
	
    /* Creating of the disk structures must be here. */
	
    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static errno_t alloc36_sync(reiserfs_alloc36_t *alloc) {
    aal_assert("umka-415", alloc != NULL, return -1);
    
    return -1;
}

#endif

static void alloc36_close(reiserfs_alloc36_t *alloc) {
    aal_assert("umka-416", alloc != NULL, return);
    aal_free(alloc);
}

static reiserfs_plugin_t alloc36_plugin = {
    .alloc_ops = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "alloc36",
	    .desc = "Space allocator for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},

#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(aal_device_t *, count_t))alloc36_create,
	.sync = (errno_t (*)(reiserfs_entity_t *))alloc36_sync,
#else
	.create = NULL,
	.sync = NULL,
#endif
	.close = (void (*)(reiserfs_entity_t *))alloc36_close,
	.open = (reiserfs_entity_t *(*)(aal_device_t *, count_t))alloc36_open,

	.mark = NULL,
	.test = NULL,
	
	.alloc = NULL,
	.dealloc = NULL,
	
	.free = NULL,
	.used = NULL
    }
};

static reiserfs_plugin_t *alloc36_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &alloc36_plugin;
}

libreiser4_factory_register(alloc36_entry);

