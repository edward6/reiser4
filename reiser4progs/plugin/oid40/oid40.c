/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "oid40.h"

static reiserfs_core_t *core = NULL;

static reiserfs_entity_t *oid40_open(void *start, uint32_t len) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len = len;
    
    oid->next = oid40_get_next(start);
    oid->used = oid40_get_used(start);
    
    return oid;
}

static void oid40_close(reiserfs_oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

#ifndef ENABLE_COMPACT

static reiserfs_entity_t *oid40_create(void *start, uint32_t len) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len= len;

    oid->next = REISERFS_OID40_RESERVED;
    oid->used = 0;
    
    oid40_set_next(start, REISERFS_OID40_RESERVED);
    oid40_set_used(start, 0);
    
    return oid;
}

static errno_t oid40_sync(reiserfs_oid40_t *oid) {
    oid40_set_next(oid->start, oid->next);
    oid40_set_used(oid->start, oid->used);
    
    return 0;
}

static oid_t oid40_alloc(reiserfs_oid40_t *oid) {
    aal_assert("umka-513", oid != NULL, return 0);

    oid->next++;
    oid->used++;
    
    return oid->next;
}

static void oid40_dealloc(reiserfs_oid40_t *oid, oid_t id) {
    aal_assert("umka-528", oid != NULL, return);
    oid->used--;
}

static errno_t oid40_check(reiserfs_oid40_t *oid, int flags) {
    aal_assert("umka-966", oid != NULL, return -1);

    if (oid->next < REISERFS_OID40_ROOT_PARENT_LOCALITY)
	return -1;
    
    return 0;
}

#endif

static oid_t oid40_free(reiserfs_oid40_t *oid) {
    aal_assert("umka-961", oid != NULL, return 0);
    return 0xffffffffffffffff - oid->next;
}

static oid_t oid40_used(reiserfs_oid40_t *oid) {
    aal_assert("umka-530", oid != NULL, return 0);
    return oid->used;
}

static oid_t oid40_root_parent_locality(void) {
    return REISERFS_OID40_ROOT_PARENT_LOCALITY;
}

static oid_t oid40_root_locality(void) {
    return REISERFS_OID40_ROOT_LOCALITY;
}

static oid_t oid40_root_objectid(void) {
    return REISERFS_OID40_ROOT_OBJECTID;
}

static reiserfs_plugin_t oid40_plugin = {
    .oid_ops = {
	.h = {
	    .handle = NULL,
	    .id = OID_REISER40_ID,
	    .type = OID_PLUGIN_TYPE,
	    .label = "oid40",
	    .desc = "Inode allocator for reiserfs 4.0, ver. " VERSION,
	},
	.open = (reiserfs_entity_t *(*)(const void *, uint32_t))oid40_open,
	.close = (void (*)(reiserfs_entity_t *))oid40_close,

#ifndef ENABLE_COMPACT	
	.create = (reiserfs_entity_t *(*)(const void *, uint32_t))oid40_create,
	.alloc = (oid_t (*)(reiserfs_entity_t *))oid40_alloc,
	.dealloc = (void (*)(reiserfs_entity_t *, oid_t))oid40_dealloc,
	.sync = (errno_t (*)(reiserfs_entity_t *))oid40_sync,
	.check = (errno_t (*)(reiserfs_entity_t *, int))oid40_check,
#else
	.create = NULL,
	.alloc = NULL,
	.dealloc = NULL,
	.sync = NULL,
	.check = NULL, 
#endif
	.used = (oid_t (*)(reiserfs_entity_t *))oid40_used,
	.free = (oid_t (*)(reiserfs_entity_t *))oid40_free,
	
	.root_parent_locality = (oid_t (*)(void))oid40_root_parent_locality,
	.root_locality = (oid_t (*)(void))oid40_root_locality,
	.root_objectid = (oid_t (*)(void))oid40_root_objectid
    }
};

static reiserfs_plugin_t *oid40_entry(reiserfs_core_t *c) {
    core = c;
    return &oid40_plugin;
}

libreiser4_factory_register(oid40_entry);

