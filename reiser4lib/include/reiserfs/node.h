/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>

struct reiserfs_node_common_header {
    uint16_t plugin_id; 
};

typedef struct reiserfs_node_common_header reiserfs_node_common_header_t;

#define get_nh_plugin_id(nh)		get_le16(nh, plugin_id)
#define set_nh_plugin_id(nh, val)	set_le16(nh, plugin_id, val)

struct reiserfs_node {
    reiserfs_node_common_header_t *header;
    reiserfs_node_opaque_t *entity; 
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_node reiserfs_node_t;

extern reiserfs_node_t *reiserfs_node_open(aal_block_t *block);
extern reiserfs_node_t *reiserfs_node_create(aal_block_t *block, uint32_t level);
extern int reiserfs_node_check(reiserfs_node_t *node, int flags);
extern void reiserfs_node_close(reiserfs_node_t *node, int sync);
    
extern uint32_t reiserfs_node_max_item_size(reiserfs_node_t *node);
extern uint32_t reiserfs_node_max_item_num(reiserfs_node_t *node);
extern uint32_t reiserfs_node_count(reiserfs_node_t *node);
extern uint32_t reiserfs_node_level(reiserfs_node_t *node);

extern uint32_t reiserfs_node_free_space(reiserfs_node_t *node);
extern void reiserfs_set_free_space(reiserfs_node_t *node);

#endif

