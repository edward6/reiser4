/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>
#include <reiserfs/node.h>

/* Format of node header for node40. */
typedef struct reiserfs_node40_header {
    reiserfs_node_header_t header;
    size_t free_space;
    size_t free_space_start;
    uint8_t level;
    uint32_t magic;
    uint16_t num_items;
    char flags;
    uint32_t flush_time;
} reiserfs_node40;

typedef struct reiserfs_node40_header reiserfs_node40_header_t;  

/* Node object which plugin works with */
struct reiserfs_node40 {
    aal_block_t  *block;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
/*struct reiserfs_item_header40 {
    reiser4_key key;	    
    node40_size_t offset;
    uint16_t length;
    uint16_t plugin_id;
};*/

typedef struct reiserfs_item_header40 reiserfs_item_header40_t;

#endif

