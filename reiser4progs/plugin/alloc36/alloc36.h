/*
    alloc36.h -- Space allocator plugin for reiser3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC36_H
#define ALLOC36_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct reiser4_alloc36 {
    reiser4_plugin_t *plugin;

    reiser4_entity_t *format;
    reiser4_plugin_t *format_plugin;
};

typedef struct reiser4_alloc36 reiser4_alloc36_t;

#endif

