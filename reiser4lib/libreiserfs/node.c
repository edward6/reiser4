/*
 *  journal.c -- reiserfs filesystem journal common code.
 *  Copyright (C) 1996-2002 Hans Reiser.
 *  Author Vitalt Fertman
 */  


#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

reiserfs_node_t *reiserfs_node_open (aal_block_t *block) {
    return NULL;
}

reiserfs_node_t *reiserfs_node_create (aal_block_t *block, uint32_t level) {
    return NULL;
}

void reiserfs_node_close (reiserfs_node_t *node, int sync) {
    return;    
}

int reiserfs_node_check (reiserfs_node_t *node, int flags) {
    return 0;
}

uint32_t reiserfs_max_item_size (void) {
    return 0;
}

uint32_t reiserfs_max_item_num  (void) {
    return 0;
}

uint32_t reiserfs_count (const reiserfs_node_opaque_t *) {
    return 0;
}

uint32_t reiserfs_level (const reiserfs_node_t *node) {
    reiserfs_plugin_check_routine (node->plugin->node, level, return 0);
    return node->plugin->node.level (node->entity);    
}

uint32_t reiserfs_get_free_space (const reiserfs_node_opaque_t *) {
    return 0;
}

void reiserfs_set_free_space (const reiserfs_node_opaque_t *) {
    return;
}
     
void (*print) (const reiserfs_node_opaque_t * node) {
    return;
}

