/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

static uint32_t reiser4_node_magic = 0x52344653; /* (*(__u32 *)"R4FS"); */

/* Format of node header for node40. */
struct reiserfs_node40_header {
    reiserfs_node_common_header_t header;
    uint16_t free_space;
    uint16_t free_space_start;
    uint8_t level;
    uint32_t magic;
    uint16_t num_items;
/*    char flags;  - still commented out in kernel */
    uint32_t flush_time;
};

typedef struct reiserfs_node40_header reiserfs_node40_header_t;  

#define get_node_free_space(node)		get_le16(node, free_space)
#define set_node_free_space(node, val)		set_le16(node, free_space, val)

#define get_node_free_space_start(node)		get_le16(node, free_space_start)
#define set_node_free_space_start(node, val)	set_le16(node, free_space_start, val)

#define get_node_level(node)			node->level 
#define set_node_level(node, val)		node->level = val

#define get_node_magic(node)			get_le32(node, magic)
#define set_node_magic(node, val)		set_le32(node, magic, val)

#define get_node_num_items(node)		get_le16(node, num_items)
#define set_node_num_items(node, val)		set_le16(node, num_items, val)

#define get_node_flush_time(node)		get_le32(node, flush_time)
#define set_node_flush_time(node, val)		set_le32(node, flush_time, val)



/* Node object which plugin works with */
struct reiserfs_node40 {
    aal_device_block_t  *block;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
struct reiserfs_item_header40 {
    reiser4_key_t key;	    
    uint16_t offset;
    uint16_t length;
    uint16_t plugin_id;
};

typedef struct reiserfs_item_header40 reiserfs_item_header40_t;

#endif

