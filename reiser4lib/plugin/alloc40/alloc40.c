/*
    alloc40.c -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "alloc40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static reiserfs_alloc40_t *reiserfs_alloc40_open(aal_device_t *device, 
    blk_t format_specific_offset, count_t fs_blocks, uint16_t blocksize) 
{
    reiserfs_alloc40_t *alloc;
	
    if (!device)
	return NULL;
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(alloc->bitmap = reiserfs_bitmap_open(device, 
	format_specific_offset + 1, fs_blocks))) 
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
    blk_t format_specific_offset, count_t fs_blocks, uint16_t blocksize) 
{
    blk_t blk;
    reiserfs_alloc40_t *alloc;
	
    if (!device)
	return NULL;
	
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    if (!(alloc->bitmap = reiserfs_bitmap_create(format_specific_offset + 1, 
	fs_blocks, blocksize))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create bitmap.");
	goto error_free_alloc;
    }

    /* Marking the skiped area (0-16 4Kb blocks) as used */
    for (blk = 0; blk < (blk_t)(REISERFS_MASTER_OFFSET / blocksize); blk++)
	reiserfs_bitmap_use_block(alloc->bitmap, blk);
    
    /* Marking master super block as used */
    reiserfs_bitmap_use_block(alloc->bitmap, (REISERFS_MASTER_OFFSET / blocksize));
    
    /* Marking format-specific super block as used */
    reiserfs_bitmap_use_block(alloc->bitmap, format_specific_offset);

    /* Marking nodes as used */
/*    reiserfs_bitmap_use_block(alloc->bitmap, format_specific_offset);
    reiserfs_bitmap_use_block(alloc->bitmap, format_specific_offset);*/
    
    alloc->device = device;
    return alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static error_t reiserfs_alloc40_sync(reiserfs_alloc40_t *alloc) {
    if (!alloc || !alloc->bitmap)
	return -1;
    
    return reiserfs_bitmap_sync(alloc->bitmap, alloc->device);
}

static void reiserfs_alloc40_close(reiserfs_alloc40_t *alloc) {
    if (!alloc || !alloc->bitmap)
	return;
    
    reiserfs_bitmap_close(alloc->bitmap);
    aal_free(alloc);
}

static blk_t reiserfs_alloc40_allocate(reiserfs_alloc40_t *alloc) {
    blk_t blk;
    
    if (!alloc || !alloc->bitmap)
	return 0;
    
    if (!(blk = reiserfs_bitmap_find_free(alloc->bitmap, 0)))
	return 0;
    
    reiserfs_bitmap_use_block(alloc->bitmap, blk);
    return blk;
}

static void reiserfs_alloc40_deallocate(reiserfs_alloc40_t *alloc, blk_t blk) {
    if (!alloc || !alloc->bitmap)
	return;
    
    reiserfs_bitmap_unuse_block(alloc->bitmap, blk);
}

count_t reiserfs_alloc40_free(reiserfs_alloc40_t *alloc) {
    if (!alloc || !alloc->bitmap)
	return 0;
    
    return reiserfs_bitmap_unused(alloc->bitmap);
}

count_t reiserfs_alloc40_used(reiserfs_alloc40_t *alloc) {
    if (!alloc || !alloc->bitmap)
	return 0;
    
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
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t, count_t, uint16_t))reiserfs_alloc40_open,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t, count_t, uint16_t))reiserfs_alloc40_create,
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_alloc40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_sync,

	.allocate = (blk_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_allocate,
	.deallocate = (void (*)(reiserfs_opaque_t *, blk_t))reiserfs_alloc40_deallocate,
	
	.free = (count_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_free,
	.used = (count_t (*)(reiserfs_opaque_t *))reiserfs_alloc40_used
    }
};

reiserfs_plugin_t *reiserfs_alloc40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &alloc40_plugin;
}

reiserfs_plugin_register(reiserfs_alloc40_entry);

