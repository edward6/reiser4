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
    reiserfs_plugin_id_t plugin_id, uint8_t level);

extern reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_plugin_id_t plugin_id);

extern error_t reiserfs_node_reopen(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk,  reiserfs_plugin_id_t plugin_id);

extern error_t reiserfs_node_close(reiserfs_node_t *node);

extern error_t reiserfs_node_check(reiserfs_node_t *node, int flags);
extern error_t reiserfs_node_sync(reiserfs_node_t *node);
extern error_t reiserfs_node_flush(reiserfs_node_t *node);

/*
    I suggest the following results of lookup (item_pos, unit_pos):
    (i+1, -1) - between i, i+1 items.
    (i, 0) - before the first unit in the item.
    (0, units_count) - after the last unit in the item.
    (item_count, -1) - after the last item.
    
    FIXME-VITALY: it works in another way for now.
*/
extern int reiserfs_node_lookup(reiserfs_node_t *node, void *key, 
    reiserfs_coord_t *coord);

extern reiserfs_plugin_id_t reiserfs_node_get_plugin_id(reiserfs_node_t *node);

extern void reiserfs_node_set_plugin_id(reiserfs_node_t *node, 
    reiserfs_plugin_id_t plugin_id);

extern uint8_t reiserfs_node_get_level(reiserfs_node_t *node);
extern void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level);

extern uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node);
extern void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value);

extern error_t reiserfs_node_add_children(reiserfs_node_t *node, 
    reiserfs_node_t *children);

extern void reiserfs_node_remove_children(reiserfs_node_t *node, 
    reiserfs_node_t *children);

extern reiserfs_node_t *reiserfs_node_find_child(reiserfs_node_t *node, 
    void *key);

/* Item functions */
extern void reiserfs_node_item_set_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_id_t plugin_id);

extern reiserfs_plugin_id_t reiserfs_node_item_get_plugin_id(
    reiserfs_node_t *node, uint32_t pos);

extern void reiserfs_node_item_set_plugin(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *reiserfs_node_item_get_plugin(reiserfs_node_t *node, 
    uint32_t pos);

extern error_t reiserfs_node_item_insert(reiserfs_node_t *node, 
    reiserfs_coord_t *coord, void *key, reiserfs_item_info_t *info);

extern error_t reiserfs_node_item_replace(reiserfs_node_t *node, 
    reiserfs_coord_t *coord, void *key, reiserfs_item_info_t *info);

extern blk_t reiserfs_node_item_get_pointer(reiserfs_node_t *node, 
    uint32_t pos);

extern void reiserfs_node_item_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk); 

extern int reiserfs_node_item_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk);

extern int reiserfs_node_item_internal(reiserfs_node_t *node, uint32_t pos);

extern error_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_item_info_t *info, reiserfs_coord_t *coord);

extern uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node);
extern uint16_t reiserfs_node_item_count(reiserfs_node_t *node);

extern void *reiserfs_node_item_at(reiserfs_node_t *node, uint32_t pos);

extern void *reiserfs_node_item_key_at(reiserfs_node_t *node, uint32_t pos);

#endif

