/*
    alloc40.c -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "alloc40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static alloc40_t *alloc40_open(aal_device_t *device, count_t len) {
    blk_t offset;
    alloc40_t *alloc;
    
    aal_assert("umka-364", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    offset = (REISERFS_MASTER_OFFSET + (2 * aal_device_get_bs(device))) / 
	aal_device_get_bs(device);
    
    if (!(alloc->bitmap = reiserfs_bitmap_open(device, offset, len))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open bitmap.");
	goto error_free_alloc;
    }
  
    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static alloc40_t *alloc40_create(aal_device_t *device, 
    count_t len)
{
    blk_t offset;
    alloc40_t *alloc;

    aal_assert("umka-365", device != NULL, return NULL);
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    offset = (REISERFS_MASTER_OFFSET + (2 * aal_device_get_bs(device))) / 
	aal_device_get_bs(device);
    
    if (!(alloc->bitmap = reiserfs_bitmap_create(device, offset, len))) 
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

static error_t alloc40_sync(alloc40_t *alloc) {

    aal_assert("umka-366", alloc != NULL, return -1);
    aal_assert("umka-367", alloc->bitmap != NULL, return -1);
    
    return reiserfs_bitmap_sync(alloc->bitmap);
}

static void alloc40_close(alloc40_t *alloc) {
    
    aal_assert("umka-368", alloc != NULL, return);
    aal_assert("umka-369", alloc->bitmap != NULL, return);

    reiserfs_bitmap_close(alloc->bitmap);
    aal_free(alloc);
}

static void alloc40_mark(alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-370", alloc != NULL, return);
    aal_assert("umka-371", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_use_block(alloc->bitmap, blk);
}

static void alloc40_dealloc(alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-372", alloc != NULL, return);
    aal_assert("umka-373", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_unuse_block(alloc->bitmap, blk);
}

static blk_t alloc40_alloc(alloc40_t *alloc) {
    blk_t blk;
    
    aal_assert("umka-374", alloc != NULL, return 0);
    aal_assert("umka-375", alloc->bitmap != NULL, return 0);
    
    /* 
	It is possible to implement here more smart 
	allocation algorithm 
    */
    if (!(blk = reiserfs_bitmap_find_free(alloc->bitmap, 0)))
	return 0;
    
    reiserfs_bitmap_use_block(alloc->bitmap, blk);
    return blk;
}

count_t alloc40_free(alloc40_t *alloc) {

    aal_assert("umka-376", alloc != NULL, return 0);
    aal_assert("umka-377", alloc->bitmap != NULL, return 0);
    
    return reiserfs_bitmap_unused(alloc->bitmap);
}

count_t alloc40_used(alloc40_t *alloc) {
    
    aal_assert("umka-378", alloc != NULL, return 0);
    aal_assert("umka-379", alloc->bitmap != NULL, return 0);

    return reiserfs_bitmap_used(alloc->bitmap);
}

static reiserfs_plugin_t alloc40_plugin = {
    .alloc = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_ALLOC_PLUGIN,
	    .label = "alloc40",
	    .desc = "Space allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, count_t))alloc40_open,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t))alloc40_create,
	.close = (void (*)(reiserfs_opaque_t *))alloc40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))alloc40_sync,

	.mark = (void (*)(reiserfs_opaque_t *, blk_t))alloc40_mark,
	.alloc = (blk_t (*)(reiserfs_opaque_t *))alloc40_alloc,
	.dealloc = (void (*)(reiserfs_opaque_t *, blk_t))alloc40_dealloc,
	
	.free = (count_t (*)(reiserfs_opaque_t *))alloc40_free,
	.used = (count_t (*)(reiserfs_opaque_t *))alloc40_used
    }
};

static reiserfs_plugin_t *alloc40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &alloc40_plugin;
}

libreiser4_plugins_register(alloc40_entry);

