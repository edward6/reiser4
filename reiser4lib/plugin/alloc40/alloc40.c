/*
    alloc40.c -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static reiserfs_alloc40_t *reiserfs_alloc40_open(aal_device_t *device, count_t len) {
    reiserfs_alloc40_t *alloc;
    
    aal_assert("umka-364", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(alloc->bitmap = reiserfs_bitmap_open(device, 
	(REISERFS_ALLOC40_OFFSET / aal_device_get_blocksize(device)), len))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't open bitmap.");
	goto error_free_alloc;
    }
  
    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static reiserfs_alloc40_t *reiserfs_alloc40_create(aal_device_t *device, 
    count_t len)
{
    reiserfs_alloc40_t *alloc;

    aal_assert("umka-365", device != NULL, return NULL);
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    if (!(alloc->bitmap = reiserfs_bitmap_create(device, 
	(REISERFS_ALLOC40_OFFSET / aal_device_get_blocksize(device)), len))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create bitmap.");
	goto error_free_alloc;
    }

    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static error_t reiserfs_alloc40_sync(reiserfs_alloc40_t *alloc) {

    aal_assert("umka-366", alloc != NULL, return -1);
    aal_assert("umka-367", alloc->bitmap != NULL, return -1);
    
    return reiserfs_bitmap_sync(alloc->bitmap);
}

static void reiserfs_alloc40_close(reiserfs_alloc40_t *alloc) {
    
    aal_assert("umka-368", alloc != NULL, return);
    aal_assert("umka-369", alloc->bitmap != NULL, return);

    reiserfs_bitmap_close(alloc->bitmap);
    aal_free(alloc);
}

static void reiserfs_alloc40_use(reiserfs_alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-370", alloc != NULL, return);
    aal_assert("umka-371", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_use_block(alloc->bitmap, blk);
}

static void reiserfs_alloc40_unuse(reiserfs_alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-372", alloc != NULL, return);
    aal_assert("umka-373", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_unuse_block(alloc->bitmap, blk);
}

static blk_t reiserfs_alloc40_find(reiserfs_alloc40_t *alloc, blk_t start) {
    
    aal_assert("umka-374", alloc != NULL, return 0);
    aal_assert("umka-375", alloc->bitmap != NULL, return 0);
    
    return reiserfs_bitmap_find_free(alloc->bitmap, start);
}

count_t reiserfs_alloc40_free(reiserfs_alloc40_t *alloc) {

    aal_assert("umka-376", alloc != NULL, return 0);
    aal_assert("umka-377", alloc->bitmap != NULL, return 0);
    
    return reiserfs_bitmap_unused(alloc->bitmap);
}

count_t reiserfs_alloc40_used(reiserfs_alloc40_t *alloc) {
    
    aal_assert("umka-378", alloc != NULL, return 0);
    aal_assert("umka-379", alloc->bitmap != NULL, return 0);

    return reiserfs_bitmap_used(alloc->bitmap);
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
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, count_t))reiserfs_alloc40_open,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t))reiserfs_alloc40_create,
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_alloc40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_sync,

	.use = (void (*)(reiserfs_opaque_t *, blk_t))reiserfs_alloc40_use,
	.unuse = (void (*)(reiserfs_opaque_t *, blk_t))reiserfs_alloc40_unuse,
	.find = (blk_t (*)(reiserfs_opaque_t *, blk_t))reiserfs_alloc40_find,
	
	.free = (count_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_free,
	.used = (count_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_used
    }
};

reiserfs_plugin_t *reiserfs_alloc40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &alloc40_plugin;
}

reiserfs_plugin_register(reiserfs_alloc40_entry);

