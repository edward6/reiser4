/*
 * journal.h -- reiserfs node functions.
 * Copyright (C) 1996-2002 Hans Reiser.
 * Author Vitaly Fertman.
 */ 

#ifndef NODE_H
#define NODE_H

#include <aal/aal.h>

extern int reiserfs_node_open (aal_block_t *block);
extern int reiserfs_node_create (aal_block_t *block);
extern void reiserfs_node_close (reiserfs_node_t *node, int sync);
extern int reiserfs_node_check (reiserfs_node_t *node, int flags);

#endif

