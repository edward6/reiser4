/*
    direntry40.h -- reiserfs default directory structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef DIRENTRY40_H
#define DIRENTRY40_H

#include <reiserfs/key.h>

struct reiserfs_direntry40_unit {
    reiserfs_dirid_t hash;
    uint16_t offset;
};

typedef struct reiserfs_direntry40_unit reiserfs_direntry40_unit_t;

struct reiserfs_direntry40 {
    uint16_t num_entries;
    reiserfs_direntry40_unit_t entry[0];
};

typedef struct reiserfs_direntry40 reiserfs_direntry40_t;

#endif

