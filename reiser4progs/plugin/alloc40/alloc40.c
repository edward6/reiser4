/*
    alloc40.c -- Default block allocator plugin for reiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "alloc40.h"

#define CRC_SIZE (4)

extern reiser4_plugin_t alloc40_plugin;

static reiser4_core_t *core = NULL;

static errno_t callback_fetch_bitmap(reiser4_entity_t *format, 
    blk_t blk, void *data)
{
    uint32_t size;
    uint32_t chunk;
    aal_block_t *block;
    aal_device_t *device;
    char *current, *start;
    alloc40_t *alloc = (alloc40_t *)data;
    
    aal_assert("umka-1053", alloc != NULL, return -1);
    
    device = plugin_call(return -1, format->plugin->format_ops, 
	device, format);
    
    if (!device) {
	aal_exception_error("Can't get device from format instance "
	    "durring bitmap loading.");
	return -1;
    }
    
    if (!(block = aal_block_open(device, blk))) {
	aal_exception_error("Can't read bitmap block %llu. %s.", 
	    blk, device->error);
	return -1;
    }

    start = alloc->bitmap->map;
    
    size = aal_block_size(block) - CRC_SIZE;
    current = start + (size * (blk / size / 8));
    
    chunk = (start + alloc->bitmap->size - current < (int)aal_block_size(block) ? 
	(int)(start + alloc->bitmap->size - current) : (int)aal_block_size(block));
    
    chunk -= CRC_SIZE;
    
    aal_memcpy(current, block->data + CRC_SIZE, chunk);

    aal_memcpy((void *)(alloc->crc + (blk / size / 8) * CRC_SIZE), 
	block->data, CRC_SIZE);
    
    aal_block_free(block);
    return 0;
    
error_free_block:
    aal_block_free(block);
    return -1;
}

static reiser4_entity_t *alloc40_open(reiser4_entity_t *format,
    count_t len)
{
    alloc40_t *alloc;
    aal_device_t *device;
    reiser4_layout_func_t layout;
    
    uint32_t crcsize;
    
    aal_assert("umka-364", format != NULL, return NULL);

    device = plugin_call(return NULL, format->plugin->format_ops,
	device, format);
    
    if (!device) {
	aal_exception_error("Can't get device from format instance "
	    "durring bitmap loading.");
	return NULL;
    }
    
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(alloc->bitmap = reiser4_bitmap_create(len))) {
	aal_exception_error("Can't create bitmap.");
	goto error_free_alloc;
    }
  
    crcsize = (alloc->bitmap->size / 
	(aal_device_get_bs(device) - CRC_SIZE)) * CRC_SIZE;

    if (!(alloc->crc = aal_calloc(crcsize, 0)))
	goto error_free_bitmap;
    
    alloc->format = format;
    alloc->plugin = &alloc40_plugin;

    if (!(layout = format->plugin->format_ops.alloc_layout)) {
	aal_exception_error("Method \"alloc_layout\" doesn't implemented "
	    "in format plugin.");
	goto error_free_bitmap;
    }
    
    if (layout(format, callback_fetch_bitmap, alloc)) {
	aal_exception_error("Can't load ondisk bitmap.");
	goto error_free_bitmap;
    }

    reiser4_bitmap_calc_used(alloc->bitmap);
    
    return (reiser4_entity_t *)alloc;

error_free_bitmap:
    reiser4_bitmap_close(alloc->bitmap);
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
static reiser4_entity_t *alloc40_create(reiser4_entity_t *format,
    count_t len) 
{
    alloc40_t *alloc;
    aal_device_t *device;
    
    uint32_t crcsize;

    aal_assert("umka-365", format != NULL, return NULL);
	
    device = plugin_call(return NULL, format->plugin->format_ops, 
	device, format);

    if (!device) {
	aal_exception_error("Can't get device from format instance "
	    "durring bitmap creating.");
	return NULL;
    }
    
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    if (!(alloc->bitmap = reiser4_bitmap_create(len))) {
	aal_exception_error("Can't create bitmap.");
	goto error_free_alloc;
    }
  
    crcsize = (alloc->bitmap->size / 
	(aal_device_get_bs(device) - CRC_SIZE)) * CRC_SIZE;

    if (!(alloc->crc = aal_calloc(crcsize, 0)))
	goto error_free_bitmap;
    
    alloc->format = format;
    alloc->plugin = &alloc40_plugin;
    
    return (reiser4_entity_t *)alloc;

error_free_bitmap:
    reiser4_bitmap_close(alloc->bitmap);
error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static errno_t callback_flush_bitmap(reiser4_entity_t *format, 
    blk_t blk, void *data)
{
    uint32_t size;
    uint32_t adler;
    uint32_t chunk;
    aal_block_t *block;
    aal_device_t *device;
    char *current, *start; 
   
    alloc40_t *alloc = (alloc40_t *)data;
    
    aal_assert("umka-1055", alloc != NULL, return -1);
    
    device = plugin_call(return -1, format->plugin->format_ops, 
	device, format);
    
    if (!device) {
	aal_exception_error("Can't get device from format instance "
	    "durring bitmap flushing.");
	return -1;
    }
    
    if (!(block = aal_block_create(device, blk, 0xff))) {
	aal_exception_error("Can't read bitmap block %llu. %s.", 
	    blk, device->error);
	return -1;
    }

    start = alloc->bitmap->map;
    
    size = aal_block_size(block) - CRC_SIZE;
    current = start + (size * (blk / size / 8));
    
    chunk = (start + alloc->bitmap->size - current < (int)size ? 
	(int)(start + alloc->bitmap->size - current) : (int)size);

    /* Updating block which is going to be saved */
    aal_memcpy(block->data + CRC_SIZE, current, chunk);
    
    adler = aal_adler32(current, chunk);
    aal_memcpy(block->data, &adler, sizeof(adler));
    
    if (aal_block_sync(block)) {
	aal_exception_error("Can't write bitmap block %llu. %s.", 
	    blk, device->error);
	
	goto error_free_block;
    }

    aal_block_free(block);
    
    return 0;
    
error_free_block:
    aal_block_free(block);
    return -1;
}

/* Saves alloc40 data (bitmap in fact) to device */
static errno_t alloc40_sync(reiser4_entity_t *entity) {
    reiser4_layout_func_t layout;
    
    alloc40_t *alloc = (alloc40_t *)entity;

    aal_assert("umka-366", alloc != NULL, return -1);
    aal_assert("umka-367", alloc->bitmap != NULL, return -1);
    
    if (!(layout = alloc->format->plugin->format_ops.alloc_layout)) {
	aal_exception_error("Method \"alloc_layout\" doesn't implemented "
	    "in format plugin.");
	return -1;
    }
    
    if (layout(alloc->format, callback_flush_bitmap, alloc)) {
	aal_exception_error("Can't synchronize bitmap.");
	return -1;
    }
    
    return 0;
}

#endif

/* Frees alloc40 instance */
static void alloc40_close(reiser4_entity_t *entity) {
    
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-368", alloc != NULL, return);
    aal_assert("umka-369", alloc->bitmap != NULL, return);

    reiser4_bitmap_close(alloc->bitmap);

    aal_free(alloc->crc);
    aal_free(alloc);
}

#ifndef ENABLE_COMPACT

/* Marks specified block as used in its own bitmap */
static void alloc40_mark(reiser4_entity_t *entity, blk_t blk) {
    
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-370", alloc != NULL, return);
    aal_assert("umka-371", alloc->bitmap != NULL, return);
    
    reiser4_bitmap_use(alloc->bitmap, blk);
}

/* Marks "blk" as free */
static void alloc40_release(reiser4_entity_t *entity, blk_t blk) {
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-372", alloc != NULL, return);
    aal_assert("umka-373", alloc->bitmap != NULL, return);
    
    reiser4_bitmap_unuse(alloc->bitmap, blk);
}

/* Finds first free block in bitmap and returns it to caller */
static blk_t alloc40_allocate(reiser4_entity_t *entity) {
    blk_t blk;
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-374", alloc != NULL, return 0);
    aal_assert("umka-375", alloc->bitmap != NULL, return 0);
    
    /* 
	It is possible to implement here more smart allocation algorithm. For
	instance, it may look for contiguous areas.
    */
    if (!(blk = reiser4_bitmap_find(alloc->bitmap, 0)))
	return 0;
    
    reiser4_bitmap_use(alloc->bitmap, blk);
    return blk;
}

#endif

/* Returns free blcoks count */
count_t alloc40_free(reiser4_entity_t *entity) {
    alloc40_t *alloc = (alloc40_t *)entity;

    aal_assert("umka-376", alloc != NULL, return 0);
    aal_assert("umka-377", alloc->bitmap != NULL, return 0);
    
    return reiser4_bitmap_unused(alloc->bitmap);
}

/* Returns used blocks count */
count_t alloc40_used(reiser4_entity_t *entity) {
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-378", alloc != NULL, return 0);
    aal_assert("umka-379", alloc->bitmap != NULL, return 0);

    return reiser4_bitmap_used(alloc->bitmap);
}

/* Checks whether specified block is used or not */
int alloc40_test(reiser4_entity_t *entity, blk_t blk) {
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-663", alloc != NULL, return 0);
    aal_assert("umka-664", alloc->bitmap != NULL, return 0);

    return reiser4_bitmap_test(alloc->bitmap, blk);
}

static errno_t callback_check_bitmap(reiser4_entity_t *format, 
    blk_t blk, void *data)
{
    char *current;
    aal_device_t *device;
    
    uint32_t size, i, n;
    uint32_t ladler, cadler;
    
    alloc40_t *alloc = (alloc40_t *)data;
    
    device = plugin_call(return -1, format->plugin->format_ops, 
	device, format);
    
    if (!device) {
	aal_exception_error("Can't get device from format instance "
	    "durring bitmap checking.");
	return -1;
    }
        
    size = aal_device_get_bs(device) - CRC_SIZE;
    i = (blk / size / 8);
    
    /* Getting pointer to next bitmap portion */
    current = alloc->bitmap->map + (i * size);
	    
    /* Getting the checksum from loaded crc map */
    ladler = *((uint32_t *)(alloc->crc + (i * CRC_SIZE)));
    
    n = alloc->bitmap->map + alloc->bitmap->size - current < (int)size ? 
	(int)((alloc->bitmap->map + alloc->bitmap->size) - current) : (int)size;

    /* Calculating adler checksumm for piece of bitmap */
    cadler = aal_adler32(current, n);

    /* 
        If loaded checksum and calculated are not equal, then we have corrupted 
        bitmap.
    */
    if (ladler != cadler) {
        aal_exception_error("Checksum missmatch in bitmap block %llu. "
	    "Loaded checksum is 0x%x, calculated one is 0x%x.", blk, 
	    ladler, cadler);
	
	return -1;
    }

    return 0;
}

/* Checks allocator on validness using loaded checksums */
errno_t alloc40_valid(reiser4_entity_t *entity) {
    reiser4_layout_func_t layout;
    
    alloc40_t *alloc = (alloc40_t *)entity;
    
    aal_assert("umka-963", alloc != NULL, return -1);
    aal_assert("umka-964", alloc->bitmap != NULL, return -1);
    
    if (!(layout = alloc->format->plugin->format_ops.alloc_layout)) {
	aal_exception_error("Method \"alloc_layout\" doesn't implemented "
	    "in format plugin.");
	return -1;
    }
    
    if (layout(alloc->format, callback_check_bitmap, alloc)) {
	aal_exception_error("Can't check bitmap on validness.");
	return -1;
    }

    return 0;
}

/* Filling the alloc40 structure by methods */
static reiser4_plugin_t alloc40_plugin = {
    .alloc_ops = {
	.h = {
	    .handle = NULL,
	    .id = ALLOC_REISER40_ID,
	    .type = ALLOC_PLUGIN_TYPE,
	    .label = "alloc40",
	    .desc = "Space allocator for reiserfs 4.0, ver. " VERSION,
	},
	.open	    = alloc40_open,
	.close	    = alloc40_close,

#ifndef ENABLE_COMPACT
	.create	    = alloc40_create,
	.sync	    = alloc40_sync,
	.mark	    = alloc40_mark,
	.allocate   = alloc40_allocate,
	.release    = alloc40_release,
#else
	.create	    = NULL,
	.sync	    = NULL,
	.mark	    = NULL,
	.allocate   = NULL,
	.release    = NULL,
#endif
	.test	    = alloc40_test,
	.free	    = alloc40_free,
	.used	    = alloc40_used,
	.valid	    = alloc40_valid
    }
};

static reiser4_plugin_t *alloc40_start(reiser4_core_t *c) {
    core = c;
    return &alloc40_plugin;
}

plugin_register(alloc40_start);

