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

static reiserfs_entity_t *oid40_open(void *area_start, void *area_end) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->area_start = area_start;
    oid->area_end = area_end;
    
    return oid;
}

static void oid40_close(reiserfs_oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

#ifndef ENABLE_COMPACT

static reiserfs_entity_t *oid40_create(void *area_start, void *area_end) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->area_start = area_start;
    oid->area_end = area_end;
    
    oid40_set_next(area_start, REISERFS_OID40_RESERVED);
    oid40_set_used(area_start, 0);
    
    return oid;
}

static errno_t oid40_sync(reiserfs_oid40_t *oid) {
    return 0;
}

static oid_t oid40_alloc(reiserfs_oid40_t *oid) {
    oid_t next, used;
	    
    aal_assert("umka-513", oid != NULL, return 0);

    next = oid40_get_next(oid->area_start);
    used = oid40_get_used(oid->area_start);
    
    oid40_set_next(oid->area_start, next + 1);
    oid40_set_used(oid->area_start, used + 1);
    return next;
}

static void oid40_dealloc(reiserfs_oid40_t *oid, oid_t inode) {
    oid_t used;
    
    aal_assert("umka-528", oid != NULL, return);
    
    used = oid40_get_used(oid->area_start);
    oid40_set_used(oid->area_start, used - 1);
}

#endif

static oid_t oid40_next(reiserfs_oid40_t *oid) {
    aal_assert("umka-529", oid != NULL, return 0);
    return oid40_get_next(oid->area_start);
}

static oid_t oid40_used(reiserfs_oid40_t *oid) {
    aal_assert("umka-530", oid != NULL, return 0);
    return oid40_get_used(oid->area_start);
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
	    .id = 0x0,
	    .type = REISERFS_OID_PLUGIN,
	    .label = "oid40",
	    .desc = "Default inode allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_entity_t *(*)(void *, void *))oid40_open,
	.close = (void (*)(reiserfs_entity_t *))oid40_close,

#ifndef ENABLE_COMPACT	
	.create = (reiserfs_entity_t *(*)(void *, void *))oid40_create,
	.alloc = (oid_t (*)(reiserfs_entity_t *))oid40_alloc,
	.dealloc = (void (*)(reiserfs_entity_t *, oid_t))oid40_dealloc,
	.sync = (errno_t (*)(reiserfs_entity_t *))oid40_sync,
	.check = NULL,
#else
	.create = NULL,
	.alloc = NULL,
	.dealloc = NULL,
	.sync = NULL,
	.check = NULL, 
#endif
	.confirm = NULL,
	.next = (oid_t (*)(reiserfs_entity_t *))oid40_next,
	.used = (oid_t (*)(reiserfs_entity_t *))oid40_used,
	
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

