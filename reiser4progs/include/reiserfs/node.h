/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <reiserfs/key.h>
#include <reiserfs/path.h>

extern reiserfs_node_t *reiserfs_node_alloc();
extern error_t reiserfs_node_free(reiserfs_node_t *node);

/* If node == NULL, just create in blk a node. */
extern error_t reiserfs_node_create(reiserfs_node_t *node, aal_device_t *device,
    blk_t blk, reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id, uint8_t level);

/* plugin_id must be specified for 3.x format */
extern error_t reiserfs_node_open(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk, reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id);

extern error_t reiserfs_node_close(reiserfs_node_t *node);
extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);

extern int reiserfs_node_lookup(reiserfs_coord_t *coord, reiserfs_key_t *key);

extern uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_max_size(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_count(reiserfs_node_t *node);

extern void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level);
extern reiserfs_plugin_id_t reiserfs_node_plugin_id(reiserfs_node_t *node);

extern uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value);

extern void *reiserfs_node_item(reiserfs_node_t *node, uint32_t pos);

extern int reiserfs_node_insert_item(reiserfs_coord_t *coord, reiserfs_key_t *key,
    reiserfs_item_info_t *item, reiserfs_plugin_id_t id);

extern reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, 
    uint16_t pos);

#define reiserfs_node(node)	    ((reiserfs_node_t *)node)
#define reiserfs_node_block(node)   (reiserfs_node(node)->block)
#define reiserfs_node_data(node)    (reiserfs_node(node)->block->data)

#endif

