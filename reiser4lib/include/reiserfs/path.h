/*
    path.h -- path functions nneded for tree lookup.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef PATH_H
#define PATH_H

#include <aal/aal.h>

struct reiserfs_coord {
    void *node;			/* node in the tree */
    int16_t item_pos;		/* pos of an item in the node */
    int16_t unit_pos;		/* pos of an unit in the item */
};

typedef struct reiserfs_coord reiserfs_coord_t;

struct reiserfs_path {
    aal_list_t *entity;		/* list for holding path elements */
    void *data;			/* user specified per-path data */
};

typedef struct reiserfs_path reiserfs_path_t;

extern reiserfs_path_t *reiserfs_path_create(void *data);
extern void reiserfs_path_free(reiserfs_path_t *path);

extern reiserfs_coord_t *reiserfs_path_append(reiserfs_path_t *path, 
    reiserfs_coord_t *coord);

extern reiserfs_coord_t *reiserfs_path_insert(reiserfs_path_t *path, 
    reiserfs_coord_t *coord, uint8_t level);

extern void reiserfs_path_remove(reiserfs_path_t *path, 
    reiserfs_coord_t *coord);

extern void reiserfs_path_delete(reiserfs_path_t *path, 
    uint8_t level);

extern reiserfs_coord_t *reiserfs_path_at(reiserfs_path_t *path, 
    uint8_t level);

extern void reiserfs_path_clear(reiserfs_path_t *path);

#endif

