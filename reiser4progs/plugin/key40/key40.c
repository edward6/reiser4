/*
    key40.c -- reiser4 default key plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include "key40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static const reiserfs_key40_t MINIMAL_KEY = {
    .el = { 0ull, 0ull, 0ull }
};

static const reiserfs_key40_t MAXIMAL_KEY = {
    .el = { ~0ull, ~0ull, ~0ull }
};

static uint64_t key40_pack_string(const char *name, int start) {
    unsigned i;
    uint64_t str;

    str = 0;
    for (i = 0 ; (i < sizeof(str) - start) && name[i] ; ++ i) {
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

static int key40_compare(reiserfs_key40_t *key1, reiserfs_key40_t *key2) {
    int result;

    if ((result = KEY40_COMP_ELEMENT(key1, key2, 0)) != 0)
	return result;

    if ((result = KEY40_COMP_ELEMENT(key1, key2, 1)) != 0)
	return result;
    
    return KEY40_COMP_ELEMENT(key1, key2, 2);
}

static int key40_confirm(reiserfs_key40_t *key) {
    return 1;
}

/* 
    Useful for temp key creation, when all fields are built already. 
    (Reason to take just one parameter - *key). When want to just 
    build the key, use key40_build_file/dir_key depending on whether 
    you need file or dir key.
*/
static reiserfs_key40_t *key40_create(uint32_t type, oid_t locality, 
    oid_t objectid, uint64_t offset) 
{
    reiserfs_key40_t *key;
    
    if (!(key = aal_calloc(sizeof(*key), 0)))
	return NULL;
    
    set_key40_locality(key, locality);
    set_key40_type(key, (key40_minor_t)type);
    set_key40_objectid(key, objectid);
    set_key40_offset(key, offset);
    
    return key;
}

static void key40_close(reiserfs_key40_t *key) {
    aal_free(key);
}

static void key40_set_type(reiserfs_key40_t *key, uint32_t type) {
    aal_assert("umka-634", key != NULL, return);
    set_key40_type(key, (key40_minor_t)type);
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
/* FIXME-VITALY: When counter macros will be ready.    
    aal_assert("vpf-129", key != NULL, return);
    set_key40_counter(key, counter);
*/
}

static uint64_t key40_get_counter(reiserfs_key40_t *key) {
/* FIXME-VITALY: When counter macros will be ready.
    aal_assert("vpf-130", key != NULL, return);
    return get_key40_counter(key);
*/
    return 0;
}

static uint8_t key40_size (void) {
    return sizeof(reiserfs_key40_t);
}

static void key40_clean(reiserfs_key40_t *key) {
    aal_memset(key, 0, key40_size());
}


static error_t key40_build_hash(reiserfs_key40_t *key, char *name, 
    reiserfs_plugin_t *hash_plugin) 
{
    uint16_t len;
    
    aal_assert("vpf-101", key != NULL, return -1);
    aal_assert("vpf-102", name != NULL, return -1);
    /* When ready
    aal_assert("vpf-128", hash_plugin != NULL, return -1); 
    */
    
    len = aal_strlen(name);
    if (len != 1 || aal_strncmp(name, ".", 1)) {
	/* 
	    Not dot, pack the first part of the name into 
	    objectid. 
	*/
	key40_set_objectid(key, key40_pack_string(name, 1));
	if (len <= OID_CHARS + sizeof(uint64_t)) {
	    /* Fits into objectid + hash. */
	    if (len > OID_CHARS)
		/* 
		    Does not fit into objectid, pack the second part of 
		    the name into offset. 
		*/
		key40_set_offset(key, key40_pack_string(name, 0));
	} else {
	    /* Note in the key that it is hash, not a name */
	    key->el[1] |= 0x0100000000000000ull;
		
/*	    When hash plugin is ready
 	    key40_set_hash(key, hash_plugin->hash.hash(name)); */
	    key40_set_counter(key, 0);
	}
    }

    return 0;
}

static void key40_build_file_key(reiserfs_key40_t *key, uint32_t type, 
    oid_t locality, oid_t objectid, uint64_t offset) 
{
    aal_assert("vpf-127", type != KEY40_FILE_NAME_MINOR, return);

    key40_clean(key);
    set_key40_locality(key, locality);
    set_key40_type(key, (key40_minor_t)type);
    set_key40_objectid(key, objectid);
    set_key40_offset(key, offset);
}

static void key40_build_dir_key(reiserfs_key40_t *key, oid_t locality, 
    oid_t objectid, char *name, reiserfs_plugin_t *hash_plugin) 
{
    key40_clean(key);
    set_key40_locality(key, objectid);
    set_key40_type(key, KEY40_FILE_NAME_MINOR);
    
    key40_build_hash(key, name, hash_plugin);
}

static reiserfs_plugin_t key40_plugin = {
    .key = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_KEY_PLUGIN,
	    .label = "key40",
	    .desc = "Reiser4 default key, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.confirm = (int (*)(const void *))key40_confirm,
	.minimal = (const void *(*)(void))key40_minimal,
	.maximal = (const void *(*)(void))key40_maximal,
	.compare = (int (*)(const void *, const void *))key40_compare,
	.create = (void *(*)(uint16_t, oid_t, oid_t, uint64_t))key40_create,
	.clean = (void (*)(void *))key40_clean,
	.close = (void (*)(void *))key40_close,

	.set_type = (void (*)(void *, uint32_t))key40_set_type,
	.get_type = (uint32_t (*)(const void *))key40_get_type,

	.set_locality = (void (*)(void *, oid_t))key40_set_locality,
	.get_locality = (oid_t (*)(const void *))key40_get_locality,

	.set_objectid = (void (*)(void *, oid_t))key40_set_objectid,
	.get_objectid = (oid_t (*)(const void *))key40_get_objectid,

	.set_offset = (void (*)(void *, uint64_t))key40_set_offset,
	.get_offset = (uint64_t (*)(const void *))key40_get_offset,

	.set_hash = (void (*)(void *, uint64_t))key40_set_hash,
	.get_hash = (uint64_t (*)(const void *))key40_get_hash,
	
	.set_counter = (void (*)(void *, uint8_t))key40_set_counter,
	.get_counter = (uint8_t (*)(const void *))key40_get_counter,
	
	.build_file_key = (void (*)(void *, uint32_t, oid_t, oid_t, uint64_t))
	    key40_build_file_key,
	
	.build_dir_key = (void (*)(void *, oid_t, oid_t, char *, void *))
	    key40_build_dir_key,

	.size = (uint8_t (*)(void))key40_size
    }
};

static reiserfs_plugin_t *key40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &key40_plugin;
}

libreiser4_plugins_register(key40_entry);

