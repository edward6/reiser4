/*
    tree.h -- reiserfs balanced tree functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef TREE_H
#define TREE_H

#include <reiserfs/plugin.h>
#include <reiserfs/filesystem.h>
#include <reiserfs/plugin.h>

#define REISERFS_LEAF_LEVEL 1
#define REISERFS_RESERVED_IDS 40

extern error_t reiserfs_tree_open(reiserfs_fs_t *fs);

extern error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_id_t node_plugin_id);

extern error_t reiserfs_tree_sync(reiserfs_fs_t *fs);
extern void reiserfs_tree_close(reiserfs_fs_t *fs);

#endif

