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
    aal_device_t *device;
    
    blk_t start;
    count_t total_blocks;
    count_t used_blocks;
    
    char *map;
    uint32_t size;
};

typedef struct reiser4_bitmap reiser4_bitmap_t;

typedef int (reiser4_bitmap_pipe_func_t)(aal_device_t *, blk_t, 
    char *, uint32_t, void *);

extern void reiser4_bitmap_use(reiser4_bitmap_t *bitmap, blk_t blk);
extern void reiser4_bitmap_unuse(reiser4_bitmap_t *bitmap, blk_t blk);
extern int reiser4_bitmap_test(reiser4_bitmap_t *bitmap, blk_t blk);

extern blk_t reiser4_bitmap_find(reiser4_bitmap_t *bitmap, blk_t start);

extern blk_t reiser4_bitmap_calc_used(reiser4_bitmap_t *bitmap);
extern blk_t reiser4_bitmap_calc_unused(reiser4_bitmap_t *bitmap);

extern blk_t reiser4_bitmap_used(reiser4_bitmap_t *bitmap);
extern blk_t reiser4_bitmap_unused(reiser4_bitmap_t *bitmap);

extern blk_t reiser4_bitmap_calc_used_in_area(reiser4_bitmap_t *bitmap, 
    blk_t start, blk_t end);

extern blk_t reiser4_bitmap_calc_unused_in_area(reiser4_bitmap_t *bitmap, 
    blk_t start, blk_t end);

extern errno_t reiser4_bitmap_check(reiser4_bitmap_t *bitmap);

extern reiser4_bitmap_t *reiser4_bitmap_alloc(count_t len);

extern reiser4_bitmap_t *reiser4_bitmap_open(aal_device_t *device, 
    blk_t start, count_t len);

extern reiser4_bitmap_t *reiser4_bitmap_create(aal_device_t *device, 
    blk_t start, count_t len);

extern errno_t reiser4_bitmap_resize(reiser4_bitmap_t *bitmap, 
    long start, long end);

extern blk_t reiser4_bitmap_copy(reiser4_bitmap_t *dest_bitmap, 
    reiser4_bitmap_t *src_bitmap, count_t len);

extern reiser4_bitmap_t *reiser4_bitmap_clone(reiser4_bitmap_t *bitmap);
extern errno_t reiser4_bitmap_sync(reiser4_bitmap_t *bitmap);
extern void reiser4_bitmap_close(reiser4_bitmap_t *bitmap);

extern reiser4_bitmap_t *reiser4_bitmap_reopen(reiser4_bitmap_t *bitmap, 
    aal_device_t *device);

extern errno_t reiser4_bitmap_pipe(reiser4_bitmap_t *bitmap, 
    reiser4_bitmap_pipe_func_t *pipe_func, void *data);

extern char *reiser4_bitmap_map(reiser4_bitmap_t *bitmap);

#endif

