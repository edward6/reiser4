/*
	alloc36.h -- Space allocator plugin for reiserfs 3.6.x
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef ALLOC36_H
#define ALLOC36_H

#include <aal/aal.h>

struct reiserfs_alloc36 {
    aal_device_t *device;
};

typedef struct reiserfs_alloc36 reiserfs_alloc36_t;

#endif

