/*
    block.c -- block-working functions.
    Copyright (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

aal_block_t *aal_block_alloc(aal_device_t *device, blk_t blk, char c) {
    aal_block_t *block;

    if (!device)
	return NULL;
	
    if (!(block = (aal_block_t *)aal_calloc(sizeof(*block), 0)))
	return NULL;

    if (!(block->data = aal_calloc(aal_device_blocksize(device), c)))
	goto error_free_block;
	
    block->device = device;
    block->offset = (aal_device_blocksize(device) * blk);
	
    return block;
	
error_free_block:
    aal_free(block);
error:
    return NULL;
}

aal_block_t *aal_block_realloc(aal_block_t *block, aal_device_t *device, blk_t blk) {

    if (!block || !device)
	return NULL;
	
    if (!aal_realloc(&block->data, aal_device_blocksize(device)))
	return NULL;

    block->device = device;
    block->offset = (aal_device_blocksize(device) * blk);
	
    return block;
}

aal_block_t *aal_block_alloc_with(aal_device_t *device, blk_t blk, void *data) {
    aal_block_t *block;

    if (!(block = aal_block_alloc(device, blk, 0)))
	return NULL;

    aal_memcpy(block->data, data, aal_device_blocksize(device));
	
    return block;
}

aal_block_t *aal_block_read(aal_device_t *device, blk_t blk) {
    aal_block_t *block;

    if (!device)
	return NULL;
	
    if (blk > aal_device_len(device))
	return NULL;
	
    if (!(block = aal_block_alloc(device, blk, 0)))
	return NULL;

    if (!aal_device_read(device, block->data, blk, 1)) {
	aal_block_free(block);
	return NULL;
    }
	
    return block;
}

int aal_block_write(aal_device_t *device, aal_block_t *block) {
    if (!device || !block)
	return 0;

    if (!block->dirty)
	return 1;
	
    return aal_device_write(device, block->data, aal_block_location(block), 1);
}

aal_device_t *aal_block_device(aal_block_t *block) {
    if (!block)
	return NULL;
	
    return block->device;
}

void aal_block_set_device(aal_block_t *block, aal_device_t *device) {
    if (!block || !device)
	return;

    block->device = device;
}

blk_t aal_block_location(aal_block_t *block) {
    if (!block)
	return 0;

    return (blk_t)(block->offset / aal_device_blocksize(block->device));
}

void aal_block_set_location(aal_block_t *block, blk_t blk) {
    if (!block)	
	return;
	
    block->offset = (uint64_t)(blk * aal_device_blocksize(block->device));
}

void aal_block_free(aal_block_t *block) {
    if (!block)
	return;
	
    aal_free(block->data);
    aal_free(block);
}

