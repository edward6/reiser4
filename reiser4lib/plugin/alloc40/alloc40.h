/*
	alloc40.h -- Space allocator plugin for reiserfs 4.0
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef ALLOC40_H
#define ALLOC40_H

#include <aal/aal.h>

struct reiserfs_alloc40 {
	aal_device_t *device;
};

typedef struct reiserfs_alloc40 reiserfs_alloc40_t;

#endif

