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

extern reiserfs_tree_t *reiserfs_tree_open(reiserfs_fs_t *fs);
extern void reiserfs_tree_close(reiserfs_tree_t *tree);

#ifndef ENABLE_COMPACT

extern reiserfs_tree_t *reiserfs_tree_create(reiserfs_fs_t *fs, 
    reiserfs_profile_t *profile);

extern errno_t reiserfs_tree_sync(reiserfs_tree_t *tree);
extern errno_t reiserfs_tree_flush(reiserfs_tree_t *tree);

extern errno_t reiserfs_tree_insert(reiserfs_tree_t *tree, 
    reiserfs_item_hint_t *item);

extern errno_t reiserfs_tree_remove(reiserfs_tree_t *tree, 
    reiserfs_coord_t *coord);

extern errno_t reiserfs_tree_move(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src);

extern errno_t reiserfs_tree_copy(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src);

extern errno_t reiserfs_tree_move(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src);

extern errno_t reiserfs_tree_copy(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src);

extern errno_t reiserfs_tree_shift(reiserfs_coord_t *old, reiserfs_coord_t *new, 
    uint32_t needed);

#endif

extern reiserfs_cache_t *reiserfs_tree_lneighbour(reiserfs_cache_t *cache);

extern reiserfs_cache_t *reiserfs_tree_rneighbour(reiserfs_cache_t *cache);

extern int reiserfs_tree_lookup(reiserfs_tree_t *tree, 
    uint8_t stop, reiserfs_key_t *key, reiserfs_coord_t *coord);

extern reiserfs_cache_t *reiserfs_tree_root(reiserfs_tree_t *tree);

#endif

