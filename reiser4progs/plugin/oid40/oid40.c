/*
    oid40.c -- reiser4 default oid allocator plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "oid40.h"

extern reiser4_plugin_t oid40_plugin;

static reiser4_core_t *core = NULL;

static reiser4_entity_t *oid40_open(const void *start, 
    uint32_t len) 
{
    oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len = len;
    
    oid->next = oid40_get_next(start);
    oid->used = oid40_get_used(start);
    oid->plugin = &oid40_plugin;
    
    return (reiser4_entity_t *)oid;
}

static void oid40_close(reiser4_entity_t *entity) {
    aal_assert("umka-510", entity != NULL, return);
    aal_free(entity);
}

#ifndef ENABLE_COMPACT

static reiser4_entity_t *oid40_create(const void *start, 
    uint32_t len) 
{
    oid40_t *oid;

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;

    oid->start = start;
    oid->len = len;

    oid->next = OID40_RESERVED;
    oid->used = 0;
    
    oid->plugin = &oid40_plugin;
    
    oid40_set_next(start, OID40_RESERVED);
    oid40_set_used(start, 0);
    
    return (reiser4_entity_t *)oid;
}

static errno_t oid40_sync(reiser4_entity_t *entity) {
    aal_assert("umka-1016", entity != NULL, return -1);
    
    oid40_set_next(((oid40_t *)entity)->start, 
	((oid40_t *)entity)->next);
    
    oid40_set_used(((oid40_t *)entity)->start, 
	((oid40_t *)entity)->used);
    
    return 0;
}

static oid_t oid40_alloc(reiser4_entity_t *entity) {
    aal_assert("umka-513", entity != NULL, return 0);

    ((oid40_t *)entity)->next++;
    ((oid40_t *)entity)->used++;
    
    return ((oid40_t *)entity)->next - 1;
}

static void oid40_dealloc(reiser4_entity_t *entity, 
    oid_t id) 
{
    aal_assert("umka-528", entity != NULL, return);
    ((oid40_t *)entity)->used--;
}

#endif

static errno_t oid40_valid(reiser4_entity_t *entity) {
    aal_assert("umka-966", entity != NULL, return -1);

    if (((oid40_t *)entity)->next < OID40_ROOT_PARENT_LOCALITY)
	return -1;
    
    return 0;
}

static oid_t oid40_free(reiser4_entity_t *entity) {
    aal_assert("umka-961", entity != NULL, return 0);
    return ~0ull - ((oid40_t *)entity)->next;
}

static oid_t oid40_used(reiser4_entity_t *entity) {
    aal_assert("umka-530", entity != NULL, return 0);
    return ((oid40_t *)entity)->used;
}

static oid_t oid40_root_parent_locality(void) {
    return OID40_ROOT_PARENT_LOCALITY;
}

static oid_t oid40_root_locality(void) {
    return OID40_ROOT_LOCALITY;
}

static oid_t oid40_root_objectid(void) {
    return OID40_ROOT_OBJECTID;
}

static reiser4_plugin_t oid40_plugin = {
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

static reiser4_plugin_t *oid40_start(reiser4_core_t *c) {
    core = c;
    return &oid40_plugin;
}

plugin_register(oid40_start);

