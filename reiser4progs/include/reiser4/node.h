/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>
#include <reiser4/key.h>
#include <reiser4/plugin.h>

extern reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk,
    reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id, uint8_t level);

/* plugin_id must be specified for 3.x format */
extern reiserfs_node_t *reiserfs_node_init(aal_device_t *device, blk_t blk,
    reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id);

extern void reiserfs_node_fini(reiserfs_node_t *node);
extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);

/*
    I suggest the following results of lookup (item_pos/unit_pos):
    0/-1 - before the 0 item.
    0/0  - before the first unit in the item.
    0/unit_count - after the last unit in the item.
    item_count/ANY - after the last item.
    
    FIXME-VITALY: it works in another way for now.
*/
extern int reiserfs_node_lookup(reiserfs_node_t *node, 
    reiserfs_item_coord_t *coord, reiserfs_key_t *key);

extern reiserfs_plugin_id_t reiserfs_node_plugin_id(reiserfs_node_t *node);
extern void *reiserfs_node_item_at(reiserfs_node_t *node, uint32_t pos);

extern uint8_t reiserfs_node_get_level(reiserfs_node_t *node);
extern void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level);

extern uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value);

/* Item functions */
extern void reiserfs_node_set_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_id_t plugin_id);

extern reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(
    reiserfs_node_t *node, uint32_t pos);

extern void reiserfs_node_set_item_plugin(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *reiserfs_node_get_item_plugin(reiserfs_node_t *node, 
    uint32_t pos);

extern error_t reiserfs_node_insert_item(reiserfs_node_t *node, 
    reiserfs_item_coord_t *coord, reiserfs_key_t *key, reiserfs_item_info_t *item);

extern blk_t reiserfs_node_down_link_item(reiserfs_node_t *node, 
    uint32_t pos);

extern int reiserfs_node_is_internal_item(reiserfs_node_t *node, uint32_t pos);

extern error_t reiserfs_node_estimate_item(reiserfs_node_t *node, 
    reiserfs_item_info_t *item_info, reiserfs_item_coord_t *coord);

extern uint16_t reiserfs_node_overhead_item(reiserfs_node_t *node);
extern uint16_t reiserfs_node_maxsize_item(reiserfs_node_t *node);
extern uint16_t reiserfs_node_maxnum_item(reiserfs_node_t *node);
extern uint16_t reiserfs_node_count_item(reiserfs_node_t *node);

#endif

