/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>
#include "oid40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static reiserfs_opaque_t *oid40_open(oid_t next, oid_t used) {
    oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    oid->next = next;
    oid->used = used;
    
    return oid;
}

static void oid40_close(oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

static oid_t oid40_alloc(oid40_t *oid) {
    aal_assert("umka-513", oid != NULL, return 0);
    oid->used++;
    return oid->next++;
}

static void oid40_dealloc(oid40_t *oid, oid_t inode) {
    aal_assert("umka-528", oid != NULL, return);
    oid->next--;
    oid->used--;
}

static oid_t oid40_next(oid40_t *oid) {
    aal_assert("umka-529", oid != NULL, return 0);
    return oid->next;
}

static oid_t oid40_used(oid40_t *oid) {
    aal_assert("umka-530", oid != NULL, return 0);
    return oid->used;
}

oid_t oid40_root_parent_locality(oid40_t *oid) {
    return REISERFS_OID40_ROOT_PARENT_LOCALITY;
}

oid_t oid40_root_parent_objectid(oid40_t *oid) {
    return REISERFS_OID40_ROOT_PARENT_OBJECTID;
}

oid_t oid40_root_objectid(oid40_t *oid) {
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
	.open = (reiserfs_opaque_t *(*)(oid_t, oid_t))oid40_open,
	.close = (void (*)(reiserfs_opaque_t *))oid40_close,
	
	.alloc = (oid_t (*)(reiserfs_opaque_t *))oid40_alloc,
	.dealloc = (void (*)(reiserfs_opaque_t *, oid_t))oid40_dealloc,
	
	.next = (oid_t (*)(reiserfs_opaque_t *))oid40_next,
	.used = (oid_t (*)(reiserfs_opaque_t *))oid40_used,
	
	.root_parent_locality = (oid_t (*)(reiserfs_opaque_t *))oid40_root_parent_locality,
	.root_parent_objectid = (oid_t (*)(reiserfs_opaque_t *))oid40_root_parent_objectid,
	
	.root_objectid = (oid_t (*)(reiserfs_opaque_t *))oid40_root_objectid,
    }
};

reiserfs_plugin_t *oid40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &oid40_plugin;
}

libreiserfs_plugins_register(oid40_entry);

