/*
    tree.h -- reiserfs balanced tree functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef TREE_H
#define TREE_H

#include <reiser4/plugin.h>
#include <reiser4/filesystem.h>

#define REISERFS_LEAF_LEVEL 1
#define REISERFS_ROOT_LEVEL 0

extern error_t reiserfs_tree_init(reiserfs_fs_t *fs);
extern void reiserfs_tree_fini(reiserfs_fs_t *fs);

extern error_t reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile);

extern error_t reiserfs_tree_sync(reiserfs_fs_t *fs);
extern error_t reiserfs_tree_flush(reiserfs_fs_t *fs);

#endif

