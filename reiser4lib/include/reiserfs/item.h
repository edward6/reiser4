/*
    item.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef ITEM_H
#define ITEM_H

#include <aal/aal.h>
#include <reiserfs/filesystem.h>

extern reiserfs_item_t *reiserfs_item_alloc();
extern error_t reiserfs_item_free(reiserfs_item_t *item);
extern error_t reiserfs_item_create(reiserfs_item_t *item, reiserfs_item_info_t *info);
extern error_t reiserfs_item_open(reiserfs_item_t *item);
extern error_t reiserfs_item_close(reiserfs_item_t *item);
extern blk_t reiserfs_item_down_link(reiserfs_item_t *item);
extern int reiserfs_item_is_internal (reiserfs_item_t * item);

/* 
    If item_info->plugin != NULL
    1. If coord is NULL or coord->unit_pos == -1 then use id.
    2. Otherwise take plugin_id from coord.
    Function sets item_info->length and item_info->plugin.
*/
extern int reiserfs_item_estimate (reiserfs_coord_t *coord, 
    reiserfs_item_info_t *item_info, reiserfs_plugin_id_t id);

#endif

