/*
    node40.h -- reiser4 default node structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

/* Format of node header for node40. */
struct reiserfs_node40_header {
    reiserfs_node_common_header_t header;
    size_t free_space;
    size_t free_space_start;
    uint8_t level;
    uint32_t magic;
    uint16_t num_items;
    char flags;
    uint32_t flush_time;
};

typedef struct reiserfs_node40_header reiserfs_node40_header_t;  

/* Node object which plugin works with */
struct reiserfs_node40 {
    aal_device_block_t  *block;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

typedef enum {
    /** major "locale", aka dirid. Sits in 1st element */
    KEY_LOCALITY_INDEX   = 0,
    /** minor "locale", aka item type. Sits in 1st element */
    KEY_TYPE_INDEX       = 0,
    /** "object band". Sits in 2nd element */
    KEY_BAND_INDEX       = 1,
    /** objectid. Sits in 2nd element */
    KEY_OBJECTID_INDEX   = 1,
    /** Offset. Sits in 3rd element */
    KEY_OFFSET_INDEX     = 2,
    /** Name hash. Sits in 3rd element */
    KEY_HASH_INDEX       = 2,
    KEY_LAST_INDEX       = 3
} reiser4_key_field_index;

union reiser4_key {
    uint64_t el[ KEY_LAST_INDEX ];
    int pad;
};

typedef union reiser4_key reiser4_key_t;

typedef uint16_t node40_size_t;
/* 
    Item headers are not standard across all node layouts, pass
    pos_in_node to functions instead.
*/
struct reiserfs_item_header40 {
    reiser4_key_t key;	    
    node40_size_t offset;
    uint16_t length;
    uint16_t plugin_id;
};

typedef struct reiserfs_item_header40 reiserfs_item_header40_t;

#endif

