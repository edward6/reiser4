/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>

struct reiserfs_node_common_header {
    uint16_t plugin_id; 
};

typedef struct reiserfs_node_common_header reiserfs_node_common_header_t;

struct reiserfs_node {
    reiserfs_node_opaque_t *entity; 
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_node reiserfs_node_t;

#define reiserfs_node_get_plugin_id(block) \
    get_le16((reiserfs_node_common_header_t *)block->data, plugin_id)

#define reiserfs_node_set_plugin_id(block, id) \
    set_le16((reiserfs_node_common_header_t *)block->data, plugin_id, id)

//extern error_t reiserfs_node_confirm_format(aal_device_block_t *block);
extern reiserfs_node_t *reiserfs_node_open(aal_device_block_t *block);

extern reiserfs_node_t *reiserfs_node_create(aal_device_block_t *block, 
    reiserfs_plugin_id_t plugin_id, uint8_t level);

void reiserfs_node_close(reiserfs_node_t *node, int sync);
    
extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);

extern uint32_t reiserfs_node_max_item_size(reiserfs_node_t *node);
extern uint32_t reiserfs_node_max_item_num(reiserfs_node_t *node);
extern uint32_t reiserfs_node_count(reiserfs_node_t *node);
extern uint8_t reiserfs_node_level(reiserfs_node_t *node);

extern uint32_t reiserfs_node_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node);

#endif

