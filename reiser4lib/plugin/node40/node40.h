/*
    node40.h -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author: Vitaly Fertman.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>

/* format of node header for 40 node layouts. */
typedef struct node40_header {
    reiserfs_node_header_t header;	/* node header common for all node plugins */
    size_t free_space;			/* free space of node in bytes */
    size_t free_space_start;		/* start position of free space in node in bytes */
    uint8_t  level;                     /* 1 is leaf level, 2 is twig level, etc */
    uint32_t magic;                     /* ?? */
    uint16_t num_items;                 /* number of items */
/*    char     flags;	*/
    uint32_t flush_time;		/* flush time, used for solving conflicts in 
					   reiser4fsck */
} node40_header;

typedef struct node40_header node40_header_t;  

/* node object which plugin works with */
struct reiserfs_node40 {
    aal_device_t *block;
    aal_block_t  *header;
};

typedef struct reiserfs_node40 reiserfs_node40_t;

/* item headers are not standard across all node layouts, pass
 * pos_in_node to functions instead */
typedef struct item_header_40 {
    reiser4_key key;	    
    node40_size_t offset;   /* offset from start of a node */
    uint16_t length;
    uint16_t plugin_id;
    /* 2 bytes are lost here */
} item_header_40;

#endif

