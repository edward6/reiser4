/*
    journal36.h -- journal plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL36_H
#define JOURNAL36_h

#include <aal/aal.h>

struct journal36 {
    aal_device_t *device;
    aal_block_t *header;
};

typedef struct journal36 journal36_t;

struct journal36_header {
    char jh_unused[100];
};

typedef struct journal36_header journal36_header_t;

#endif

