/*
    device.c -- device independent interface and block-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

#define aal_device_check_routine(device, routine, action) \
    do { \
	if (!device->ops->##routine##) { \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, \
		"Device operation \"" #routine "\" isn't implemented."); \
	    action; \
	} \
    } while (0)

aal_device_t *aal_device_open(struct aal_device_ops *ops, uint32_t blocksize, 
    int flags, void *data) 
{
    aal_device_t *device;
	
    if (!ops) return NULL;
    
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Block size %d isn't power of two.", blocksize);
	return NULL;
    }	
	
    if (!(device = (aal_device_t *)aal_malloc(sizeof(*device))))
	return NULL;

    device->ops = ops;
    device->data = data;
    device->flags = flags;
    device->blocksize = blocksize;
	
    return device;
}

void aal_device_close(aal_device_t *device) {
	
    if (!device) 
	return;
	
    aal_free(device);
}

error_t aal_device_set_blocksize(aal_device_t *device, uint32_t blocksize) {

    if (!device) 
	return 0;
	
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block size %d isn't power of two.", blocksize);
	return -1;
    }	
    device->blocksize = blocksize;
	
    return 0;
}

uint32_t aal_device_get_blocksize(aal_device_t *device) {

    if (!device) 
	return 0;

    return device->blocksize;
}

error_t aal_device_read(aal_device_t *device, void *buff, blk_t block, count_t count) {

    if (!device) 
	return -1;
    
    aal_device_check_routine(device, read, return -1);
    return device->ops->read(device, buff, block, count);
}

error_t aal_device_write(aal_device_t *device, void *buff, blk_t block, count_t count) {

    if (!device) 
	return -1;
	
    aal_device_check_routine(device, write, return -1);
    return device->ops->write(device, buff, block, count);
}

error_t aal_device_sync(aal_device_t *device) {

    if (!device) 
	return -1;

    aal_device_check_routine(device, sync, return -1);
    return device->ops->sync(device);
}

error_t aal_device_flags(aal_device_t *device) {

    if (!device) 
	return -1;

    aal_device_check_routine(device, flags, return -1);
    return device->ops->flags(device);
}

int aal_device_equals(aal_device_t *device1, aal_device_t *device2) {
	
    if (!device1 || !device2) 
	return 0;

    aal_device_check_routine(device1, equals, return 0);
    return device1->ops->equals(device1, device2);
}

uint32_t aal_device_stat(aal_device_t *device) {

    if (!device)
	return 0;
	
    aal_device_check_routine(device, stat, return 0);
    return device->ops->stat(device);
}

count_t aal_device_len(aal_device_t *device) {
	
    if (!device)
	return 0;

    aal_device_check_routine(device, len, return 0);
    return device->ops->len(device);
}

char *aal_device_name(aal_device_t *device) {
    if (!device)
	return NULL;
    
    return device->name;
}

/* Block-working functions */
aal_block_t *aal_device_alloc_block(aal_device_t *device, blk_t blk, char c) {
    aal_block_t *block;

    if (!device)
	return NULL;
	
    if (!(block = (aal_block_t *)aal_calloc(sizeof(*block), 0)))
	return NULL;

    block->size = aal_device_get_blocksize(device);
	    
    if (!(block->data = aal_calloc(block->size, c)))
	goto error_free_block;
	
    block->offset = (aal_device_get_blocksize(device) * blk);
	
    return block;
	
error_free_block:
    aal_free(block);
error:
    return NULL;
}

aal_block_t *aal_device_read_block(aal_device_t *device, blk_t blk) {
    aal_block_t *block;

    if (!device)
	return NULL;
	
    if (blk > aal_device_len(device))
	return NULL;
	
    if (!(block = aal_device_alloc_block(device, blk, 0)))
	return NULL;

    if (aal_device_read(device, block->data, blk, 1)) {
	aal_device_free_block(block);
	return NULL;
    }
	
    return block;
}

error_t aal_device_write_block(aal_device_t *device, aal_block_t *block) {
    if (!device || !block)
	return -1;

    if (!aal_block_dirty(block))
	return 0;
	
    return aal_device_write(device, block->data, 
	aal_device_get_block_location(device, block), 1);
}

blk_t aal_device_get_block_location(aal_device_t *device, aal_block_t *block) {
    if (!block || !device)
	return 0;

    return (blk_t)(block->offset / aal_device_get_blocksize(device));
}

void aal_device_set_block_location(aal_device_t *device, aal_block_t *block, blk_t blk) {
    if (!block || !device)	
	return;
	
    block->offset = (uint64_t)(blk * aal_device_get_blocksize(device));
}

void aal_device_free_block(aal_block_t *block) {
    if (!block)
	return;
	
    aal_free(block->data);
    block->data = NULL;
    aal_free(block);
}

