/*
	block.h -- block-working functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <aal/aal.h>

struct aal_block {
    int dirty;
    void *data;
    uint64_t location;
    aal_device_t *device;
};

typedef struct aal_block aal_block_t;

#define aal_block_reading_failed(blk, action) \
    do { \
    	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, \
	   "Reading block %lu failed.", blk); \
    	action; \
    } while (0)

#define aal_block_writing_failed(blk, action) \
    do { \
    	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, \
	   "Writing block %lu failed.", blk); \
	action; \
    } while (0)

extern aal_block_t *aal_block_alloc(aal_device_t *device, blk_t blk, char c);
extern aal_block_t *aal_block_realloc(aal_block_t *block, aal_device_t *device, blk_t blk);
extern aal_block_t *aal_block_alloc_with(aal_device_t *device, blk_t blk, void *data);

extern aal_block_t *aal_block_read(aal_device_t *device, blk_t blk);
extern int aal_block_write(aal_device_t *device, aal_block_t *block);

extern aal_device_t *aal_block_device(aal_block_t *block);
extern void aal_block_set_device(aal_block_t *block, aal_device_t *device);

extern blk_t aal_block_location(aal_block_t *block);
extern void aal_block_set_location(aal_block_t *block, blk_t blk);

extern void aal_block_free(aal_block_t *block);

#endif

