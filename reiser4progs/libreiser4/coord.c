/*
    coord.c -- reiserfs tree coord functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

reiserfs_coord_t *reiserfs_coord_create(reiserfs_node_t *node, 
    uint16_t item, uint16_t unit)
{
    reiserfs_coord_t *coord;

    if (!(coord = aal_calloc(sizeof(*coord), 0)))
	return NULL;

    reiserfs_coord_init(coord, node, item, unit);
    return coord;
}

errno_t reiserfs_coord_init(reiserfs_coord_t *coord, 
    reiserfs_node_t *node, uint16_t item, uint16_t unit)
{
    aal_assert("umka-795", coord != NULL, return -1);
    coord->node = node;
    coord->pos.item = item;
    coord->pos.unit = unit;

    return 0;
}

void reiserfs_coord_free(reiserfs_coord_t *coord) {
    aal_assert("umka-793", coord != NULL, return);
    aal_free(coord);
}

