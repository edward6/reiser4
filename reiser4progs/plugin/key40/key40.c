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

static reiserfs_key40_t *key40_create(uint16_t type, oid_t locality, 
    oid_t objectid, uint64_t offset) 
{
    reiserfs_key40_t *key;
    
    if (!(key = aal_calloc(sizeof(*key), 0)))
	return NULL;
    
    set_key40_locality(key, locality);
    set_key40_type(key, (reiserfs_key40_minor_t)type);
    set_key40_objectid(key, objectid);

    if (type == KEY40_FILE_NAME_MINOR)
	set_key40_hash(key, offset);
    else
	set_key40_offset(key, offset);
    
    return key;
}

static void key40_close(reiserfs_key40_t *key) {
    aal_free(key);
}

static void key40_set_type(reiserfs_key40_t *key, uint16_t type) {
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

    if (get_key40_type(key) == KEY40_FILE_NAME_MINOR)
	set_key40_hash(key, offset);
    else
	set_key40_offset(key, offset);
}

static uint64_t key40_get_offset(reiserfs_key40_t *key) {
    aal_assert("umka-641", key != NULL, return 0);

    if (get_key40_type(key) == KEY40_FILE_NAME_MINOR)
	return get_key40_hash(key);

    return get_key40_offset(key);
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
	.confirm = (int (*)(void *))key40_confirm,
	.minimal = (const void *(*)(void))key40_minimal,
	.maximal = (const void *(*)(void))key40_maximal,
	.compare = (int (*)(const void *, const void *))key40_compare,
	.create = (void *(*)(uint16_t, oid_t, oid_t, uint64_t))key40_create,
	.close = (void (*)(void *))key40_close,

	.set_type = (void (*)(void *, uint16_t))key40_set_type,
	.get_type = (uint16_t (*)(void *))key40_get_type,

	.set_locality = (void (*)(void *, oid_t))key40_set_locality,
	.get_locality = (oid_t (*)(void *))key40_get_locality,

	.set_objectid = (void (*)(void *, oid_t))key40_set_objectid,
	.get_objectid = (oid_t (*)(void *))key40_get_objectid,

	.set_offset = (void (*)(void *, uint64_t))key40_set_offset,
	.get_offset = (uint64_t (*)(void *))key40_get_offset
    }
};

static reiserfs_plugin_t *key40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &key40_plugin;
}

libreiser4_plugins_register(key40_entry);

