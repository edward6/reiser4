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
reiser4_oid_t *reiser4_oid_open(
    reiser4_format_t *format	    /* format oid allocated will be opened on */
) {
    rpid_t pid;
    reiser4_oid_t *oid;
    reiser4_plugin_t *plugin;

    void *oid_start;
    uint32_t oid_len;

    aal_assert("umka-519", format != NULL, return NULL);

    /* Allocating memory needed for instance */
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
    
    if ((pid = reiser4_format_oid_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid oid allocator plugin id has been detected.");
	goto error_free_oid;
    }
    
    /* Getting oid allocator plugin */
    if (!(plugin = libreiser4_factory_ifind(OID_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find oid allocator plugin by its id 0x%x.", pid);
	goto error_free_oid;
    }
    
    plugin_call(goto error_free_oid, format->entity->plugin->format_ops, 
	oid_area, format->entity, &oid_start, &oid_len);
    
    /* Initializing entity */
    if (!(oid->entity = plugin_call(goto error_free_oid, 
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
void reiser4_oid_close(
    reiser4_oid_t *oid		/* oid allocator instance to be closed */
) {
    aal_assert("umka-523", oid != NULL, return);

    plugin_call(return, oid->entity->plugin->oid_ops, 
	close, oid->entity);
    
    aal_free(oid);
}

#ifndef ENABLE_COMPACT

/* Creates oid allocator in specified area */
reiser4_oid_t *reiser4_oid_create(
    reiser4_format_t *format	    /* format oid allocator will be oned on */
) {
    rpid_t pid;
    reiser4_oid_t *oid;
    reiser4_plugin_t *plugin;

    void *oid_start;
    uint32_t oid_len;
	
    aal_assert("umka-729", format != NULL, return NULL);

    /* Initializing instance */
    if (!(oid = aal_calloc(sizeof(*oid), 0)))
	return NULL;
   
    if ((pid = reiser4_format_oid_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid oid allocator plugin id has been detected.");
	goto error_free_oid;
    }
    
    /* Getting plugin from plugin id */
    if (!(plugin = libreiser4_factory_ifind(OID_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find oid allocator plugin by its id 0x%x.", pid);
	goto error_free_oid;
    }
    
    plugin_call(goto error_free_oid, format->entity->plugin->format_ops, 
	oid_area, format->entity, &oid_start, &oid_len);
    
    /* Initializing entity */
    if (!(oid->entity = plugin_call(goto error_free_oid, 
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

/* Returns next object id from specified oid allocator */
roid_t reiser4_oid_next(reiser4_oid_t *oid) {
    aal_assert("umka-1108", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	next, oid->entity);
}

/* Returns free object id from specified oid allocator */
roid_t reiser4_oid_allocate(reiser4_oid_t *oid) {
    aal_assert("umka-522", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	allocate, oid->entity);
}

/* Releases passed objectid */
void reiser4_oid_release(
    reiser4_oid_t *oid,	/* oid allocator to be used */
    roid_t id			/* object id to be released */
) {
    aal_assert("umka-525", oid != NULL, return);
    
    plugin_call(return, oid->entity->plugin->oid_ops, 
	release, oid->entity, id);
}

/* Checks specified oid allocator on validness */
errno_t reiser4_oid_valid(reiser4_oid_t *oid) {
    aal_assert("umka-962", oid != NULL, return -1);
    
    return plugin_call(return -1, oid->entity->plugin->oid_ops, 
	valid, oid->entity);
}

/* Synchronizes specified oid allocator */
errno_t reiser4_oid_sync(reiser4_oid_t *oid) {
    aal_assert("umka-735", oid != NULL, return -1);
    return plugin_call(return -1, oid->entity->plugin->oid_ops, 
	sync, oid->entity);
}

#endif

/* Returns number of used oids from passed oid allocator */
uint64_t reiser4_oid_used(reiser4_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	used, oid->entity);
}

/* Returns number of free oids from passed oid allocator */
uint64_t reiser4_oid_free(reiser4_oid_t *oid) {
    aal_assert("umka-527", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	free, oid->entity);
}

/* Returns root parent locality from specified oid allocator */
roid_t reiser4_oid_root_parent_locality(reiser4_oid_t *oid) {
    aal_assert("umka-745", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	root_parent_locality,);
}

/* Returns root parent objectid from specified oid allocator */
roid_t reiser4_oid_root_locality(reiser4_oid_t *oid) {
    aal_assert("umka-746", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	root_locality,);
}

/* Returns root objectid from specified oid allocator */
roid_t reiser4_oid_root_objectid(reiser4_oid_t *oid) {
    aal_assert("umka-747", oid != NULL, return 0);
    
    return plugin_call(return 0, oid->entity->plugin->oid_ops, 
	root_objectid,);
}

