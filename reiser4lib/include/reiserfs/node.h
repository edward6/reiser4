/*
 * journal.h -- reiserfs node functions.
 * Copyright (C) 1996-2002 Hans Reiser.
 * Author Vitaly Fertman.
 */ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>

struct reiserfs_node_header {
    uint16_t node_plugin_id; 
};

typedef struct reiserfs_node_header reiserfs_node_header_t;

struct reiserfs_node {
    reiserfs_node_header_t *header;
    reiserfs_node_opaque_t *entity; 
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_node reiserfs_node_t;

extern reiserfs_node_t *reiserfs_node_open(aal_block_t *block);
extern reiserfs_node_t *reiserfs_node_create(aal_block_t *block);
extern int reiserfs_node_check(reiserfs_node_t *node, int flags);
extern void reiserfs_node_close(reiserfs_node_t *node, int sync);

#endif

