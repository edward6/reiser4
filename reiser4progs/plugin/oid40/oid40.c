/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>

#include "oid40.h"


static reiserfs_plugins_factory_t *factory = NULL;

/* 
    Oid allocator plugin's init functions accepts block where oid area 
    lies and offset in this block. Oid area for oid40 plugin is just
    64-bit counter at offset known to format plugin, which usualy 
    initializing oid allocator plugin.

    As block oid area lies in may be super block in theory, "sync"
    function just updates own oid area and return control to caller.
*/
static reiserfs_opaque_t *reiserfs_oid40_open(aal_block_t *block, uint16_t offset) {
    reiserfs_oid40_t *oid;

    aal_assert("umka-512", block != NULL, return NULL);
    
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    oid->block = block;
    oid->offset = offset;
    oid->current = *((uint64_t *)(block->data + offset));
    
    return oid;
}

static reiserfs_opaque_t *reiserfs_oid40_create(aal_block_t *block, uint16_t offset) {
    reiserfs_oid40_t *oid;

    aal_assert("umka-511", block != NULL, return NULL);
    
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    oid->block = block;
    oid->offset = offset;
    oid->current = REISERFS_OID40_INITIAL;
    
    *((uint64_t *)(block->data + offset)) = oid->current;
    
    return oid;
}

static void reiserfs_oid40_close(reiserfs_oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

static uint64_t reiserfs_oid40_alloc(reiserfs_oid40_t *oid) {
    aal_assert("umka-513", oid != NULL, return 0);
    return ++oid->current;
}

static error_t reiserfs_oid40_sync(reiserfs_oid40_t *oid) {
    aal_assert("umka-514", oid != NULL, return -1);
    
    *((uint64_t *)(oid->block->data + oid->offset)) = oid->current;
    return 0;
}

static reiserfs_plugin_t oid40_plugin = {
    .oid = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_OID_PLUGIN,
	    .label = "oid40",
	    .desc = "Inode allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_block_t *, uint16_t))reiserfs_oid40_open,
	.create = (reiserfs_opaque_t *(*)(aal_block_t *, uint16_t))reiserfs_oid40_create,
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_oid40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_oid40_sync,
	.alloc = (uint64_t (*)(reiserfs_opaque_t *))reiserfs_oid40_alloc,
	.dealloc = NULL
    }
};

reiserfs_plugin_t *reiserfs_oid40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &oid40_plugin;
}

reiserfs_plugin_register(reiserfs_oid40_entry);

