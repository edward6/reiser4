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

static const reiserfs_key40_t *reiserfs_key40_minimal(void) {
    return &MINIMAL_KEY;
}

static const reiserfs_key40_t *reiserfs_key40_maximal(void) {
    return &MAXIMAL_KEY;
}

static int reiserfs_key40_compare(reiserfs_key40_t *key1, reiserfs_key40_t *key2) {
    int result;

    if ((result = KEY40_COMP_ELEMENT(key1, key2, 0)) != 0)
	return result;

    if ((result = KEY40_COMP_ELEMENT(key1, key2, 1)) != 0)
	return result;
    
    return KEY40_COMP_ELEMENT(key1, key2, 2);
}

static int reiserfs_key40_confirm(reiserfs_key40_t *key) {
    return 1;
}

static reiserfs_key40_t *reiserfs_key40_create(uint16_t type, oid_t locality, 
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

static void reiserfs_key40_fini(reiserfs_key40_t *key) {
    aal_assert("umka-630", key != NULL, return);
    aal_free(key);
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
	.confirm = (int (*)(void *))reiserfs_key40_confirm,
	.minimal = (const void *(*)(void))reiserfs_key40_minimal,
	.maximal = (const void *(*)(void))reiserfs_key40_maximal,
	.compare = (int (*)(const void *, const void *))reiserfs_key40_compare,
	.create = (void *(*)(uint16_t, oid_t, oid_t, uint64_t))reiserfs_key40_create,
	.fini = (void (*)(void *))reiserfs_key40_fini
    }
};

reiserfs_plugin_t *reiserfs_key40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &key40_plugin;
}

libreiserfs_plugins_register(reiserfs_key40_entry);


