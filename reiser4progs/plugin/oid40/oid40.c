/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>
#include "oid40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static reiserfs_opaque_t *oid40_open(void *area, uint32_t len) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->area = area;
    oid->len = len;
    
    return oid;
}

static void oid40_close(reiserfs_oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

static oid_t oid40_alloc(reiserfs_oid40_t *oid) {
    oid_t next, used;
	    
    aal_assert("umka-513", oid != NULL, return 0);

    next = oid40_get_next(oid->area);
    used = oid40_get_used(oid->area);
    
    oid40_set_next(oid->area, next + 1);
    oid40_set_used(oid->area, used + 1);
    return next;
}

static void oid40_dealloc(reiserfs_oid40_t *oid, oid_t inode) {
    oid_t used;
    
    aal_assert("umka-528", oid != NULL, return);
    
    used = oid40_get_used(oid->area);
    oid40_set_used(oid->area, used - 1);
}

static oid_t oid40_next(reiserfs_oid40_t *oid) {
    aal_assert("umka-529", oid != NULL, return 0);
    return oid40_get_next(oid->area);
}

static oid_t oid40_used(reiserfs_oid40_t *oid) {
    aal_assert("umka-530", oid != NULL, return 0);
    return oid40_get_used(oid->area);
}

static oid_t oid40_root_parent_locality(void) {
    return REISERFS_OID40_ROOT_PARENT_LOCALITY;
}

static oid_t oid40_root_parent_objectid(void) {
    return REISERFS_OID40_ROOT_PARENT_OBJECTID;
}

static oid_t oid40_root_objectid(void) {
    return REISERFS_OID40_ROOT_OBJECTID;
}

static reiserfs_plugin_t oid40_plugin = {
    .oid = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_OID_PLUGIN,
	    .label = "oid40",
	    .desc = "Default inode allocator for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(void *, uint32_t))oid40_open,
	.close = (void (*)(reiserfs_opaque_t *))oid40_close,
	
	.alloc = (oid_t (*)(reiserfs_opaque_t *))oid40_alloc,
	.dealloc = (void (*)(reiserfs_opaque_t *, oid_t))oid40_dealloc,
	
	.next = (oid_t (*)(reiserfs_opaque_t *))oid40_next,
	.used = (oid_t (*)(reiserfs_opaque_t *))oid40_used,
	
	.root_parent_locality = (oid_t (*)(void))oid40_root_parent_locality,
	.root_parent_objectid = (oid_t (*)(void))oid40_root_parent_objectid,
	.root_objectid = (oid_t (*)(void))oid40_root_objectid,
    }
};

static reiserfs_plugin_t *oid40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &oid40_plugin;
}

libreiser4_plugins_register(oid40_entry);

