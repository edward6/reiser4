/*
    item.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef ITEM_H
#define ITEM_H

#include <aal/aal.h>
#include <reiserfs/filesystem.h>

extern blk_t reiserfs_item_down_link(reiserfs_item_t *item, uint16_t unit_pos);
extern int reiserfs_item_is_internal (reiserfs_item_t * item);
extern reiserfs_item_t *reiserfs_item_open(reiserfs_node_t *node, uint16_t pos);

#endif

