/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <misc/misc.h>

/* (*(__u32 *)"R4FS"); */
static uint32_t reiserfs_node_magic = 0x52344653;

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

#define node40(node)        ((reiserfs_node40_t *)node)
#define node40_data(node)   (node40(node)->block->data)
#define node40_block(node)  (node40(node)->block)
#define node40_size(node)   (node40(node)->block->size)
#define node40_header(node) ((reiserfs_node40_header_t *) node40_data (node))

#define get_nh40_free_space(header)		get_le16(header, free_space)
#define set_nh40_free_space(header, val)	set_le16(header, free_space, val)

#define get_nh40_free_space_start(header)	get_le16(header, free_space_start)
#define set_nh40_free_space_start(header, val)	set_le16(header, free_space_start, val)

#define get_nh40_level(header)			header->level 
#define set_nh40_level(header, val)		header->level = val

#define get_nh40_magic(header)			get_le32(header, magic)
#define set_nh40_magic(header, val)		set_le32(header, magic, val)

#define get_nh40_num_items(header)		get_le16(header, num_items)
#define set_nh40_num_items(header, val)		set_le16(header, num_items, val)

#define get_nh40_flush_time(header)		get_le32(header, flush_time)
#define set_nh40_flush_time(header, val)	set_le32(header, flush_time, val)

/* Node object which plugin works with */
struct reiserfs_node40 {
    aal_block_t *block;
    aal_device_t *device;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
struct reiserfs_item_header40 {
    reiserfs_key_t key;	    
    uint16_t offset;
    uint16_t length;
    uint16_t plugin_id;
};

typedef struct reiserfs_item_header40 reiserfs_item_header40_t;

#define node40_ih_at(node, pos) \
    ((reiserfs_item_header40_t *) (node40_data(node) + node40_size(node)) - pos - 1)

#define node40_item_at(node, pos) node40_data(node) + get_ih40_offset(node40_ih_at(node, pos))	
    
#define get_ih40_offset(item_header)        get_le16(item_header, offset)
#define set_ih40_offset(item_header,val)    set_le16(item_header, offset, val)

#define get_ih40_length(item_header)        get_le16(item_header, length)
#define set_ih40_length(item_header,val)    set_le16(item_header, length, val)

#define get_ih40_plugin_id(item_header)     get_le16(item_header, plugin_id)
#define set_ih40_plugin_id(item_header,val) set_le16(item_header, plugin_id, val)

#endif

