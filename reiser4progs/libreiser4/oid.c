/*
    oid.c -- oid allocator common code and API functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

reiserfs_oid_t *reiserfs_oid_open(void *area_start, void *area_end, 
    reiserfs_id_t oid_pid) 
{
    reiserfs_oid_t *oid;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-519", area_start != NULL, return NULL);
    aal_assert("umka-730", area_end != NULL, return NULL);

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
   
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_OID_PLUGIN, oid_pid))) 
	libreiser4_factory_failed(goto error_free_oid, find, oid, oid_pid);
    
    oid->plugin = plugin;
    
    if (!(oid->entity = libreiser4_plugin_call(goto error_free_oid, 
	plugin->oid_ops, open, area_start, area_end))) 
    {
	aal_throw_error(EO_OK, "Can't open oid allocator %s.", plugin->h.label);
	goto error_free_oid;
    }

    return oid;
    
error_free_oid:
    aal_free(oid);
    return NULL;
}

void reiserfs_oid_close(reiserfs_oid_t *oid) {
    aal_assert("umka-523", oid != NULL, return);

    libreiser4_plugin_call(return, oid->plugin->oid_ops, 
	close, oid->entity);
    
    aal_free(oid);
}

#ifndef ENABLE_COMPACT

reiserfs_oid_t *reiserfs_oid_create(void *area_start, void *area_end, 
    reiserfs_id_t oid_pid) 
{
    reiserfs_oid_t *oid;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-729", area_start != NULL, return NULL);
    aal_assert("umka-731", area_end != NULL, return NULL);

    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
   
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_OID_PLUGIN, oid_pid)))
	libreiser4_factory_failed(goto error_free_oid, find, oid, oid_pid);
    
    oid->plugin = plugin;
    
    if (!(oid->entity = libreiser4_plugin_call(goto error_free_oid, 
	plugin->oid_ops, create, area_start, area_end)))
    {
	aal_throw_error(EO_OK, "Can't create oid allocator %s.", plugin->h.label);
	goto error_free_oid;
    }

    return oid;
    
error_free_oid:
    aal_free(oid);
    return NULL;
}

uint64_t reiserfs_oid_alloc(reiserfs_oid_t *oid) {
    aal_assert("umka-522", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	alloc, oid->entity);
}

void reiserfs_oid_dealloc(reiserfs_oid_t *oid, uint64_t id) {
    aal_assert("umka-525", oid != NULL, return);
    
    libreiser4_plugin_call(return, oid->plugin->oid_ops, 
	dealloc, oid->entity, id);
}

errno_t reiserfs_oid_sync(reiserfs_oid_t *oid) {
    aal_assert("umka-735", oid != NULL, return -1);
    return libreiser4_plugin_call(return -1, oid->plugin->oid_ops, 
	sync, oid->entity);
}

#endif

uint64_t reiserfs_oid_next(reiserfs_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	next, oid->entity);
}

uint64_t reiserfs_oid_used(reiserfs_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	used, oid->entity);
}

oid_t reiserfs_oid_root_parent_locality(reiserfs_oid_t *oid) {
    aal_assert("umka-745", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_parent_locality,);
}

oid_t reiserfs_oid_root_parent_objectid(reiserfs_oid_t *oid) {
    aal_assert("umka-746", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_parent_objectid,);
}

oid_t reiserfs_oid_root_objectid(reiserfs_oid_t *oid) {
    aal_assert("umka-747", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_objectid,);
}

