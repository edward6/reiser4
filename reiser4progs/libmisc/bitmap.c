/*
    bitmap.c -- bitmap functions. Bitmap is used by block allocator 
    plugin and fsck program.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <misc/bitops.h>
#include <misc/bitmap.h>

#include <aal/debug.h>

#define reiserfs_bitmap_range_check(bitmap, blk, action)			\
do {										\
    if (blk >= bitmap->total_blocks) {						\
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,			\
	    "Block %llu is out of range (0-%llu)", blk, bitmap->total_blocks);  \
	action;									\
    }										\
} while (0)

void reiserfs_bitmap_use(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-336", bitmap != NULL, return);

    reiserfs_bitmap_range_check(bitmap, blk, return);
    if (reiserfs_misc_test_bit(blk, bitmap->map))
	return;
	
    reiserfs_misc_set_bit(blk, bitmap->map);
    bitmap->used_blocks++;
}

void reiserfs_bitmap_unuse(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-337", bitmap != NULL, return);

    reiserfs_bitmap_range_check(bitmap, blk, return);
    if (!reiserfs_misc_test_bit(blk, bitmap->map))
	return;
	
    reiserfs_misc_clear_bit(blk, bitmap->map);
    bitmap->used_blocks--;
}

int reiserfs_bitmap_test(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-338", bitmap != NULL, return 0);
    reiserfs_bitmap_range_check(bitmap, blk, return 0);
    return reiserfs_misc_test_bit(blk, bitmap->map);
}

blk_t reiserfs_bitmap_find(reiserfs_bitmap_t *bitmap, blk_t start) {
    blk_t blk;
	
    aal_assert("umka-339", bitmap != NULL, return 0);
	
    reiserfs_bitmap_range_check(bitmap, start, return 0);
    if ((blk = reiserfs_misc_find_next_zero_bit(bitmap->map, 
	    bitmap->total_blocks, start)) >= bitmap->total_blocks)
	return 0;

    return blk;
}

static blk_t reiserfs_bitmap_calc(reiserfs_bitmap_t *bitmap, 
    blk_t start, blk_t end, int flag) 
{
    blk_t i, blocks = 0;
	
    aal_assert("umka-340", bitmap != NULL, return 0);
	
    reiserfs_bitmap_range_check(bitmap, start, return 0);
    reiserfs_bitmap_range_check(bitmap, end - 1, return 0);
	
    for (i = start; i < end; ) {
#if !defined(__sparc__) && !defined(__sparcv9)
	uint64_t *block64 = (uint64_t *)(bitmap->map + (i >> 0x3));
	uint16_t bits = sizeof(uint64_t) * 8;
		
	if (i + bits < end && i % 0x8 == 0 &&
	    *block64 == (flag == 0 ? 0xffffffffffffffffLL : 0)) 
	{
	    blocks += bits;
	    i += bits;
	} else {
	    if ((flag == 0 ? reiserfs_bitmap_test(bitmap, i) : 
		    !reiserfs_bitmap_test(bitmap, i)))
		blocks++;
	    i++;
	}
#else
	if ((flag == 0 ? reiserfs_bitmap_test(bitmap, i) : 
		!reiserfs_bitmap_test(bitmap, i)))
	    blocks++;
	i++;	
#endif
    }
    return blocks;
}

blk_t reiserfs_bitmap_calc_used(reiserfs_bitmap_t *bitmap) {
    return reiserfs_bitmap_calc(bitmap, 0, bitmap->total_blocks, 0);
}

blk_t reiserfs_bitmap_calc_unused(reiserfs_bitmap_t *bitmap) {
    return reiserfs_bitmap_calc(bitmap, 0, bitmap->total_blocks, 1);
}

blk_t reiserfs_bitmap_calc_used_in_area(reiserfs_bitmap_t *bitmap, 
    blk_t start, blk_t end) 
{
    aal_assert("umka-341", bitmap != NULL, return 0);
    return reiserfs_bitmap_calc(bitmap, start, end, 0);
}

blk_t reiserfs_bitmap_calc_unused_in_area(reiserfs_bitmap_t *bitmap, 
    blk_t start, blk_t end) 
{
    aal_assert("umka-342", bitmap != NULL, return 0);
    return reiserfs_bitmap_calc(bitmap, start, end, 1);
}

blk_t reiserfs_bitmap_used(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-343", bitmap != NULL, return 0);
    return bitmap->used_blocks;
}

blk_t reiserfs_bitmap_unused(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-344", bitmap != NULL, return 0);

    aal_assert("umka-345", bitmap->total_blocks - 
	bitmap->used_blocks > 0, return 0);
	
    return bitmap->total_blocks - bitmap->used_blocks;
}

error_t reiserfs_bitmap_check(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-346", bitmap != NULL, return 0);
	
    if (reiserfs_bitmap_calc_used(bitmap) != bitmap->used_blocks)
	return -1;
	
    return 0;
}

reiserfs_bitmap_t *reiserfs_bitmap_alloc(blk_t len) {
    reiserfs_bitmap_t *bitmap;
	
    aal_assert("umka-357", len > 0, goto error);
	
    if (!(bitmap = (reiserfs_bitmap_t *)aal_calloc(sizeof(*bitmap), 0)))
	goto error;
	
    bitmap->used_blocks = 0;
    bitmap->total_blocks = len;
    bitmap->size = (len + 7) / 8;
    
    if (!(bitmap->map = (char *)aal_calloc(bitmap->size, 0)))
	goto error_free_bitmap;
	
    return bitmap;
	
error_free_bitmap:
    reiserfs_bitmap_close(bitmap);
error:
    return NULL;
}

static error_t callback_bitmap_flush(aal_device_t *device, 
    blk_t blk, char *map, uint32_t chunk, void *data) 
{
    aal_block_t *block;

    if (!(block = aal_block_alloc(device, blk, 0xff)))
	goto error;
		
    aal_memcpy(block->data, map, chunk); 
		
    if (aal_block_write(device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write bitmap block to %llu.", blk);
	goto error_free_block;
    }
    aal_block_free(block);
	
    return 0;
	
error_free_block:
    aal_block_free(block);
error:
    return -1;
}

static error_t callback_bitmap_fetch(aal_device_t *device, 
    blk_t blk, char *map, uint32_t chunk, void *data) 
{
    aal_block_t *block;
	
    if (!(block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read bitmap block %llu.", blk);
	return -1;
    }	
    aal_memcpy(map, block->data, chunk);
    aal_block_free(block);
	
    return 0;
}

error_t reiserfs_bitmap_pipe(reiserfs_bitmap_t *bitmap, 
    reiserfs_bitmap_pipe_func_t *pipe_func, void *data) 
{
    char *map;
    blk_t blk;
    uint16_t left, chunk;
	
    aal_assert("umka-347", bitmap != NULL, return -1);
    aal_assert("umka-348", bitmap->device != NULL, return -1);
	
    for (left = bitmap->size, blk = bitmap->start, map = bitmap->map; left > 0; ) {	
	chunk = (left < aal_device_get_bs(bitmap->device) ? left : 
	    aal_device_get_bs(bitmap->device));
	
	if (pipe_func && pipe_func(bitmap->device, blk, map, chunk, NULL))
	    return -1;
		
	blk = (blk / (aal_device_get_bs(bitmap->device) * 8) + 1) * 
	    (aal_device_get_bs(bitmap->device) * 8);

	map += chunk;
	left -= chunk;
    }
	
    return 0;
}

reiserfs_bitmap_t *reiserfs_bitmap_open(aal_device_t *device, 
    blk_t start, count_t len) 
{
    reiserfs_bitmap_t *bitmap;
	    
    aal_assert("umka-349", device != NULL, return NULL);
	
    if(!(bitmap = reiserfs_bitmap_alloc(len)))
	goto error;
	
    bitmap->start = start;
    bitmap->device = device;
	
    if (reiserfs_bitmap_pipe(bitmap, callback_bitmap_fetch, NULL))
	goto error_free_bitmap;

    if (!(bitmap->used_blocks = reiserfs_bitmap_calc_used(bitmap)))
	goto error_free_bitmap;
	
    return bitmap;
	
error_free_bitmap:
    reiserfs_bitmap_close(bitmap);
error:
    return NULL;
}

reiserfs_bitmap_t *reiserfs_bitmap_create(aal_device_t *device, 
    blk_t start, count_t len) 
{
    blk_t i, bmap_blknr;
    reiserfs_bitmap_t *bitmap;
    
    aal_assert("umka-363", device != NULL, return NULL);

    if (!(bitmap = reiserfs_bitmap_alloc(len)))
	return NULL;
	
    bitmap->start = start;
    bitmap->device = device;
    
    /* Marking first bitmap block as used */
    reiserfs_bitmap_use(bitmap, start);
  
    /* Setting up other bitmap blocks */
    bmap_blknr = (len - 1) / (aal_device_get_bs(device) * 8) + 1;
    for (i = 1; i < bmap_blknr; i++)
	reiserfs_bitmap_use(bitmap, i * aal_device_get_bs(device) * 8);

    return bitmap;
}

static uint32_t reiserfs_bitmap_resize_map(reiserfs_bitmap_t *bitmap, 
    long start, long end) 
{
    char *map;
    long i, right;
    long size = ((end - start) + 7) / 8;
	
    if (start == 0) {
	int chunk;
		
	if (size == (long)bitmap->size)
	    return bitmap->size;
		
  	if (!aal_realloc((void **)&bitmap->map, size))
	    return 0;

  	if ((chunk = size - bitmap->size) > 0)
	    aal_memset(bitmap->map + bitmap->size, 0, chunk);

	return size;
    }

    if (!(map = aal_calloc(size, 0)))
	return 0;

    right = end > (long)bitmap->total_blocks ? (long)bitmap->total_blocks : end;
    
    if (start < 0) {
	for (i = right - 1; i >= 0; i--) {
	    if (reiserfs_misc_test_bit(i, bitmap->map)) {
		if (i + start >= 0)
		    reiserfs_misc_set_bit(i + start, map);
	    }
	}
    } else {
	for (i = start; i < right; i++) {
	    if (reiserfs_misc_test_bit(i, bitmap->map))
		reiserfs_misc_set_bit(i, map);
	}
    }
	
    aal_free(bitmap->map);
    bitmap->map = map;
	
    return size;
}

error_t reiserfs_bitmap_resize(reiserfs_bitmap_t *bitmap, 
    long start, long end) 
{
    int size;
    blk_t i, bmap_old_blknr, bmap_new_blknr;
	
    aal_assert("umka-350", bitmap != NULL, return -1);
    aal_assert("umka-351", end - start > 0, return -1);
	
    if ((size = reiserfs_bitmap_resize_map(bitmap, start, end)) - 
	    bitmap->size == 0)
	return 0;

    bmap_old_blknr = bitmap->size / aal_device_get_bs(bitmap->device);
    
    bmap_new_blknr = (end - start - 1) / 
	(aal_device_get_bs(bitmap->device) * 8) + 1;

    bitmap->size = size;
    bitmap->total_blocks = end - start;
	
    /* Marking new bitmap blocks as used */
    if (bmap_new_blknr - bmap_old_blknr > 0) {
	for (i = bmap_old_blknr; i < bmap_new_blknr; i++)
	    reiserfs_bitmap_use(bitmap, i * aal_device_get_bs(bitmap->device) * 8);
    }

    return 0;
}

blk_t reiserfs_bitmap_copy(reiserfs_bitmap_t *dest_bitmap, 
    reiserfs_bitmap_t *src_bitmap, blk_t len) 
{
	
    aal_assert("umka-352", dest_bitmap != NULL, return 0);
    aal_assert("umka-353", src_bitmap != NULL, return 0);

    if (!len) 
	return 0;
	
    if (reiserfs_bitmap_resize(dest_bitmap, 0, (len > src_bitmap->total_blocks ? 
	    src_bitmap->total_blocks : len)))
        return 0;
	
    aal_memcpy(dest_bitmap->map, src_bitmap->map, dest_bitmap->size);
    dest_bitmap->used_blocks = reiserfs_bitmap_used(dest_bitmap);

    return dest_bitmap->total_blocks;
}

reiserfs_bitmap_t *reiserfs_bitmap_clone(reiserfs_bitmap_t *bitmap) {
    reiserfs_bitmap_t *clone;

    aal_assert("umka-358", bitmap != NULL, return 0);	

    if (!(clone = reiserfs_bitmap_alloc(bitmap->total_blocks)))
	return NULL;
	
    aal_memcpy(clone->map, bitmap->map, clone->size);
    clone->used_blocks = reiserfs_bitmap_used(clone);
	
    return clone;
}

error_t reiserfs_bitmap_sync(reiserfs_bitmap_t *bitmap) {

    if (reiserfs_bitmap_pipe(bitmap, callback_bitmap_flush, NULL))
	return -1;

    return 0;
}

void reiserfs_bitmap_close(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-354", bitmap != NULL, return);
	
    if (bitmap->map)
	aal_free(bitmap->map);

    aal_free(bitmap);
}

reiserfs_bitmap_t *reiserfs_bitmap_reopen(reiserfs_bitmap_t *bitmap, 
    aal_device_t *device) 
{
    blk_t start;
    count_t len;
	
    aal_assert("umka-355", bitmap != NULL, return NULL);

    start = bitmap->start;
    len = bitmap->total_blocks;
		
    reiserfs_bitmap_close(bitmap);

    return reiserfs_bitmap_open(device, start, len);
}

char *reiserfs_bitmap_map(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-356", bitmap != NULL, return NULL);
    return bitmap->map;
}

