/*
    coord.h -- reiser4 tree coord functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef COORD_H
#define COORD_H

#include <reiser4/filesystem.h>

extern inline void reiser4_pos_init(reiser4_pos_t *pos,
    uint32_t item, uint32_t unit);

extern reiser4_coord_t *reiser4_coord_create(reiser4_cache_t *cache, 
    uint32_t item, uint32_t unit);

extern errno_t reiser4_coord_init(reiser4_coord_t *coord, 
    reiser4_cache_t *cache, uint32_t item, uint32_t unit);

extern void reiser4_coord_free(reiser4_coord_t *coord);

#endif

