/*
    alloc40.h -- Space allocator plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC40_H
#define ALLOC40_H

#include <aal/aal.h>

#include "bitmap.h"

#define REISERFS_ALLOC40_OFFSET (65536 + 8192)

struct reiserfs_alloc40 {
    aal_device_t *device;
    reiserfs_bitmap_t *bitmap;
};

typedef struct reiserfs_alloc40 reiserfs_alloc40_t;

#endif

