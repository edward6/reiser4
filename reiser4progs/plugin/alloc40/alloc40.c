/*
    alloc40.c -- Default block allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "alloc40.h"

static reiserfs_core_t *core = NULL;

/* 
    Performs actual opening of the alloc40 allocator. As alloc40 is the bitmap-based
    block allocator, this function calles reiserfs_bitmap_open function in order to 
    load it from device.
*/
static reiserfs_alloc40_t *alloc40_open(aal_device_t *device, 
    count_t len) 
{
    blk_t offset;
    reiserfs_alloc40_t *alloc;
    
    aal_assert("umka-364", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    /* Addres of first bitmap block */
    offset = (REISERFS_MASTER_OFFSET + (2 * aal_device_get_bs(device))) / 
	aal_device_get_bs(device);

    /* Opening bitmap */
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

#ifndef ENABLE_COMPACT

/* 
    Initializes new alloc40 instance, creates bitmap and return new instance to 
    caller (block allocator in libreiser4).
*/
static reiserfs_alloc40_t *alloc40_create(aal_device_t *device, 
    count_t len)
{
    blk_t offset;
    reiserfs_alloc40_t *alloc;

    aal_assert("umka-365", device != NULL, return NULL);
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    offset = (REISERFS_MASTER_OFFSET + (2 * aal_device_get_bs(device))) / 
	aal_device_get_bs(device);
    
    if (!(alloc->bitmap = reiserfs_bitmap_create(device, offset, len))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create bitmap.");
	goto error_free_alloc;
    }

    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

/* Saves alloc40 data (bitmap in fact) to device */
static errno_t alloc40_sync(reiserfs_alloc40_t *alloc) {

    aal_assert("umka-366", alloc != NULL, return -1);
    aal_assert("umka-367", alloc->bitmap != NULL, return -1);
    
    return reiserfs_bitmap_sync(alloc->bitmap);
}

#endif

/* Frees alloc40 instance */
static void alloc40_close(reiserfs_alloc40_t *alloc) {
    
    aal_assert("umka-368", alloc != NULL, return);
    aal_assert("umka-369", alloc->bitmap != NULL, return);

    reiserfs_bitmap_close(alloc->bitmap);
    aal_free(alloc);
}

#ifndef ENABLE_COMPACT

/* Marks specified block as used in its own bitmap */
static void alloc40_mark(reiserfs_alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-370", alloc != NULL, return);
    aal_assert("umka-371", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_use(alloc->bitmap, blk);
}

/* Marks "blk" as free */
static void alloc40_dealloc(reiserfs_alloc40_t *alloc, blk_t blk) {
    
    aal_assert("umka-372", alloc != NULL, return);
    aal_assert("umka-373", alloc->bitmap != NULL, return);
    
    reiserfs_bitmap_unuse(alloc->bitmap, blk);
}

/* Finds first free block in bitmap and returns it to caller */
static blk_t alloc40_alloc(reiserfs_alloc40_t *alloc) {
    blk_t blk;
    
    aal_assert("umka-374", alloc != NULL, return 0);
    aal_assert("umka-375", alloc->bitmap != NULL, return 0);
    
    /* 
	It is possible to implement here more smart allocation algorithm. For
	instance, it may look for contiguous areas.
    */
    if (!(blk = reiserfs_bitmap_find(alloc->bitmap, 0)))
	return 0;
    
    reiserfs_bitmap_use(alloc->bitmap, blk);
    return blk;
}

#endif

/* Returns free blcoks count */
count_t alloc40_free(reiserfs_alloc40_t *alloc) {

    aal_assert("umka-376", alloc != NULL, return 0);
    aal_assert("umka-377", alloc->bitmap != NULL, return 0);
    
    return reiserfs_bitmap_unused(alloc->bitmap);
}

/* Returns used blocks count */
count_t alloc40_used(reiserfs_alloc40_t *alloc) {
    
    aal_assert("umka-378", alloc != NULL, return 0);
    aal_assert("umka-379", alloc->bitmap != NULL, return 0);

    return reiserfs_bitmap_used(alloc->bitmap);
}

/* Checks whether specified block is used or not */
int alloc40_test(reiserfs_alloc40_t *alloc, blk_t blk) {
    aal_assert("umka-663", alloc != NULL, return 0);
    aal_assert("umka-664", alloc->bitmap != NULL, return 0);

    return reiserfs_bitmap_test(alloc->bitmap, blk);
}

/* Checks allocator on validness */
errno_t alloc40_check(reiserfs_alloc40_t *alloc, int flags) {
    aal_assert("umka-963", alloc != NULL, return -1);
    aal_assert("umka-964", alloc->bitmap != NULL, return -1);

    return reiserfs_bitmap_check(alloc->bitmap);
}

/* Filling the alloc40 structure by methods */
static reiserfs_plugin_t alloc40_plugin = {
    .alloc_ops = {
	.h = {
	    .handle = NULL,
	    .id = ALLOC_REISER40_ID,
	    .type = ALLOC_PLUGIN_TYPE,
	    .label = "alloc40",
	    .desc = "Space allocator for reiserfs 4.0, ver. " VERSION,
	},
	.open = (reiserfs_entity_t *(*)(aal_device_t *, count_t))alloc40_open,
	.close = (void (*)(reiserfs_entity_t *))alloc40_close,

#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(aal_device_t *, count_t))alloc40_create,
	.sync = (errno_t (*)(reiserfs_entity_t *))alloc40_sync,
	.mark = (void (*)(reiserfs_entity_t *, blk_t))alloc40_mark,
	.alloc = (blk_t (*)(reiserfs_entity_t *))alloc40_alloc,
	.dealloc = (void (*)(reiserfs_entity_t *, blk_t))alloc40_dealloc,
#else
	.create = NULL,
	.sync = NULL,
	.mark = NULL,
	.alloc = NULL,
	.dealloc = NULL,
#endif
	.test = (int (*)(reiserfs_entity_t *, blk_t))alloc40_test,
	.free = (count_t (*)(reiserfs_entity_t *))alloc40_free,
	.used = (count_t (*)(reiserfs_entity_t *))alloc40_used,
	.check = (errno_t (*)(reiserfs_entity_t *, int))alloc40_check,
    }
};

static reiserfs_plugin_t *alloc40_entry(reiserfs_core_t *c) {
    core = c;
    return &alloc40_plugin;
}

libreiser4_factory_register(alloc40_entry);

