/*
    tree.h -- reiser4 balanced tree functions.
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

#define REISER4_LEAF_LEVEL 1

extern reiser4_tree_t *reiser4_tree_open(reiser4_fs_t *fs);
extern void reiser4_tree_close(reiser4_tree_t *tree);

#ifndef ENABLE_COMPACT

extern reiser4_tree_t *reiser4_tree_create(reiser4_fs_t *fs, 
    reiser4_profile_t *profile);

extern errno_t reiser4_tree_sync(reiser4_tree_t *tree);
extern errno_t reiser4_tree_flush(reiser4_tree_t *tree);

extern errno_t reiser4_tree_insert(reiser4_tree_t *tree, 
    reiser4_item_hint_t *item, reiser4_coord_t *coord);

extern errno_t reiser4_tree_remove(reiser4_tree_t *tree, 
    reiser4_key_t *key);

extern errno_t reiser4_tree_move(reiser4_tree_t *tree,
    reiser4_coord_t *dst, reiser4_coord_t *src);

extern errno_t reiser4_tree_mkspace(reiser4_tree_t *tree, 
    reiser4_coord_t *old, reiser4_coord_t *new, 
    uint32_t needed);

extern errno_t reiser4_tree_lshift(reiser4_tree_t *tree, 
    reiser4_coord_t *old, reiser4_coord_t *new, 
    uint32_t needed, int allocate);

extern errno_t reiser4_tree_rshift(reiser4_tree_t *tree, 
    reiser4_coord_t *old, reiser4_coord_t *new, 
    uint32_t needed, int allocate);

#endif

extern int reiser4_tree_lookup(reiser4_tree_t *tree, 
    uint8_t level, reiser4_key_t *key, reiser4_coord_t *coord);

extern reiser4_cache_t *reiser4_tree_root(reiser4_tree_t *tree);

#endif

