/*
    oid.c -- oid allocator common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

/* Opens object allocator using start and end pointers */
reiserfs_oid_t *reiserfs_oid_open(
    reiserfs_format_t *format	    /* format oid allocated will be opened on */
) {
    reiserfs_id_t pid;
    reiserfs_oid_t *oid;
    reiserfs_plugin_t *plugin;

    void *oid_start;
    uint32_t oid_len;

    aal_assert("umka-519", format != NULL, return NULL);

    /* Allocating memory needed for instance */
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    if ((pid = reiserfs_format_oid_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid oid allocator plugin id has been detected.");
	goto error_free_oid;
    }
    
    /* Getting oid allocator plugin */
    if (!(plugin = libreiser4_factory_find_by_id(OID_PLUGIN_TYPE, pid))) 
	libreiser4_factory_failed(goto error_free_oid, find, oid, pid);
    
    oid->plugin = plugin;
    
    libreiser4_plugin_call(goto error_free_oid, format->plugin->format_ops, 
	oid_area, format->entity, &oid_start, &oid_len);
    
    /* Initializing entity */
    if (!(oid->entity = libreiser4_plugin_call(goto error_free_oid, 
	plugin->oid_ops, open, oid_start, oid_len))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open oid allocator %s.", plugin->h.label);
	goto error_free_oid;
    }

    return oid;
    
error_free_oid:
    aal_free(oid);
    return NULL;
}

/* Closes oid allocator */
void reiserfs_oid_close(
    reiserfs_oid_t *oid		/* oid allocator instance to be closed */
) {
    aal_assert("umka-523", oid != NULL, return);

    libreiser4_plugin_call(return, oid->plugin->oid_ops, 
	close, oid->entity);
    
    aal_free(oid);
}

#ifndef ENABLE_COMPACT

/* Creates oid allocator in specified area */
reiserfs_oid_t *reiserfs_oid_create(
    reiserfs_format_t *format	    /* format oid allocator will be oned on */
) {
    reiserfs_id_t pid;
    reiserfs_oid_t *oid;
    reiserfs_plugin_t *plugin;

    void *oid_start;
    uint32_t oid_len;
	
    aal_assert("umka-729", format != NULL, return NULL);

    /* Initializing instance */
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
   
    if ((pid = reiserfs_format_oid_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid oid allocator plugin id has been detected.");
	goto error_free_oid;
    }
    
    /* Getting plugin from plugin id */
    if (!(plugin = libreiser4_factory_find_by_id(OID_PLUGIN_TYPE, pid)))
	libreiser4_factory_failed(goto error_free_oid, find, oid, pid);
    
    oid->plugin = plugin;
    
    libreiser4_plugin_call(goto error_free_oid, format->plugin->format_ops, 
	oid_area, format->entity, &oid_start, &oid_len);
    
    /* Initializing entity */
    if (!(oid->entity = libreiser4_plugin_call(goto error_free_oid, 
	plugin->oid_ops, create, oid_start, oid_len)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create oid allocator %s.", plugin->h.label);
	goto error_free_oid;
    }

    return oid;
    
error_free_oid:
    aal_free(oid);
    return NULL;
}

/* Returns free object id from specified oid allocator */
uint64_t reiserfs_oid_alloc(reiserfs_oid_t *oid) {
    aal_assert("umka-522", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	alloc, oid->entity);
}

/* Releases passed objectid */
void reiserfs_oid_dealloc(
    reiserfs_oid_t *oid,	/* oid allocator to be used */
    uint64_t id			/* object id to be released */
) {
    aal_assert("umka-525", oid != NULL, return);
    
    libreiser4_plugin_call(return, oid->plugin->oid_ops, 
	dealloc, oid->entity, id);
}

/* Checks specified oid allocator on validness */
errno_t reiserfs_oid_valid(reiserfs_oid_t *oid, int flags) {
    aal_assert("umka-962", oid != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, oid->plugin->oid_ops, 
	valid, oid->entity, flags);
}

/* Synchronizes specified oid allocator */
errno_t reiserfs_oid_sync(reiserfs_oid_t *oid) {
    aal_assert("umka-735", oid != NULL, return -1);
    return libreiser4_plugin_call(return -1, oid->plugin->oid_ops, 
	sync, oid->entity);
}

#endif

/* Returns number of used oids from passed oid allocator */
uint64_t reiserfs_oid_used(reiserfs_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	used, oid->entity);
}

/* Returns number of free oids from passed oid allocator */
uint64_t reiserfs_oid_free(reiserfs_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	free, oid->entity);
}

/* Returns root parent locality from specified oid allocator */
oid_t reiserfs_oid_root_parent_locality(reiserfs_oid_t *oid) {
    aal_assert("umka-745", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_parent_locality,);
}

/* Returns root parent objectid from specified oid allocator */
oid_t reiserfs_oid_root_locality(reiserfs_oid_t *oid) {
    aal_assert("umka-746", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_locality,);
}

/* Returns root objectid from specified oid allocator */
oid_t reiserfs_oid_root_objectid(reiserfs_oid_t *oid) {
    aal_assert("umka-747", oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, oid->plugin->oid_ops, 
	root_objectid,);
}

