/*
    repair/node.h -- reiserfs node recovery structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef REPAIR_NODE_H
#define REPAIR_NODE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <repair/repair.h>
#include <reiser4/filesystem.h>

extern errno_t repair_node_check(reiser4_node_t *node, void *data);
extern errno_t repair_node_update_internal(reiser4_node_t *node, uint32_t pos, 
    void *data);

#endif

