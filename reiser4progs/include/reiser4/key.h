/*
    key.h -- reiserfs key defines and functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>

extern errno_t reiserfs_key_init(reiserfs_key_t *key, 
    reiserfs_plugin_t *plugin, const void *data);

extern reiserfs_plugin_t *reiserfs_key_guess(const void *data);

extern int reiserfs_key_compare_full(reiserfs_key_t *key1, 
    reiserfs_key_t *key2);

extern int reiserfs_key_compare_short(reiserfs_key_t *key1, 
    reiserfs_key_t *key2);

extern void reiserfs_key_clean(reiserfs_key_t *key);

extern errno_t reiserfs_key_build_generic_full(reiserfs_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset);

extern errno_t reiserfs_key_build_generic_short(reiserfs_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid);

extern errno_t reiserfs_key_build_entry_full(reiserfs_key_t *key, 
    reiserfs_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name);

extern errno_t reiserfs_key_build_entry_short(reiserfs_key_t *key, 
    reiserfs_plugin_t *hash_plugin, const char *name);

extern errno_t reiserfs_key_set_type(reiserfs_key_t *key, uint32_t type);
extern errno_t reiserfs_key_set_offset(reiserfs_key_t *key, uint64_t offset);
extern errno_t reiserfs_key_set_hash(reiserfs_key_t *key, uint64_t hash);

extern errno_t reiserfs_key_set_objectid(reiserfs_key_t *key, oid_t objectid);
extern errno_t reiserfs_key_set_locality(reiserfs_key_t *key, oid_t locality);

extern uint32_t reiserfs_key_get_type(reiserfs_key_t *key);
extern uint64_t reiserfs_key_get_offset(reiserfs_key_t *key);
extern uint64_t reiserfs_key_get_hash(reiserfs_key_t *key);

extern oid_t reiserfs_key_get_objectid(reiserfs_key_t *key);
extern oid_t reiserfs_key_get_locality(reiserfs_key_t *key);

#endif

