/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR40_H
#define DIR40_H

struct reiserfs_dir40 {
    const void *tree;

    reiserfs_place_t place;
    reiserfs_key_t key;
};

typedef struct reiserfs_dir40 reiserfs_dir40_t;

#endif

