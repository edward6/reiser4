/*
    key.h -- reiser4 key defines and functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>

extern errno_t reiser4_key_init(reiser4_key_t *key, 
    reiser4_plugin_t *plugin, const void *data);

extern reiser4_plugin_t *reiser4_key_guess(const void *data);

extern int reiser4_key_compare(reiser4_key_t *key1, 
    reiser4_key_t *key2);

extern void reiser4_key_clean(reiser4_key_t *key);

extern errno_t reiser4_key_build_generic(reiser4_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset);

extern errno_t reiser4_key_build_objid(reiser4_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid);

extern errno_t reiser4_key_build_direntry(reiser4_key_t *key, 
    reiser4_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name);

extern errno_t reiser4_key_build_entryid(reiser4_key_t *key, 
    reiser4_plugin_t *hash_plugin, const char *name);

extern errno_t reiser4_key_build_by_entry(reiser4_key_t *key,
    void *data);

extern errno_t reiser4_key_set_type(reiser4_key_t *key, uint32_t type);
extern errno_t reiser4_key_set_offset(reiser4_key_t *key, uint64_t offset);
extern errno_t reiser4_key_set_hash(reiser4_key_t *key, uint64_t hash);

extern errno_t reiser4_key_set_objectid(reiser4_key_t *key, oid_t objectid);
extern errno_t reiser4_key_set_locality(reiser4_key_t *key, oid_t locality);

extern uint32_t reiser4_key_get_type(reiser4_key_t *key);
extern uint64_t reiser4_key_get_offset(reiser4_key_t *key);
extern uint64_t reiser4_key_get_hash(reiser4_key_t *key);

extern oid_t reiser4_key_get_objectid(reiser4_key_t *key);
extern oid_t reiser4_key_get_locality(reiser4_key_t *key);

#endif

