/*
    block.c -- block functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>

/* 
    Allocates one block on specified device. Fills its data field by specified 
    char. Marks it as ditry and returns it to caller. This function is widely 
    used in libreiser4 for working with disk blocks (node.c, almost all plugins).
*/
aal_block_t *aal_block_create(
    aal_device_t *device,	/* device block will eb allocated on */
    blk_t blk,			/* block number for allocating */
    char c			/* char for filling allocated block */
) {
    aal_block_t *block;

    aal_assert("umka-443", device != NULL, return NULL);
    
    if (!(block = (aal_block_t *)aal_calloc(sizeof(*block), 0)))
	return NULL;

    block->device = device;
	    
    if (!(block->data = aal_calloc(aal_device_get_bs(device), c)))
	goto error_free_block;
	
    block->offset = (aal_device_get_bs(device) * blk);
    aal_block_mkdirty(block);
	
    return block;
	
error_free_block:
    aal_free(block);
error:
    return NULL;
}

/*
    Reads one block from specified device. Marks it as clean and returns it 
    to caller. For reading is used aal_device_read routine, see above for 
    more detailed description.
*/
aal_block_t *aal_block_open(
    aal_device_t *device,	/* device block will be read from */
    blk_t blk			/* block number for reading */
) {
    aal_block_t *block;

    aal_assert("umka-444", device != NULL, return NULL);

    /* Allocating new block at passed position blk */    
    if (!(block = aal_block_create(device, blk, 0)))
	return NULL;

    /* Reading block data from device */
    if (aal_device_read(device, block->data, blk, 1)) {
	aal_block_free(block);
	return NULL;
    }
    
    /* 
	Mark block as clean. It means, block will not be realy wrote onto device 
	when aal_block_write method will be called, since block was not chnaged.
    */
    aal_block_mkclean(block);
    
    return block;
}

/* Makes reread of specified block */
errno_t aal_block_reopen(
    aal_block_t *block, 	/* block to be reread */
    aal_device_t *device,	/* device, new block should be reread from */
    blk_t blk			/* block number for rereading */
) {
    aal_assert("umka-631", block != NULL, return -1);
    aal_assert("umka-632", device != NULL, return -1);

    if (aal_device_read(device, block->data, blk, 1))
	return -1;

    aal_block_relocate(block, blk);
    block->device = device;

    return 0;
}

/* 
    Writes specified block onto device. Device reference, block will be wrote 
    onto, stored in block->device field. Marks it as clean and returns error 
    code to caller.
*/
errno_t aal_block_sync(
    aal_block_t *block		/* block for writing */
) {
    errno_t error;
    blk_t blk;

    aal_assert("umka-446", block != NULL, return -1);

/*    if (aal_block_isclean(block))
	return 0;*/

    blk = aal_block_number(block);
    
    if ((error = aal_device_write(block->device, block->data, blk, 1)))
	aal_block_mkclean(block);
    
    return error;
}

/*
    Returns block number of specified block. Block stores own location as 
    offset from the device start in bytes. This ability was introduced to avoid 
    additional activities (for instance, loops for update block number) which 
    should be performed on opened blocks in the case device has been changed its 
    blocksize. It will be used for converting reiserfs from one block size to 
    another.
*/
blk_t aal_block_number(
    aal_block_t *block		/* block, position will be obtained from */
) {
    aal_assert("umka-448", block != NULL, return 0);
   
    /* 
	Here we are using shifting for calculating block position because block
	position is 64-bit number. And gcc is using for multipling and dividing 
	such numbers a special internal function that is not available in allone 
	mode.
    */
    return (blk_t)(block->offset >> 
	aal_log2(aal_device_get_bs(block->device)));
}

/* Sets block number */
void aal_block_relocate(
    aal_block_t *block,		/* block, position will be set to */
    blk_t blk			/* position for setting up */
) {
    aal_assert("umka-450", block != NULL, return);

    /* Checking for passed block validness */
    if (blk > aal_device_len(block->device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't setup block into address out of device.");
	return;
    }
    
    /* 
	Here we are using shifting for calculating block position because block
	position is 64-bit number. And gcc is using for multipling and dividing 
	such numbers a special internal function that is not available in allone 
	mode.
    */
    block->offset = (uint64_t)(blk << 
	aal_log2(aal_device_get_bs(block->device)));
}

uint32_t aal_block_size(aal_block_t *block) {
    aal_assert("umka-1049", block != NULL, return 0);
    return block->device->blocksize;
}

/* Frees block instance and all assosiated memory */
void aal_block_free(
    aal_block_t *block		/* block to be released */
) {
    aal_assert("umka-451", block != NULL, return);
	
    aal_free(block->data);
    aal_free(block);
}

