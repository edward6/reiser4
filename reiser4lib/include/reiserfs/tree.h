/*
    tree.h -- reiserfs balanced tree functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef TREE_H
#define TREE_H

#include <reiserfs/reiserfs.h>

#define REISERFS_ROOT_LEVEL 2

extern int reiserfs_tree_open(reiserfs_fs_t *fs);
extern int reiserfs_tree_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t node_plugin_id);
extern void reiserfs_tree_close(reiserfs_fs_t *fs, int sync);

#endif

