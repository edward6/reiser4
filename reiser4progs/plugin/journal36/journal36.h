/*
    journal36.h -- journal plugin for reiser3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL36_H
#define JOURNAL36_h

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct journal36 {
    reiser4_plugin_t *plugin;
    aal_block_t *header;
};

typedef struct journal36 journal36_t;

struct journal36_header {
    char jh_unused[100];
};

typedef struct journal36_header journal36_header_t;

#endif

