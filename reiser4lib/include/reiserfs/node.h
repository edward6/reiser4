/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>
#include <reiserfs/filesystem.h>
#include <reiserfs/plugin.h>
#include <reiserfs/path.h>

extern reiserfs_node_t *reiserfs_node_open(aal_device_t *device, 
    blk_t blk, reiserfs_plugin_id_t plugin_id);

extern reiserfs_node_t *reiserfs_node_create(aal_device_t *device, 
    blk_t blk, reiserfs_plugin_id_t plugin_id, uint8_t level);

extern error_t reiserfs_node_add(reiserfs_node_t *node, reiserfs_node_t *child);

extern void reiserfs_node_close(reiserfs_node_t *node);
extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);

extern reiserfs_coord_t *reiserfs_node_lookup(reiserfs_node_t *node, 
    reiserfs_key_t *key);

extern uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node);
extern uint32_t reiserfs_node_item_maxnum(reiserfs_node_t *node);
extern uint32_t reiserfs_node_item_count(reiserfs_node_t *node);

extern void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level);
extern reiserfs_plugin_id_t reiserfs_node_plugin_id(reiserfs_node_t *node);

extern uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value);

extern void *reiserfs_node_item(reiserfs_node_t *node, uint32_t pos);

extern int reiserfs_node_insert_item(reiserfs_coord_t *coord, reiserfs_key_t *key,
    reiserfs_item_info_t *item);

extern reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, uint16_t pos);

#endif

