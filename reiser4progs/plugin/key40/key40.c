/*
    key40.c -- reiser4 default key plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include "key40.h"

static reiserfs_core_t *core = NULL;

static const reiserfs_key40_t MINIMAL_KEY = {
    .el = { 0ull, 0ull, 0ull }
};

static const reiserfs_key40_t MAXIMAL_KEY = {
    .el = { ~0ull, ~0ull, ~0ull }
};

static uint64_t key40_pack_string(const char *name, int start) {
    unsigned i;
    uint64_t str;

    aal_assert("vpf-134", name != NULL, return 0);
    
    str = 0;
    for (i = 0; (i < sizeof(str) - start) && name[i]; ++i) {
        str <<= 8;
        str |= (unsigned char)name[i];
    }
    str <<= (sizeof(str) - i - start) << 3;
    return str;
}

static const reiserfs_key40_t *key40_minimal(void) {
    return &MINIMAL_KEY;
}

static const reiserfs_key40_t *key40_maximal(void) {
    return &MAXIMAL_KEY;
}

static int key40_compare_short(reiserfs_key40_t *key1, reiserfs_key40_t *key2) {
    int result;

    aal_assert("vpf-135", key1 != NULL, return -2);
    aal_assert("vpf-136", key2 != NULL, return -2);
    
    if ((result = KEY40_COMP_ELEMENT(key1, key2, 0)) != 0)
	return result;

    return KEY40_COMP_ELEMENT(key1, key2, 1);
}

static int key40_compare_full(reiserfs_key40_t *key1, reiserfs_key40_t *key2) {
    int result;

    aal_assert("vpf-135", key1 != NULL, return -2);
    aal_assert("vpf-136", key2 != NULL, return -2);
    
    if ((result = key40_compare_short(key1, key2)) != 0)
	return result;

    return KEY40_COMP_ELEMENT(key1, key2, 2);
}

static int key40_confirm(reiserfs_key40_t *key) {
    aal_assert("vpf-137", key != NULL, return -1);
    return 1;
}

static errno_t key40_check(reiserfs_key40_t *key, int flags) {
    aal_assert("vpf-137", key != NULL, return -1);
    return -1;
}

static void key40_set_type(reiserfs_key40_t *key, uint32_t type) {
    aal_assert("umka-634", key != NULL, return);
    set_key40_type(key, (reiserfs_key40_minor_t)type);
}

static uint16_t key40_get_type(reiserfs_key40_t *key) {
    aal_assert("umka-635", key != NULL, return 0);
    return (uint16_t)get_key40_type(key);
}

static void key40_set_locality(reiserfs_key40_t *key, oid_t locality) {
    aal_assert("umka-636", key != NULL, return);
    set_key40_locality(key, (uint64_t)locality);
}

static oid_t key40_get_locality(reiserfs_key40_t *key) {
    aal_assert("umka-637", key != NULL, return 0);
    return (oid_t)get_key40_locality(key);
}
    
static void key40_set_objectid(reiserfs_key40_t *key, oid_t objectid) {
    aal_assert("umka-638", key != NULL, return);
    set_key40_objectid(key, (uint64_t)objectid);
}

static oid_t key40_get_objectid(reiserfs_key40_t *key) {
    aal_assert("umka-639", key != NULL, return 0);
    return (oid_t)get_key40_objectid(key);
}

static void key40_set_offset(reiserfs_key40_t *key, uint64_t offset) {
    aal_assert("umka-640", key != NULL, return);
    set_key40_offset(key, offset);
}

static uint64_t key40_get_offset(reiserfs_key40_t *key) {
    aal_assert("umka-641", key != NULL, return 0);
    return get_key40_offset(key);
}

static void key40_set_hash(reiserfs_key40_t *key, uint64_t hash) {
    aal_assert("vpf-129", key != NULL, return);
    set_key40_hash(key, hash);
}

static uint64_t key40_get_hash(reiserfs_key40_t *key) {
    aal_assert("vpf-130", key != NULL, return 0);
    return get_key40_hash(key);
}

static void key40_set_counter(reiserfs_key40_t *key, uint8_t counter) {
}

static uint64_t key40_get_counter(reiserfs_key40_t *key) {
    return 0;
}

static uint8_t key40_size (void) {
    return sizeof(reiserfs_key40_t);
}

static void key40_clean(reiserfs_key40_t *key) {
    aal_assert("vpf-139", key != NULL, return);
    aal_memset(key, 0, key40_size());
}

static errno_t key40_build_hash(reiserfs_key40_t *key,
    reiserfs_plugin_t *hash_plugin, const char *name) 
{
    uint16_t len;
    
    aal_assert("vpf-101", key != NULL, return -1);
    aal_assert("vpf-102", name != NULL, return -1);
    aal_assert("vpf-128", hash_plugin != NULL, return -1); 
    
    len = aal_strlen(name);
    if (len != 1 || aal_strncmp(name, ".", 1)) {
	/* 
	    Not dot, pack the first part of the name into 
	    objectid.
	*/
	key40_set_objectid(key, key40_pack_string(name, 1));
	if (len <= OID_CHARS + sizeof(uint64_t)) {
	    if (len > OID_CHARS)
		/* 
		    Does not fit into objectid, pack the second part of 
		    the name into offset. 
		*/
		key40_set_offset(key, key40_pack_string(name + 7, 0));
	} else {
	    /* Note in the key that it is hash, not a name */
	    key->el[1] |= 0x0100000000000000ull;
		
 	    key40_set_hash(key, hash_plugin->hash_ops.build((const unsigned char *)name, 
		aal_strlen(name)));

	    key40_set_counter(key, 0);
	}
    }

    return 0;
}

static errno_t key40_build_entry_full(reiserfs_key40_t *key, 
    reiserfs_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name) 
{
    aal_assert("vpf-140", key != NULL, return -1);
    aal_assert("umka-667", name != NULL, return -1);
    
    key40_clean(key);

    set_key40_locality(key, objectid);
    set_key40_type(key, KEY40_FILENAME_MINOR);
    
    key40_build_hash(key, hash_plugin, name);

    return 0;
}

static errno_t key40_build_entry_short(void *ptr, 
    reiserfs_plugin_t *hash_plugin, const char *name) 
{
    reiserfs_key40_t key;    
    
    aal_assert("vpf-142", ptr != NULL, return -1);
    
    key40_clean(&key);
    key40_build_hash(&key, hash_plugin, name);
    
    aal_memset(ptr, 0, 2 * sizeof(uint64_t));
    aal_memcpy(ptr, &key.el[1], 2 * sizeof(uint64_t));

    return 0;
}

static errno_t key40_build_generic_full(reiserfs_key40_t *key, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset) 
{
    aal_assert("vpf-141", key != NULL, return -1);

    key40_clean(key);

    set_key40_locality(key, locality);
    set_key40_type(key, (reiserfs_key40_minor_t)type);
    set_key40_objectid(key, objectid);
    set_key40_offset(key, offset);

    return 0;
}

static errno_t key40_build_generic_short(void *ptr, uint32_t type, 
    oid_t locality, oid_t objectid)
{
    reiserfs_key40_t key;
    
    aal_assert("vpf-143", ptr != NULL, return -1);
    
    key40_clean(&key);

    set_key40_locality(&key, locality);
    set_key40_type(&key, (reiserfs_key40_minor_t)type);
    set_key40_objectid(&key, objectid);
    
    aal_memset(ptr, 0, 2 * sizeof(uint64_t));
    aal_memcpy(ptr, &key, 2 * sizeof(uint64_t));

    return 0;
}

static errno_t key40_build_by_entry(reiserfs_key40_t *key, 
    void *data)
{
    aal_assert("umka-877", key != NULL, return -1);
    aal_assert("umka-878", data != NULL, return -1);
    
    key40_clean(key);    
    aal_memcpy(&key->el[1], data, sizeof(uint64_t) * 2);
    
    return 0;
}

static reiserfs_plugin_t key40_plugin = {
    .key_ops = {
	.h = {
	    .handle = NULL,
	    .id = KEY_REISER40_ID,
	    .type = KEY_PLUGIN_TYPE,
	    .label = "key40",
	    .desc = "Key for reiserfs 4.0, ver. " VERSION,
	},
	.confirm = (int (*)(const void *))key40_confirm,
	.check = (errno_t (*)(const void *, int))key40_check,
	.minimal = (const void *(*)(void))key40_minimal,
	.maximal = (const void *(*)(void))key40_maximal,
	.clean = (void (*)(const void *))key40_clean,
	.size = (uint8_t (*)(void))key40_size,

	.set_type = (void (*)(const void *, uint32_t))key40_set_type,
	.get_type = (uint32_t (*)(const void *))key40_get_type,

	.set_locality = (void (*)(const void *, oid_t))key40_set_locality,
	.get_locality = (oid_t (*)(const void *))key40_get_locality,

	.set_objectid = (void (*)(const void *, oid_t))key40_set_objectid,
	.get_objectid = (oid_t (*)(const void *))key40_get_objectid,

	.set_offset = (void (*)(const void *, uint64_t))key40_set_offset,
	.get_offset = (uint64_t (*)(const void *))key40_get_offset,

	.set_hash = (void (*)(const void *, uint64_t))key40_set_hash,
	.get_hash = (uint64_t (*)(const void *))key40_get_hash,
	
	.set_counter = (void (*)(const void *, uint8_t))key40_set_counter,
	.get_counter = (uint8_t (*)(const void *))key40_get_counter,
	
	.compare_full = (int (*)(const void *, const void *))key40_compare_full,
	.compare_short = (int (*)(const void *, const void *))key40_compare_short,
	
	.build_generic_full = (errno_t (*)(const void *, uint32_t, oid_t, oid_t, uint64_t))
	    key40_build_generic_full,
	
	.build_entry_full = (errno_t (*)(const void *, void *, oid_t, oid_t, const char *))
	    key40_build_entry_full,

	.build_generic_short = (errno_t (*)(const void *, uint32_t, oid_t, oid_t))
	    key40_build_generic_short,

	.build_entry_short = (errno_t (*)(const void *, void *, const char *))
	    key40_build_entry_short,

	.build_by_entry = (errno_t (*)(const void *, void *))
	    key40_build_by_entry
    }
};

static reiserfs_plugin_t *key40_entry(reiserfs_core_t *c) {
    core = c;
    return &key40_plugin;
}

libreiser4_factory_register(key40_entry);

