/*
    journal.h -- reiserfs node functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
 */ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>

struct reiserfs_node_common_header {
    uint16_t node_plugin_id; 
};

typedef struct reiserfs_node_common_header reiserfs_node_common_header_t;

struct reiserfs_node {
    reiserfs_node_common_header_t *header;
    reiserfs_node_opaque_t *entity; 
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_node reiserfs_node_t;

extern reiserfs_node_t *reiserfs_node_open(aal_block_t *block);
extern reiserfs_node_t *reiserfs_node_create(aal_block_t *block, uint32_t level);
extern int reiserfs_node_check(reiserfs_node_t *node, int flags);
extern void reiserfs_node_close(reiserfs_node_t *node, int sync);
    
extern uint32_t reiserfs_max_item_size (void);
extern uint32_t reiserfs_max_item_num  (void);
extern uint32_t reiserfs_count (const reiserfs_node_t *);
extern uint32_t reiserfs_level (const reiserfs_node_t *);
extern uint32_t reiserfs_get_free_space (const reiserfs_node_t *);
extern void     reiserfs_set_free_space (const reiserfs_node_t *);
     
void (*print) (const reiserfs_node_t * node);

#endif

