/*
 *  journal.c -- reiserfs filesystem journal common code.
 *  Copyright (C) 1996-2002 Hans Reiser.
 *  Author Vitalt Fertman
 */  


#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

reiserfs_node_t *reiserfs_node_open (aal_block_t *block) {
    return 0;
}

reiserfs_node_t *reiserfs_node_create (aal_block_t *block) {
    return 0;
}

void reiserfs_node_close (reiserfs_node_t *node, int sync) {
    
}

int reiserfs_node_check (reiserfs_node_t *node, int flags) {
    return 0;
}

