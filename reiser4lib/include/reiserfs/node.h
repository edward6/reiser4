/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>
#include <reiserfs/path.h>

struct reiserfs_node_common_header {
    uint16_t plugin_id; 
};

typedef struct reiserfs_node_common_header reiserfs_node_common_header_t;

struct reiserfs_node {
    aal_device_t *device;
    aal_block_t *block;
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_node reiserfs_node_t;

extern reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_plugin_id_t plugin_id);

extern reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk, 
    reiserfs_plugin_id_t plugin_id, uint8_t level);

extern void reiserfs_node_close(reiserfs_node_t *node);
extern reiserfs_plugin_id_t reiserfs_node_plugin(reiserfs_node_t *node);
    
extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);
extern reiserfs_coord_t *reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key);

extern uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node);
extern uint32_t reiserfs_node_item_maxnum(reiserfs_node_t *node);
extern uint32_t reiserfs_node_item_count(reiserfs_node_t *node);

extern uint8_t reiserfs_node_level(reiserfs_node_t *node);
extern reiserfs_plugin_id_t reiserfs_node_plugin(reiserfs_node_t *node);

extern uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value);

extern reiserfs_item_info_t *reiserfs_node_get_item_info (reiserfs_node_t *node, uint32_t pos);
extern void *reiserfs_node_get_item (reiserfs_node_t *node, uint32_t pos);

#endif

