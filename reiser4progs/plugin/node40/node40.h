/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_H

#include <aal/aal.h>
#include <misc/misc.h>
#include <reiser4/reiser4.h>

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

#define reiserfs_nh40(block)			((reiserfs_nh40_t *)block->data)

#define nh40_get_free_space(header)		get_le16(header, free_space)
#define nh40_set_free_space(header, val)	set_le16(header, free_space, val)

#define nh40_get_free_space_start(header)	get_le16(header, free_space_start)
#define nh40_set_free_space_start(header, val)	set_le16(header, free_space_start, val)

#define nh40_get_level(header)			(header->level)
#define nh40_set_level(header, val)		(header->level = val)

#define nh40_get_magic(header)			get_le32(header, magic)
#define nh40_set_magic(header, val)		set_le32(header, magic, val)

#define nh40_get_num_items(header)		get_le16(header, num_items)
#define nh40_set_num_items(header, val)		set_le16(header, num_items, val)

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
struct reiserfs_ih40 {
    reiserfs_key40_t key;
    
    uint16_t offset;
    uint16_t length;
    uint16_t plugin_id;
};

typedef struct reiserfs_ih40 reiserfs_ih40_t;

#define node40_ih_at(block, pos) \
    ((reiserfs_ih40_t *) (block->data + block->size) - pos - 1)

#define node40_item_at(block, pos) \
    (block->data + ih40_get_offset(node40_ih_at(block, pos)))
    
#define ih40_get_offset(ih)			get_le16(ih, offset)
#define ih40_set_offset(ih, val)		set_le16(ih, offset, val)

#define ih40_get_length(ih)			get_le16(ih, length)
#define ih40_set_length(ih, val)		set_le16(ih, length, val)

#define ih40_get_plugin_id(ih)			get_le16(ih, plugin_id)
#define ih40_set_plugin_id(ih, val)		set_le16(ih, plugin_id, val)

#endif

