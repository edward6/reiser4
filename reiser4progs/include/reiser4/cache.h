/*
    cache.h -- functions which work with node cache.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets. 
*/

#ifndef CACHE_H
#define CACHE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/filesystem.h>

extern reiserfs_cache_t *reiserfs_cache_create(reiserfs_node_t *node);
extern void reiserfs_cache_close(reiserfs_cache_t *cache);

extern errno_t reiserfs_cache_pos(reiserfs_cache_t *cache, 
    reiserfs_pos_t *pos);

extern reiserfs_cache_t *reiserfs_cache_find(reiserfs_cache_t *cache, 
    reiserfs_key_t *key);

extern errno_t reiserfs_cache_register(reiserfs_cache_t *cache, 
    reiserfs_cache_t *child);

extern void reiserfs_cache_unregister(reiserfs_cache_t *cache, 
    reiserfs_cache_t *child);

#ifndef ENABLE_COMPACT

extern errno_t reiserfs_cache_sync(reiserfs_cache_t *cache);

#endif

extern errno_t reiserfs_cache_lnkey(reiserfs_cache_t *cache, 
    reiserfs_key_t *key);

extern errno_t reiserfs_cache_rnkey(reiserfs_cache_t *cache, 
    reiserfs_key_t *key);

extern errno_t reiserfs_cache_raise(reiserfs_cache_t *cache);

#endif

