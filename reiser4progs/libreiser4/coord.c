/*
    coord.c -- reiserfs tree coord functions. Coord contains full information
    about smaller tree element position in the tree. The instance of structure 
    reiserfs_coord_t contains pointer to cache where needed unit or item lies,
    item position and unit position in specified item. As cache is wrapper for 
    reiserfs_node_t, we are able to access nodes stored in cache by nodes funcs.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

/* Initializes reiserfs_pos_t struct */
inline void reiserfs_pos_init(
    reiserfs_pos_t *pos,	/* pos to be initialized */
    uint32_t item,		/* item number */
    uint32_t unit		/* unit number */
) {
    aal_assert("umka-955", pos != NULL, return);
    pos->item = item;
    pos->unit = unit;
}

/* Creates coord instance based on passed cache, item pos and unit pos params */
reiserfs_coord_t *reiserfs_coord_create(
    reiserfs_cache_t *cache,	/* the first component of coord */
    uint16_t item,		/* the second one */
    uint16_t unit		/* the third one */
) {
    reiserfs_coord_t *coord;

    /* Allocating memory for instance of coord */
    if (!(coord = aal_calloc(sizeof(*coord), 0)))
	return NULL;

    /* Initializing needed fields */
    reiserfs_coord_init(coord, cache, item, unit);
    return coord;
}

/* This function initializes passed coord by specified params */
errno_t reiserfs_coord_init(
    reiserfs_coord_t *coord,	/* coord to be initialized */
    reiserfs_cache_t *cache,	/* the first component of coord */
    uint16_t item,		/* the second one */
    uint16_t unit		/* the third one */
) {
    aal_assert("umka-795", coord != NULL, return -1);
    
    coord->cache = cache;
    coord->pos.item = item;
    coord->pos.unit = unit;

    return 0;
}

/* Freeing passed coord */
void reiserfs_coord_free(
    reiserfs_coord_t *coord	/* coord to be freed */
) {
    aal_assert("umka-793", coord != NULL, return);
    aal_free(coord);
}

