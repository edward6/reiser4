/*
    alloc36.c -- Space allocator plugin for reiser3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "alloc36.h"

extern reiser4_plugin_t alloc36_plugin;

static reiser4_core_t *core = NULL;

static reiser4_entity_t *alloc36_open(reiser4_entity_t *format, 
    count_t len) 
{
    reiser4_alloc36_t *alloc;

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
	
    alloc->format = format;
    alloc->plugin = &alloc36_plugin;

    return (reiser4_entity_t *)alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiser4_entity_t *alloc36_create(reiser4_entity_t *format, 
    count_t len) 
{
    reiser4_alloc36_t *alloc;

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    alloc->format = format;
    alloc->plugin = &alloc36_plugin;
    
    return (reiser4_entity_t *)alloc;

error_free_alloc:
    aal_free(alloc);
error:
    return NULL;
}

static errno_t alloc36_sync(reiser4_entity_t *entity) {
    aal_assert("umka-415", entity != NULL, return -1);
    return -1;
}

#endif

static void alloc36_close(reiser4_entity_t *entity) {
    aal_assert("umka-416", entity != NULL, return);
    aal_free(entity);
}

static reiser4_plugin_t alloc36_plugin = {
    .alloc_ops = {
	.h = {
	    .handle = NULL,
	    .id = ALLOC_REISER36_ID,
	    .type = ALLOC_PLUGIN_TYPE,
	    .label = "alloc36",
	    .desc = "Space allocator for reiserfs 3.6.x, ver. " VERSION,
	},

#ifndef ENABLE_COMPACT
	.create	    = alloc36_create,
	.sync	    = alloc36_sync,
#else
	.create	    = NULL,
	.sync	    = NULL,
#endif
	.close	    = alloc36_close,
	.open	    = alloc36_open,

	.mark	    = NULL,
	.test	    = NULL,
	
	.alloc	    = NULL,
	.dealloc    = NULL,
	
	.free	    = NULL,
	.used	    = NULL,

	.valid	    = NULL,
    }
};

static reiser4_plugin_t *alloc36_start(reiser4_core_t *c) {
    core = c;
    return &alloc36_plugin;
}

libreiser4_factory_register(alloc36_start);

