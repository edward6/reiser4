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

extern reiser4_cache_t *reiser4_cache_create(reiser4_node_t *node);
extern void reiser4_cache_close(reiser4_cache_t *cache);

extern errno_t reiser4_cache_pos(reiser4_cache_t *cache, 
    reiser4_pos_t *pos);

extern reiser4_cache_t *reiser4_cache_find(reiser4_cache_t *cache, 
    reiser4_key_t *key);

extern errno_t reiser4_cache_register(reiser4_cache_t *cache, 
    reiser4_cache_t *child);

extern void reiser4_cache_unregister(reiser4_cache_t *cache, 
    reiser4_cache_t *child);

#ifndef ENABLE_COMPACT

extern errno_t reiser4_cache_sync(reiser4_cache_t *cache);

extern errno_t reiser4_cache_insert(reiser4_cache_t *cache,
    reiser4_pos_t *pos, reiser4_item_hint_t *hint);

extern errno_t reiser4_cache_remove(reiser4_cache_t *cache,
    reiser4_pos_t *pos);

extern errno_t reiser4_cache_move(reiser4_cache_t *dst_cache,
    reiser4_pos_t *dst_pos, reiser4_cache_t *src_cache,
    reiser4_pos_t *src_pos);

extern errno_t reiser4_cache_set_key(reiser4_cache_t *cache, 
    reiser4_pos_t *pos, reiser4_key_t *key);

#endif

extern errno_t reiser4_cache_lnkey(reiser4_cache_t *cache, 
    reiser4_key_t *key);

extern errno_t reiser4_cache_rnkey(reiser4_cache_t *cache, 
    reiser4_key_t *key);

extern errno_t reiser4_cache_raise(reiser4_cache_t *cache);

#endif

