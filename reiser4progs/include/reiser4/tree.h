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

extern error_t reiserfs_tree_open(reiserfs_fs_t *fs);
extern void reiserfs_tree_close(reiserfs_fs_t *fs);

#ifndef ENABLE_COMPACT

extern error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile);

extern error_t reiserfs_tree_sync(reiserfs_fs_t *fs);
extern error_t reiserfs_tree_flush(reiserfs_fs_t *fs);

extern error_t reiserfs_tree_item_insert(reiserfs_fs_t *fs, void *key, 
    reiserfs_item_info_t *item_info);

extern error_t reiserfs_tree_item_remove(reiserfs_fs_t *fs, void *key);

extern error_t reiserfs_tree_node_insert(reiserfs_fs_t *fs, 
    reiserfs_node_t *node);

extern error_t reiserfs_tree_node_remove(reiserfs_fs_t *fs, void *key);

#endif

extern int reiserfs_tree_lookup(reiserfs_fs_t *fs, 
    void *key, reiserfs_coord_t *coord);

#endif

