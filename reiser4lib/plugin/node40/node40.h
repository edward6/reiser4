/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_H

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <misc/misc.h>

/* (*(__u32 *)"R4FS"); */
#define REISERFS_NODE40_MAGIC 0x52344653

typedef struct flush_stamp {
    uint32_t mkfs_id;
    uint64_t flush_time;
} flush_stamp_t;

/* Format of node header for node40. */
struct reiserfs_nh40 {
    reiserfs_node_common_header_t header;
    uint16_t free_space;
    uint16_t free_space_start;
    uint8_t level;
    uint32_t magic;
    uint16_t num_items;
    flush_stamp_t flush_stamp;
};

typedef struct reiserfs_nh40 reiserfs_nh40_t;  

#define reiserfs_nh40(node)			((reiserfs_nh40_t *)reiserfs_node_data(node))

#define get_nh40_free_space(header)		get_le16(header, free_space)
#define set_nh40_free_space(header, val)	set_le16(header, free_space, val)

#define get_nh40_free_space_start(header)	get_le16(header, free_space_start)
#define set_nh40_free_space_start(header, val)	set_le16(header, free_space_start, val)

#define get_nh40_level(header)			(header->level)
#define set_nh40_level(header, val)		(header->level = val)

#define get_nh40_magic(header)			get_le32(header, magic)
#define set_nh40_magic(header, val)		set_le32(header, magic, val)

#define get_nh40_num_items(header)		get_le16(header, num_items)
#define set_nh40_num_items(header, val)		set_le16(header, num_items, val)

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
struct reiserfs_ih40 {
    reiserfs_key_t key;	    
    uint16_t offset;
    uint16_t length;
    uint16_t plugin_id;
};

typedef struct reiserfs_ih40 reiserfs_ih40_t;

#define reiserfs_node40_ih_at(node, pos) \
    ((reiserfs_ih40_t *) (reiserfs_node_data(node) + reiserfs_node_block(node)->size) \
     - pos - 1)

#define reiserfs_node40_item_at(node, pos) \
    reiserfs_node_data(node) + get_ih40_offset(reiserfs_node40_ih_at(node, pos))
    
#define get_ih40_offset(ih)        get_le16(ih, offset)
#define set_ih40_offset(ih,val)    set_le16(ih, offset, val)

#define get_ih40_length(ih)        get_le16(ih, length)
#define set_ih40_length(ih,val)    set_le16(ih, length, val)

#define get_ih40_plugin_id(ih)     get_le16(ih, plugin_id)
#define set_ih40_plugin_id(ih,val) set_le16(ih, plugin_id, val)

#endif

