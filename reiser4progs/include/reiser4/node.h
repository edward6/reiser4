/*
    node.h -- reiserfs formated node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/ 

#ifndef NODE_H
#define NODE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/key.h>
#include <reiser4/plugin.h>

/* 
    Plugin id for reiser3 filesystems. This will be configurable value. There
    will be ability to specify it in configure time in maner like this:

    ./configure --reiser3-node-pid=0x1
*/
#define REISERFS_NODE36_PID (0x1)

extern reiserfs_node_t *reiserfs_node_open(aal_device_t *device, 
    blk_t blk, reiserfs_id_t key_pid);

extern errno_t reiserfs_node_close(reiserfs_node_t *node);

extern errno_t reiserfs_node_split(reiserfs_node_t *node, 
    reiserfs_node_t *right);

extern errno_t reiserfs_node_rdkey(reiserfs_node_t *node, 
    reiserfs_key_t *key);

extern errno_t reiserfs_node_ldkey(reiserfs_node_t *node, 
    reiserfs_key_t *key);

extern errno_t reiserfs_node_remove(reiserfs_node_t *node, 
    reiserfs_pos_t *pos);

extern errno_t reiserfs_node_copy(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos);

extern errno_t reiserfs_node_move(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos);

extern uint32_t reiserfs_node_maxnum(reiserfs_node_t *node);
extern uint32_t reiserfs_node_count(reiserfs_node_t *node);

extern int reiserfs_node_lookup(reiserfs_node_t *node, 
    reiserfs_key_t *key, reiserfs_pos_t *pos);

extern errno_t reiserfs_node_check(reiserfs_node_t *node, int flags);

extern blk_t reiserfs_node_get_pointer(reiserfs_node_t *node, 
    uint32_t pos);

extern int reiserfs_node_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk);

extern int reiserfs_node_item_internal(reiserfs_node_t *node, 
    uint32_t pos);

#ifndef ENABLE_COMPACT

extern reiserfs_node_t *reiserfs_node_create(aal_device_t *device, 
    blk_t blk, reiserfs_id_t key_pid, reiserfs_id_t node_pid, 
    uint8_t level);

extern errno_t reiserfs_node_sync(reiserfs_node_t *node);
extern errno_t reiserfs_node_flush(reiserfs_node_t *node);

extern errno_t reiserfs_node_insert(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item);

extern errno_t reiserfs_node_set_key(reiserfs_node_t *node, uint32_t pos, 
    reiserfs_key_t *key);

extern errno_t reiserfs_node_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk); 

#endif

extern errno_t reiserfs_node_get_key(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_key_t *key);

extern uint32_t reiserfs_node_get_pid(reiserfs_node_t *node);

extern uint8_t reiserfs_node_get_level(reiserfs_node_t *node);
extern uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node);

#ifndef ENABLE_COMPACT

extern errno_t reiserfs_node_set_pid(reiserfs_node_t *node, 
    uint32_t pid);

extern errno_t reiserfs_node_set_level(reiserfs_node_t *node, 
    uint8_t level);

extern errno_t reiserfs_node_set_free_space(reiserfs_node_t *node, 
    uint32_t value);

extern errno_t reiserfs_node_item_set_pid(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_id_t pid);

extern errno_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item);

#endif

extern reiserfs_id_t reiserfs_node_item_get_pid(reiserfs_node_t *node, 
    uint32_t pos);

extern reiserfs_plugin_t *reiserfs_node_item_get_plugin(reiserfs_node_t *node, 
    uint32_t pos);

extern uint32_t reiserfs_node_item_overhead(reiserfs_node_t *node);
extern uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node);

extern uint32_t reiserfs_node_item_len(reiserfs_node_t *node, 
    uint32_t pos);

extern void *reiserfs_node_item_body(reiserfs_node_t *node, 
    uint32_t pos);

#endif

