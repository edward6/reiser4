/*
    bitmap.c -- bitmap functions. Bitmap is used by bitmap-based block allocator 
    plugin.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aux/bitmap.h>
#include <aal/aal.h>

/* 
    This macros is used for checking whether given block is inside of allowed 
    range or not. It is used in all bitmap functions.
*/
#define reiser4_bitmap_range_check(bitmap, blk, action)				\
do {										\
    if (blk >= bitmap->total_blocks) {						\
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,			\
	    "Block %llu is out of range (0-%llu)", blk, bitmap->total_blocks);  \
	action;									\
    }										\
} while (0)

/* 
    Checks whether passed block is inside of bitmap and marks it as used. This
    function also increses used blocks counter. 
*/
void reiser4_bitmap_use(
    reiser4_bitmap_t *bitmap,	/* bitmap instance passed blk will be marked in */
    blk_t blk			/* blk to be marked as used */
) {
    aal_assert("umka-336", bitmap != NULL, return);

    if (aal_test_bit(blk, bitmap->map))
	return;
	
    aal_set_bit(blk, bitmap->map);
    bitmap->used_blocks++;
}

/* 
    Checks whether passed block is inside of bitmap and marks it as free. This
    function also descreases used blocks counter. 
*/
void reiser4_bitmap_unuse(
    reiser4_bitmap_t *bitmap,	/* bitmap, passed blk will be marked in */
    blk_t blk			/* blk to be marked as unused */
) {
    aal_assert("umka-337", bitmap != NULL, return);

    reiser4_bitmap_range_check(bitmap, blk, return);
    if (!aal_test_bit(blk, bitmap->map))
	return;
	
    aal_clear_bit(blk, bitmap->map);
    bitmap->used_blocks--;
}

/* 
    Checks whether passed block is inside of bitmap and test it. Returns TRUE
    if block is used, FALSE otherwise.
*/
int reiser4_bitmap_test(
    reiser4_bitmap_t *bitmap,	/* bitmap, passed blk will be tested */
    blk_t blk			/* blk to be tested */
) {
    aal_assert("umka-338", bitmap != NULL, return 0);
    reiser4_bitmap_range_check(bitmap, blk, return 0);
    return aal_test_bit(blk, bitmap->map);
}

/* Finds first unused in bitmap block, starting from passed "start" */
blk_t reiser4_bitmap_find(
    reiser4_bitmap_t *bitmap,	/* bitmap, unused bit will be searched in */
    blk_t start			/* start bit, search should be performed from */
) {
    blk_t blk;
	
    aal_assert("umka-339", bitmap != NULL, return 0);
	
    reiser4_bitmap_range_check(bitmap, start, return 0);

    if ((blk = aal_find_next_zero_bit(bitmap->map, 
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
    bitmap in reiser4_bitmap_open function. See bellow for details.
*/
static blk_t reiser4_bitmap_calc(
    reiser4_bitmap_t *bitmap,	/* bitmap will be used for calculating bits */
    blk_t start,		/* start bit, calculating should be performed from */
    blk_t end,			/* end bit, calculating should be stoped on */
    int flag			/* flag for kind of calculating (used or free) */
) {
    blk_t i, blocks = 0;
	
    aal_assert("umka-340", bitmap != NULL, return 0);
	
    reiser4_bitmap_range_check(bitmap, start, return 0);
    reiser4_bitmap_range_check(bitmap, end - 1, return 0);
	
    for (i = start; i < end; i++)
	blocks += reiser4_bitmap_test(bitmap, i) ? flag : !flag;

    return blocks;
}

/* Public wrapper for previous function */
blk_t reiser4_bitmap_calc_used(
    reiser4_bitmap_t *bitmap	/* bitmap, calculating will be performed in */
) {
    return (bitmap->used_blocks = reiser4_bitmap_calc(bitmap, 0, 
	bitmap->total_blocks, 1));
}

/* The same as previous one */
blk_t reiser4_bitmap_calc_unused(
    reiser4_bitmap_t *bitmap	/* bitmap, calculating will be performed in */
) {
    return reiser4_bitmap_calc(bitmap, 0, bitmap->total_blocks, 0);
}

/* 
    Yet another wrapper. It counts the number of used/unused blocks in specified 
    region.
*/
count_t reiser4_bitmap_calc_used_in_area(
    reiser4_bitmap_t *bitmap,	/* bitmap calculation will be performed in */
    blk_t start,		/* start bit (block) */
    blk_t end			/* end bit (block) */
) {
    return reiser4_bitmap_calc(bitmap, start, end, 1);
}

/* The same as previous one */
count_t reiser4_bitmap_calc_unused_in_area(
    reiser4_bitmap_t *bitmap,	/* bitmap calculation will be performed in */
    blk_t start,		/* start bit */
    blk_t end			/* end bit */
) {
    return reiser4_bitmap_calc(bitmap, start, end, 0);
}

/* Retuns stored value of used blocks from specified bitmap */
count_t reiser4_bitmap_used(
    reiser4_bitmap_t *bitmap	/* bitmap used blocks number will be obtained from */
) {
    aal_assert("umka-343", bitmap != NULL, return 0);
    return bitmap->used_blocks;
}

/* Retuns stored value of free blocks from specified bitmap */
count_t reiser4_bitmap_unused(
    reiser4_bitmap_t *bitmap	/* bitmap unsuded blocks will be obtained from */
) {
    aal_assert("umka-344", bitmap != NULL, return 0);
    return bitmap->total_blocks - bitmap->used_blocks;
}

/* Creates instance of bitmap */
reiser4_bitmap_t *reiser4_bitmap_create(count_t len) {
    reiser4_bitmap_t *bitmap;
	    
    if (!(bitmap = (reiser4_bitmap_t *)aal_calloc(sizeof(*bitmap), 0)))
	return NULL;
	
    bitmap->size = (len + 7) / 8;
    bitmap->used_blocks = 0;
    bitmap->total_blocks = len;
    
    if (!(bitmap->map = aal_calloc(bitmap->size, 0)))
	goto error_free_bitmap;
    
    return bitmap;
    
error_free_bitmap:
    aal_free(bitmap);
    return NULL;
}

/* Makes clone of specified bitmap. Returns it to caller */
reiser4_bitmap_t *reiser4_bitmap_clone(
    reiser4_bitmap_t *bitmap	    /* bitmap clone of which will be created */
) {
    reiser4_bitmap_t *clone;

    aal_assert("umka-358", bitmap != NULL, return 0);	

    if (!(clone = reiser4_bitmap_create(bitmap->total_blocks)))
	return NULL;
	
    clone->size = bitmap->size;
    clone->used_blocks = bitmap->used_blocks;
    
    aal_memcpy(clone->map, bitmap->map, clone->size);
    
    return clone;
}

/* Frees all assosiated with bitmap memory */
void reiser4_bitmap_close(
    reiser4_bitmap_t *bitmap	    /* bitmap to be closed */
) {
    aal_assert("umka-354", bitmap != NULL, return);
    aal_assert("umka-1082", bitmap->map != NULL, return);
	
    aal_free(bitmap->map);
    aal_free(bitmap);
}

/* Returns bitmap's map (memory chunk, bits array placed in) for direct access */
char *reiser4_bitmap_map(
    reiser4_bitmap_t *bitmap	    /* bitmap, the bit array will be obtained from */
) {
    aal_assert("umka-356", bitmap != NULL, return NULL);
    return bitmap->map;
}

