/*
    bitmap.h -- bitmap functions. Bitmap is used by block allocator plugin
    and fsck program. See libmisc/bitmap.c for more details.

    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef BITMAP_H
#define BITMAP_H

#include <aal/aal.h>

/* 
    Bitmap structure. It contains: pointer to device instance bitmap opened on,
    start on device, total blocks bitmap described, used blocks, pointer to memory
    chunk bit array placed in and bit array size.
*/
struct reiser4_bitmap {
    count_t used_blocks;
    count_t total_blocks;
    
    uint32_t size;
    char *map;;
};

typedef struct reiser4_bitmap reiser4_bitmap_t;

extern void reiser4_bitmap_use(reiser4_bitmap_t *bitmap, blk_t blk);
extern void reiser4_bitmap_unuse(reiser4_bitmap_t *bitmap, blk_t blk);
extern int reiser4_bitmap_test(reiser4_bitmap_t *bitmap, blk_t blk);

extern blk_t reiser4_bitmap_find(reiser4_bitmap_t *bitmap, blk_t start);

extern count_t reiser4_bitmap_calc_used(reiser4_bitmap_t *bitmap);
extern count_t reiser4_bitmap_calc_unused(reiser4_bitmap_t *bitmap);

extern count_t reiser4_bitmap_used(reiser4_bitmap_t *bitmap);
extern count_t reiser4_bitmap_unused(reiser4_bitmap_t *bitmap);

extern count_t reiser4_bitmap_calc_used_in_area(reiser4_bitmap_t *bitmap, 
    blk_t start, blk_t end);

extern count_t reiser4_bitmap_calc_unused_in_area(reiser4_bitmap_t *bitmap, 
    blk_t start, blk_t end);

extern reiser4_bitmap_t *reiser4_bitmap_create(count_t len);
extern reiser4_bitmap_t *reiser4_bitmap_clone(reiser4_bitmap_t *bitmap);

extern void reiser4_bitmap_close(reiser4_bitmap_t *bitmap);
extern char *reiser4_bitmap_map(reiser4_bitmap_t *bitmap);

#endif

