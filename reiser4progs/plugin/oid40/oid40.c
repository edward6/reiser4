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

static reiserfs_entity_t *oid40_open(const void *start, 
    uint32_t len) 
{
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len = len;
    
    oid->next = oid40_get_next(start);
    oid->used = oid40_get_used(start);
    
    return (reiserfs_entity_t *)oid;
}

static void oid40_close(reiserfs_entity_t *entity) {
    aal_assert("umka-510", entity != NULL, return);
    aal_free(entity);
}

#ifndef ENABLE_COMPACT

static reiserfs_entity_t *oid40_create(const void *start, 
    uint32_t len) 
{
    reiserfs_oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len = len;

    oid->next = REISERFS_OID40_RESERVED;
    oid->used = 0;
    
    oid40_set_next(start, REISERFS_OID40_RESERVED);
    oid40_set_used(start, 0);
    
    return oid;
}

static errno_t oid40_sync(reiserfs_entity_t *entity) {
    aal_assert("umka-1016", entity != NULL, return -1);
    
    oid40_set_next(((reiserfs_oid40_t *)entity)->start, 
	((reiserfs_oid40_t *)entity)->next);
    
    oid40_set_used(((reiserfs_oid40_t *)entity)->start, 
	((reiserfs_oid40_t *)entity)->used);
    
    return 0;
}

static oid_t oid40_alloc(reiserfs_entity_t *entity) {
    aal_assert("umka-513", entity != NULL, return 0);

    ((reiserfs_oid40_t *)entity)->next++;
    ((reiserfs_oid40_t *)entity)->used++;
    
    return ((reiserfs_oid40_t *)entity)->next;
}

static void oid40_dealloc(reiserfs_entity_t *entity, 
    oid_t id) 
{
    aal_assert("umka-528", entity != NULL, return);
    ((reiserfs_oid40_t *)entity)->used--;
}

static errno_t oid40_valid(reiserfs_entity_t *entity, 
    int flags) 
{
    aal_assert("umka-966", entity != NULL, return -1);

    if (((reiserfs_oid40_t *)entity)->next < 
	    REISERFS_OID40_ROOT_PARENT_LOCALITY)
	return -1;
    
    return 0;
}

#endif

static oid_t oid40_free(reiserfs_entity_t *entity) {
    aal_assert("umka-961", entity != NULL, return 0);
    return ~0ull - ((reiserfs_oid40_t *)entity)->next;
}

static oid_t oid40_used(reiserfs_entity_t *entity) {
    aal_assert("umka-530", entity != NULL, return 0);
    return ((reiserfs_oid40_t *)entity)->used;
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
	.open		= oid40_open,
	.close		= oid40_close,
	.valid		= oid40_valid,
#ifndef ENABLE_COMPACT	
	.create		= oid40_create,
	.alloc		= oid40_alloc,
	.dealloc	= oid40_dealloc,
	.sync		= oid40_sync,
#else
	.create		= NULL,
	.alloc		= NULL,
	.dealloc	= NULL,
	.sync		= NULL,
#endif
	.used		= oid40_used,
	.free		= oid40_free,
	
	.root_locality	= oid40_root_locality,
	.root_objectid	= oid40_root_objectid,
		
	.root_parent_locality	= oid40_root_parent_locality
    }
};

static reiserfs_plugin_t *oid40_start(reiserfs_core_t *c) {
    core = c;
    return &oid40_plugin;
}

libreiser4_factory_register(oid40_start);

