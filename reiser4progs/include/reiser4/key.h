/*
    key.h -- reiserfs key defines and functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>

/* 
    Maximal possible key size. It is used for creating temporary keys by declaring 
    array of uint8_t elements REISERFS_KEY_SIZE long.
*/
#define REISERFS_KEY_SIZE 24

struct reiserfs_key {
    reiserfs_plugin_t *plugin;
    uint8_t body[REISERFS_KEY_SIZE];
};

typedef struct reiserfs_key reiserfs_key_t;

typedef uint64_t oid_t;

/*
    FIXME-VITALY: We should change these names to plugin independent style. Pass 
    these names to key plugin where they will be converted to plugin-specific names.

    FIXME-UMKA: For awhile key40 minor is defined here. Not in key40 plugin. It will 
    be fixed when key format independent types will be produced here.
*/
typedef enum {
    /* File name key type */
    KEY40_FILENAME_MINOR = 0,
    /* Stat-data key type */
    KEY40_STATDATA_MINOR = 1,
    /* File attribute name */
    KEY40_ATTRNAME_MINOR = 2,
    /* File attribute value */
    KEY40_ATTRBODY_MINOR = 3,
    /* File body (tail or extent) */
    KEY40_BODY_MINOR	 = 4
} reiserfs_key40_minor_t;

extern errno_t reiserfs_key_init(reiserfs_key_t *key, 
    const void *data);

extern int reiserfs_key_compare(reiserfs_key_t *key1, 
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

