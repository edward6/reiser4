/*
    alloc40.h -- default block allocator plugin for reiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC40_H
#define ALLOC40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include <comm/bitmap.h>

struct alloc40 {
    reiser4_plugin_t *plugin;
	
    reiser4_bitmap_t *bitmap;
    reiser4_entity_t *format;
};

typedef struct alloc40 alloc40_t;

#endif

