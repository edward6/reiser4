/*
    device.c -- device independent interface and block-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

#define aal_device_check_routine(device, routine, action)		    \
    do {								    \
	if (!device->ops->routine) {					    \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,		    \
		"Device operation \"" #routine "\" isn't implemented.");    \
	    action;							    \
	}								    \
    } while (0)

aal_device_t *aal_device_open(struct aal_device_ops *ops, uint16_t blocksize, 
    int flags, void *data) 
{
    aal_device_t *device;

    aal_assert("umka-429", ops != NULL, return NULL);
    
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Block size %u isn't power of two.", blocksize);
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
    aal_assert("umka-430", device != NULL, return);
    aal_free(device);
}

error_t aal_device_set_bs(aal_device_t *device, uint16_t blocksize) {

    aal_assert("umka-431", device != NULL, return -1);
	
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block size %u isn't power of two.", blocksize);
	return -1;
    }	
    device->blocksize = blocksize;
	
    return 0;
}

uint16_t aal_device_get_bs(aal_device_t *device) {

    aal_assert("umka-432", device != NULL, return 0);

    return device->blocksize;
}

error_t aal_device_read(aal_device_t *device, void *buff, blk_t block, count_t count) {
    aal_assert("umka-433", device != NULL, return -1);
    
    aal_device_check_routine(device, read, return -1);
    return device->ops->read(device, buff, block, count);
}

error_t aal_device_write(aal_device_t *device, void *buff, blk_t block, count_t count) {
    aal_assert("umka-434", device != NULL, return -1);
    aal_assert("umka-435", buff != NULL, return -1);
	
    aal_device_check_routine(device, write, return -1);
    return device->ops->write(device, buff, block, count);
}

error_t aal_device_sync(aal_device_t *device) {
    aal_assert("umka-436", device != NULL, return -1);
    
    aal_device_check_routine(device, sync, return -1);
    return device->ops->sync(device);
}

error_t aal_device_flags(aal_device_t *device) {
    aal_assert("umka-437", device != NULL, return -1);

    aal_device_check_routine(device, flags, return -1);
    return device->ops->flags(device);
}

int aal_device_equals(aal_device_t *device1, aal_device_t *device2) {
    aal_assert("umka-438", device1 != NULL, return 0);
    aal_assert("umka-439", device2 != NULL, return 0);
	
    aal_device_check_routine(device1, equals, return 0);
    return device1->ops->equals(device1, device2);
}

uint32_t aal_device_stat(aal_device_t *device) {
    aal_assert("umka-440", device != NULL, return 0);
	
    aal_device_check_routine(device, stat, return 0);
    return device->ops->stat(device);
}

count_t aal_device_len(aal_device_t *device) {
    aal_assert("umka-441", device != NULL, return 0);	

    aal_device_check_routine(device, len, return 0);
    return device->ops->len(device);
}

char *aal_device_name(aal_device_t *device) {
    aal_assert("umka-442", device != NULL, return NULL);
    
    return device->name;
}

/* Block-working functions */
aal_block_t *aal_block_alloc(aal_device_t *device, blk_t blk, char c) {
    aal_block_t *block;

    aal_assert("umka-443", device != NULL, return NULL);
    
    if (blk > aal_device_len(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block %llu beyond of device (0-%llu).", 
	    blk, aal_device_len(device));
	return NULL;
    }
    
    if (!(block = (aal_block_t *)aal_calloc(sizeof(*block), 0)))
	return NULL;

    block->size = aal_device_get_bs(device);
    block->device = device;
	    
    if (!(block->data = aal_calloc(block->size, c)))
	goto error_free_block;
	
    block->offset = (aal_device_get_bs(device) * blk);
    aal_block_dirty(block);
	
    return block;
	
error_free_block:
    aal_free(block);
error:
    return NULL;
}

aal_block_t *aal_block_read(aal_device_t *device, blk_t blk) {
    aal_block_t *block;

    aal_assert("umka-444", device != NULL, return NULL);
	
    if (blk > aal_device_len(device))
	return NULL;
	
    if (!(block = aal_block_alloc(device, blk, 0)))
	return NULL;

    if (aal_device_read(device, block->data, blk, 1)) {
	aal_block_free(block);
	return NULL;
    }
    
    aal_block_clean(block);
    
    return block;
}

error_t aal_block_reread(aal_block_t *block, aal_device_t *device, blk_t blk) {
    aal_assert("umka-631", block != NULL, return -1);
    aal_assert("umka-632", device != NULL, return -1);

    if (blk > aal_device_len(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't reread block %llu beyond of device (0-%llu).", 
	    blk, aal_device_len(device));
	return -1;
    }
    
    if (aal_device_read(device, block->data, blk, 1))
	return -1;

    aal_block_set_nr(block, blk);
    block->device = device;
    return 0;
}

error_t aal_block_write(aal_device_t *device, aal_block_t *block) {
    error_t error;

    aal_assert("umka-445", device != NULL, return -1);
    aal_assert("umka-446", block != NULL, return -1);

    if (aal_block_is_clean(block))
	return 0;

    if((error = aal_device_write(device, block->data, 
	aal_block_get_nr(block), 1)))

    aal_block_clean(block);
    
    return error;
}

blk_t aal_block_get_nr(aal_block_t *block) {
    aal_assert("umka-448", block != NULL, return 0);
   
    return (blk_t)(block->offset >> 
	aal_log2(aal_device_get_bs(block->device)));
}

void aal_block_set_nr(aal_block_t *block, blk_t blk) {
    aal_assert("umka-450", block != NULL, return);

    if (blk > aal_device_len(block->device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't setup block into address out of device.");
	return;
    }
    
    block->offset = (uint64_t)(blk * aal_device_get_bs(block->device));
}

void aal_block_free(aal_block_t *block) {
    aal_assert("umka-451", block != NULL, return);
	
    aal_free(block->data);
    aal_free(block);
}

