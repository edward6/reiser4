/*
    coord.h -- reiserfs tree coord functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef COORD_H
#define COORD_H

#include <reiser4/filesystem.h>

extern reiserfs_coord_t *reiserfs_coord_create(reiserfs_node_t *node, 
    uint16_t item, uint16_t unit);

extern errno_t reiserfs_coord_init(reiserfs_coord_t *coord, 
    reiserfs_node_t *node, uint16_t item, uint16_t unit);

extern void reiserfs_coord_free(reiserfs_coord_t *coord);

#endif

