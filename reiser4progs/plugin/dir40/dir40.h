/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR40_H
#define DIR40_H

#include <reiser4/plugin.h>

struct reiserfs_dir40 {
    aal_block_t *block;
    uint32_t pos;
};

typedef struct reiserfs_dir40 reiserfs_dir40_t;

#endif

