/*
    tree.h -- reiserfs balanced tree functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef TREE_H
#define TREE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/plugin.h>
#include <reiser4/filesystem.h>

#define REISERFS_LEAF_LEVEL 1

extern reiserfs_tree_t *reiserfs_tree_open(aal_device_t *device, 
    reiserfs_alloc_t *alloc, blk_t root_blk, reiserfs_key_t *root_key);

extern void reiserfs_tree_close(reiserfs_tree_t *tree);

#ifndef ENABLE_COMPACT

extern reiserfs_tree_t *reiserfs_tree_create(aal_device_t *device, 
    reiserfs_alloc_t *alloc, reiserfs_key_t *root_key, 
    reiserfs_profile_t *profile);

extern errno_t reiserfs_tree_sync(reiserfs_tree_t *tree);
extern errno_t reiserfs_tree_flush(reiserfs_tree_t *tree);

extern errno_t reiserfs_tree_item_insert(reiserfs_tree_t *tree, 
    reiserfs_key_t *key, reiserfs_item_hint_t *hint);

extern errno_t reiserfs_tree_item_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key);

extern errno_t reiserfs_tree_node_insert(reiserfs_tree_t *tree, 
    reiserfs_node_t *node);

extern errno_t reiserfs_tree_node_remove(reiserfs_tree_t *tree, 
    reiserfs_key_t *key);

#endif

extern int reiserfs_tree_lookup(reiserfs_tree_t *tree, 
    reiserfs_key_t *key, reiserfs_coord_t *coord);

extern reiserfs_node_t *reiserfs_tree_root_node(reiserfs_tree_t *tree);

#endif

