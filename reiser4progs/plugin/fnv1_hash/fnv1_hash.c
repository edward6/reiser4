/*
    fnv1_hash.c -- fnv1 hash.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/plugin.h>

static uint64_t fnv1_hash_build(const unsigned char *name, uint32_t len) {
    uint32_t i;
    uint64_t a = 0xcbf29ce484222325ull;
    const uint64_t fnv_64_prime = 0x100000001b3ull;

    for(i = 0; i < len; i++) {
	a *= fnv_64_prime;
	a ^= (uint64_t)name[i];
    }
    return a;
}

static reiserfs_plugin_t fnv1_hash_plugin = {
    .hash_ops = {
	.h = {
	    .handle = NULL,
	    .id = HASH_FNV1_ID,
	    .type = HASH_PLUGIN_TYPE,
	    .label = "fnv1_hash",
	    .desc = "fnv1 hash plugin for reiser4, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.build = (uint64_t (*)(const unsigned char *, uint32_t))fnv1_hash_build
    }
};

static reiserfs_plugin_t *fnv1_hash_entry(reiserfs_core_t *c) {
    return &fnv1_hash_plugin;
}

libreiser4_factory_register(fnv1_hash_entry);

