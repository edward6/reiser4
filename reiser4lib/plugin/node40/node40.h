/*
    node40.h -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>

typedef uint16_t node40_size_t;

/* Format of node header for 40 node layouts. */
struct reiserfs_node40_header {
    reiserfs_node_header_t header;
    node40_size_t free_space; 
    node40_size_t free_space_start;
    uint8_t  level;
    uint32_t magic;
    uint16_t num_items;
    uint32_t flush_time;
};

typedef struct reiserfs_node40_header reiserfs_node40_header_t;

struct reiserfs_node40 {
    aal_device_t *block;
    aal_block_t *header;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

#endif

