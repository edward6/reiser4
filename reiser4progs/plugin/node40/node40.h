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

/* (*(__u32 *)"R4FS") */
#define NODE40_MAGIC 0x52344653

struct node40 {
    aal_block_t *block;
};

typedef struct node40 node40_t;

struct flush_stamp {
    uint32_t mkfs_id;
    uint64_t flush_time;
};

typedef struct flush_stamp flush_stamp_t;

/* Format of node header for node40 */
struct node40_header {
    uint16_t pid; 
    uint16_t free_space;
    uint16_t free_space_start;
    uint8_t level;
    uint32_t magic;
    uint16_t num_items;
    
    flush_stamp_t flush_stamp;
};

typedef struct node40_header node40_header_t;  

#define	nh40(block)				((node40_header_t *)block->data)

#define nh40_get_pid(header)			aal_get_le16(header, pid)
#define nh40_set_pid(header, val)		aal_set_le16(header, pid, val)

#define nh40_get_free_space(header)		aal_get_le16(header, free_space)
#define nh40_set_free_space(header, val)	aal_set_le16(header, free_space, val)

#define nh40_get_free_space_start(header)	aal_get_le16(header, free_space_start)
#define nh40_set_free_space_start(header, val)	aal_set_le16(header, free_space_start, val)

#define nh40_get_level(header)			(header->level)
#define nh40_set_level(header, val)		(header->level = val)

#define nh40_get_magic(header)			aal_get_le32(header, magic)
#define nh40_set_magic(header, val)		aal_set_le32(header, magic, val)

#define nh40_get_num_items(header)		aal_get_le16(header, num_items)
#define nh40_set_num_items(header, val)		aal_set_le16(header, num_items, val)

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/

union key40 {
    uint64_t el[3];
    int pad;
};

typedef union key40 key40_t;

struct item40_header {
    key40_t key;
    
    uint16_t offset;
    uint16_t len;
    uint16_t pid;
};

typedef struct item40_header item40_header_t;

#define ih40_get_offset(ih)			aal_get_le16(ih, offset)
#define ih40_set_offset(ih, val)		aal_set_le16(ih, offset, val)

#define ih40_get_len(ih)			aal_get_le16(ih, len)
#define ih40_set_len(ih, val)			aal_set_le16(ih, len, val)

#define ih40_get_pid(ih)			aal_get_le16(ih, pid)
#define ih40_set_pid(ih, val)			aal_set_le16(ih, pid, val)

/* Returns item header by pos */
inline item40_header_t *node40_ih_at(aal_block_t *block, uint32_t pos) {
    return ((item40_header_t *)(block->data + block->size)) - pos - 1;
}

/* Retutrns item body by pos */
inline void *node40_ib_at(aal_block_t *block, uint32_t pos) {
    return block->data + ih40_get_offset(node40_ih_at(block, pos));
}
    
#endif

