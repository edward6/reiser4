/*
    alloc40.h -- default block allocator plugin for reiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC40_H
#define ALLOC40_H

#include <aal/aal.h>
#include <misc/bitmap.h>

struct alloc40 {
    aal_device_t *device;
    reiser4_bitmap_t *bitmap;
};

typedef struct alloc40 alloc40_t;

#endif

