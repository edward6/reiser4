/*
    key.h -- reiser4 key defines and functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

extern errno_t reiser4_key_init(reiser4_key_t *key, 
    reiser4_plugin_t *plugin, const void *data);

extern reiser4_plugin_t *reiser4_key_guess(const void *data);

extern int reiser4_key_compare(reiser4_key_t *key1, 
    reiser4_key_t *key2);

extern void reiser4_key_clean(reiser4_key_t *key);

extern errno_t reiser4_key_build_generic(reiser4_key_t *key, 
    uint32_t type, roid_t locality, roid_t objectid, uint64_t offset);

extern errno_t reiser4_key_build_objid(reiser4_key_t *key, 
    uint32_t type, roid_t locality, roid_t objectid);

extern errno_t reiser4_key_build_direntry(reiser4_key_t *key, 
    reiser4_plugin_t *hash_plugin, roid_t locality, 
    roid_t objectid, const char *name);

extern errno_t reiser4_key_build_entryid(reiser4_key_t *key, 
    reiser4_plugin_t *hash_plugin, const char *name);

extern errno_t reiser4_key_build_by_entry(reiser4_key_t *key,
    void *data);

extern errno_t reiser4_key_set_type(reiser4_key_t *key, 
    uint32_t type);

extern errno_t reiser4_key_set_offset(reiser4_key_t *key, 
    uint64_t offset);

extern errno_t reiser4_key_set_hash(reiser4_key_t *key, 
    uint64_t hash);

extern errno_t reiser4_key_set_objectid(reiser4_key_t *key, 
    roid_t objectid);

extern errno_t reiser4_key_set_locality(reiser4_key_t *key, 
    roid_t locality);

extern uint32_t reiser4_key_get_type(reiser4_key_t *key);
extern uint64_t reiser4_key_get_offset(reiser4_key_t *key);
extern uint64_t reiser4_key_get_hash(reiser4_key_t *key);

extern roid_t reiser4_key_get_objectid(reiser4_key_t *key);
extern roid_t reiser4_key_get_locality(reiser4_key_t *key);
extern void reiser4_key_maximal(reiser4_key_t *key);
extern void reiser4_key_minimal(reiser4_key_t *key);

extern errno_t reiser4_key_print(reiser4_key_t *key, char *buff, 
    uint32_t n, uint16_t options);

#endif

