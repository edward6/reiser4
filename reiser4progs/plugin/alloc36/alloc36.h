/*
    alloc36.h -- Space allocator plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC36_H
#define ALLOC36_H

#include <aal/aal.h>

struct alloc36 {
    aal_device_t *device;
};

typedef struct alloc36 alloc36_t;

#endif

