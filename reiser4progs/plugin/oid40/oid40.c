/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>
#include "oid40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static reiserfs_opaque_t *reiserfs_oid40_init(oid_t next, oid_t used) {
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    oid->next = next;
    oid->used = used;
    
    return oid;
}

static void reiserfs_oid40_fini(reiserfs_oid40_t *oid) {
    aal_assert("umka-510", oid != NULL, return);
    aal_free(oid);
}

static oid_t reiserfs_oid40_alloc(reiserfs_oid40_t *oid) {
    aal_assert("umka-513", oid != NULL, return 0);
    oid->used++;
    return oid->next++;
}

static void reiserfs_oid40_dealloc(reiserfs_oid40_t *oid, oid_t inode) {
    aal_assert("umka-528", oid != NULL, return);
    oid->next--;
    oid->used--;
}

static oid_t reiserfs_oid40_next(reiserfs_oid40_t *oid) {
    aal_assert("umka-529", oid != NULL, return 0);
    return oid->next;
}

static oid_t reiserfs_oid40_used(reiserfs_oid40_t *oid) {
    aal_assert("umka-530", oid != NULL, return 0);
    return oid->used;
}

oid_t reiserfs_oid40_root_parent_locality(reiserfs_oid40_t *oid) {
    return REISERFS_OID40_ROOT_PARENT_LOCALITY;
}

oid_t reiserfs_oid40_root_parent_objectid(reiserfs_oid40_t *oid) {
    return REISERFS_OID40_ROOT_PARENT_OBJECTID;
}

oid_t reiserfs_oid40_root_objectid(reiserfs_oid40_t *oid) {
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
	.init = (reiserfs_opaque_t *(*)(oid_t, oid_t))reiserfs_oid40_init,
	.fini = (void (*)(reiserfs_opaque_t *))reiserfs_oid40_fini,
	
	.alloc = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_alloc,
	.dealloc = (void (*)(reiserfs_opaque_t *, oid_t))reiserfs_oid40_dealloc,
	
	.next = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_next,
	.used = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_used,
	
	.root_parent_locality = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_root_parent_locality,
	.root_parent_objectid = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_root_parent_objectid,
	
	.root_objectid = (oid_t (*)(reiserfs_opaque_t *))reiserfs_oid40_root_objectid,
    }
};

reiserfs_plugin_t *reiserfs_oid40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &oid40_plugin;
}

libreiserfs_plugins_register(reiserfs_oid40_entry);

