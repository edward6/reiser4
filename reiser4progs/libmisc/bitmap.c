/*
    bitmap.c -- bitmap functions. Bitmap is used by bitmap-based block allocator 
    plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <misc/bitops.h>
#include <misc/bitmap.h>

#include <aal/debug.h>

/* 
    This macros is used for checking whether given block is inside of allowed 
    range or not. It is used in all bitmap functions.
*/
#define reiserfs_bitmap_range_check(bitmap, blk, action)			\
do {										\
    if (blk >= bitmap->total_blocks) {						\
	aal_throw_error(EO_CANCEL, "Block %llu is out of range (0-%llu).\n",	\
	    blk, bitmap->total_blocks);						\
	action;									\
    }										\
} while (0)

/* 
    Checks whether passed block is inside of bitmap and marks it as used. This
    function also increses used blocks counter. 
*/
void reiserfs_bitmap_use(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-336", bitmap != NULL, return);

    reiserfs_bitmap_range_check(bitmap, blk, return);
    if (reiserfs_misc_test_bit(blk, bitmap->map))
	return;
	
    reiserfs_misc_set_bit(blk, bitmap->map);
    bitmap->used_blocks++;
}

/* 
    Checks whether passed block is inside of bitmap and marks it as free. This
    function also descreases used blocks counter. 
*/
void reiserfs_bitmap_unuse(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-337", bitmap != NULL, return);

    reiserfs_bitmap_range_check(bitmap, blk, return);
    if (!reiserfs_misc_test_bit(blk, bitmap->map))
	return;
	
    reiserfs_misc_clear_bit(blk, bitmap->map);
    bitmap->used_blocks--;
}

/* 
    Checks whether passed block is inside of bitmap and test it. Returns TRUE
    if block is used, FALSE otherwise.
*/
int reiserfs_bitmap_test(reiserfs_bitmap_t *bitmap, blk_t blk) {
    aal_assert("umka-338", bitmap != NULL, return 0);
    reiserfs_bitmap_range_check(bitmap, blk, return 0);
    return reiserfs_misc_test_bit(blk, bitmap->map);
}

/* Finds first unused in bitmap block, starting from passed "start" */
blk_t reiserfs_bitmap_find(reiserfs_bitmap_t *bitmap, blk_t start) {
    blk_t blk;
	
    aal_assert("umka-339", bitmap != NULL, return 0);
	
    reiserfs_bitmap_range_check(bitmap, start, return 0);
    if ((blk = reiserfs_misc_find_next_zero_bit(bitmap->map, 
	    bitmap->total_blocks, start)) >= bitmap->total_blocks)
	return 0;

    return blk;
}

/*
    Makes loop through bitmap and calculates the number of used/unused blocks
    in it. If it is possible it tries to find contiguous bitmap areas (64 bit) 
    and in this maner increases performance. This function is used for checking 
    the bitmap on validness. Imagine, we have a number of free blocks in the super 
    block or somewhere else. And we can easily check whether this number equal 
    to actual returned one or not. Also it is used for calculating used blocks of 
    bitmap in reiserfs_bitmap_open function. See bellow for details.
*/
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
	    *block64 == (flag == 0 ? 0xffffffffffffffffll : 0)) 
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

/* Public wrapper for previous function */
blk_t reiserfs_bitmap_calc_used(reiserfs_bitmap_t *bitmap) {
    return reiserfs_bitmap_calc(bitmap, 0, bitmap->total_blocks, 0);
}

/* The same as previous one */
blk_t reiserfs_bitmap_calc_unused(reiserfs_bitmap_t *bitmap) {
    return reiserfs_bitmap_calc(bitmap, 0, bitmap->total_blocks, 1);
}

/* 
    Yet another wraper. It counts the number of used/unused blocks in specified 
    region.
*/
blk_t reiserfs_bitmap_calc_used_in_area(reiserfs_bitmap_t *bitmap, 
    blk_t start, blk_t end) 
{
    aal_assert("umka-341", bitmap != NULL, return 0);
    return reiserfs_bitmap_calc(bitmap, start, end, 0);
}

/* The same as previous one */
blk_t reiserfs_bitmap_calc_unused_in_area(reiserfs_bitmap_t *bitmap, 
    blk_t start, blk_t end) 
{
    aal_assert("umka-342", bitmap != NULL, return 0);
    return reiserfs_bitmap_calc(bitmap, start, end, 1);
}

/* Retuns stored value of used blocks from specified bitmap */
blk_t reiserfs_bitmap_used(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-343", bitmap != NULL, return 0);
    return bitmap->used_blocks;
}

/* Retuns stored value of free blocks from specified bitmap */
blk_t reiserfs_bitmap_unused(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-344", bitmap != NULL, return 0);

    aal_assert("umka-345", bitmap->total_blocks - 
	bitmap->used_blocks > 0, return 0);
	
    return bitmap->total_blocks - bitmap->used_blocks;
}

/* 
    Performs basic check of bitmap consistency, based on comparing of number
    of used blocks from stored field with calculated one.
*/
errno_t reiserfs_bitmap_check(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-346", bitmap != NULL, return 0);
	
    if (reiserfs_bitmap_calc_used(bitmap) != bitmap->used_blocks)
	return -1;
	
    return 0;
}

/* Allocates bitmap for "len" blocks length */
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

/* 
    Callback function for saving one block of bitmap to device. Called from
    reiserfs_bitmap_flush function on every bitmap block.
*/
static errno_t callback_bitmap_flush(aal_device_t *device, 
    blk_t blk, char *map, uint32_t chunk, void *data) 
{
    aal_block_t *block;

    if (!(block = aal_block_alloc(device, blk, 0xff)))
	goto error;
		
    aal_memcpy(block->data, map, chunk); 
		
    if (aal_block_write(block)) {
	aal_throw_error(EO_OK, "Can't write bitmap block to %llu. %s.\n", blk, 
	    aal_device_error(device));
	goto error_free_block;
    }
    aal_block_free(block);
	
    return 0;
	
error_free_block:
    aal_block_free(block);
error:
    return -1;
}

/* 
    Callback function for reading one block of bitmap. It is called from function
    reiserfs_bitmap_fetch.
*/
static errno_t callback_bitmap_fetch(aal_device_t *device, 
    blk_t blk, char *map, uint32_t chunk, void *data) 
{
    aal_block_t *block;
	
    if (!(block = aal_block_read(device, blk))) {
	aal_throw_error(EO_OK, "Can't read bitmap block %llu. %s.\n", 
	    blk, aal_device_error(device));
	return -1;
    }	
    aal_memcpy(map, block->data, chunk);
    aal_block_free(block);
	
    return 0;
}

/*
    The central function for bitmap fetching and flushing. It implements bitmap 
    traverse algorithm and calles for every block passed callback function. This
    functions may perform any actions on specified block. For now tehre are two
    callback functions which are used with this it: callback_bitmap_flush and
    callback_bitmap_fetch. See above for details.
*/
errno_t reiserfs_bitmap_pipe(reiserfs_bitmap_t *bitmap, 
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

/* Allocates bitmap of specified length and fetch it from the device */
reiserfs_bitmap_t *reiserfs_bitmap_open(aal_device_t *device, 
    blk_t start, count_t len) 
{
    reiserfs_bitmap_t *bitmap;
	    
    aal_assert("umka-349", device != NULL, return NULL);
	
    if(!(bitmap = reiserfs_bitmap_alloc(len)))
	goto error;
	
    bitmap->start = start;
    bitmap->device = device;
    
    /* Fetching bitmap from device */
    if (reiserfs_bitmap_pipe(bitmap, callback_bitmap_fetch, NULL))
	goto error_free_bitmap;

    /* Setting up of number of used blocks */
    if (!(bitmap->used_blocks = reiserfs_bitmap_calc_used(bitmap)))
	goto error_free_bitmap;
	
    return bitmap;
	
error_free_bitmap:
    reiserfs_bitmap_close(bitmap);
error:
    return NULL;
}

/* Creates empty bitmap of specified length */
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

/* 
    Resizes bitmap's map to specified start and end. It is useful for filesystem
    resizing, when it it used bitmap based block allocator.
*/
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

/* Resizes bitmap to specified boundaries */
errno_t reiserfs_bitmap_resize(reiserfs_bitmap_t *bitmap, 
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

/* 
    Copies "src_bitmap" to "dst_bitmap". They may have different size. In this 
    case, destination bitmap will be previously resized to be equal with source 
    one.
*/
blk_t reiserfs_bitmap_copy(reiserfs_bitmap_t *dest_bitmap, 
    reiserfs_bitmap_t *src_bitmap, blk_t len) 
{
	
    aal_assert("umka-352", dest_bitmap != NULL, return 0);
    aal_assert("umka-353", src_bitmap != NULL, return 0);

    if (!len) 
	return 0;

    /* Resising destination bitmap */    
    if (reiserfs_bitmap_resize(dest_bitmap, 0, (len > src_bitmap->total_blocks ? 
	    src_bitmap->total_blocks : len)))
        return 0;
    
    /* Updating map and used blocks field in destination bitmap */
    aal_memcpy(dest_bitmap->map, src_bitmap->map, dest_bitmap->size);
    dest_bitmap->used_blocks = reiserfs_bitmap_used(dest_bitmap);

    return dest_bitmap->total_blocks;
}

/* Makes clone of specified bitmap. Returns it to caller */
reiserfs_bitmap_t *reiserfs_bitmap_clone(reiserfs_bitmap_t *bitmap) {
    reiserfs_bitmap_t *clone;

    aal_assert("umka-358", bitmap != NULL, return 0);	

    if (!(clone = reiserfs_bitmap_alloc(bitmap->total_blocks)))
	return NULL;
	
    aal_memcpy(clone->map, bitmap->map, clone->size);
    clone->used_blocks = reiserfs_bitmap_used(clone);
	
    return clone;
}

/* 
    Synchronizes bitmap to device. It uses pipe function and flush callback function 
    to perform this.
*/
errno_t reiserfs_bitmap_sync(reiserfs_bitmap_t *bitmap) {

    if (reiserfs_bitmap_pipe(bitmap, callback_bitmap_flush, NULL))
	return -1;

    return 0;
}

/* Frees all assosiated with bitmap memory */
void reiserfs_bitmap_close(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-354", bitmap != NULL, return);
	
    if (bitmap->map)
	aal_free(bitmap->map);

    aal_free(bitmap);
}

/* Reopens bitmap from specified device */
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

/* Returns bitmap's map (memory chunk, bits array placed in) for direct access */
char *reiserfs_bitmap_map(reiserfs_bitmap_t *bitmap) {
    aal_assert("umka-356", bitmap != NULL, return NULL);
    return bitmap->map;
}

