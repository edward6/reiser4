/*
    r5_hash.c -- r5 hash.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/plugin.h>

uint64_t r5_hash_build(const unsigned char *name, uint32_t len) {
    uint32_t i;
    uint64_t a = 0;
	
    for (i = 0; i < len; i++) {
	a += name[i] << 4;
	a += name[i] >> 4;
	a *= 11;
    }
    
    return a;
}

static reiserfs_plugin_t r5_hash_plugin = {
    .hash_ops = {
	.h = {
	    .handle = NULL,
	    .id = HASH_R5_ID,
	    .type = HASH_PLUGIN_TYPE,
	    .label = "r5_hash",
	    .desc = "r5 hash plugin for reiser4, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.build = (uint64_t (*)(const unsigned char *, uint32_t))r5_hash_build
    }
};

static reiserfs_plugin_t *r5_hash_entry(reiserfs_core_t *c) {
    return &r5_hash_plugin;
}

libreiser4_factory_register(r5_hash_entry);

