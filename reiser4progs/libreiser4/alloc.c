/*
    alloc.c -- reiserfs block allocator common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

/*
    Allocates block allocator structures and
    requests block allocator plugin for opening.
*/
reiserfs_alloc_t *reiserfs_alloc_open(aal_device_t *device, 
    count_t len, reiserfs_id_t plugin_id) 
{
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-135", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(plugin = libreiser4_factory_find(REISERFS_ALLOC_PLUGIN, plugin_id)))
    	libreiser4_factory_failed(goto error_free_alloc, find, alloc, plugin_id);

    alloc->plugin = plugin;

    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc, open, device, len)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize block allocator.");
	goto error_free_alloc;
    }
	
    return alloc;
	
error_free_alloc:
    aal_free(alloc);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_alloc_t *reiserfs_alloc_create(aal_device_t *device, 
    count_t len, reiserfs_id_t plugin_id) 
{
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-726", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(plugin = libreiser4_factory_find(REISERFS_ALLOC_PLUGIN, plugin_id)))
    	libreiser4_factory_failed(goto error_free_alloc, find, alloc, plugin_id);

    alloc->plugin = plugin;

    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc, create, device, len)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create block allocator.");
	goto error_free_alloc;
    }
	
    return alloc;
	
error_free_alloc:
    aal_free(alloc);
    return NULL;
}

error_t reiserfs_alloc_sync(reiserfs_alloc_t *alloc) {
    aal_assert("umka-139", alloc != NULL, return -1);

    return libreiser4_plugin_call(return -1, alloc->plugin->alloc, 
	sync, alloc->entity);
}

#endif

void reiserfs_alloc_close(reiserfs_alloc_t *alloc) {
    aal_assert("umka-141", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc, 
	close, alloc->entity);
    
    aal_free(alloc);
}

count_t reiserfs_alloc_free(reiserfs_alloc_t *alloc) {
    aal_assert("umka-362", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc, 
	free, alloc->entity);
}

count_t reiserfs_alloc_used(reiserfs_alloc_t *alloc) {
    aal_assert("umka-499", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc, 
	used, alloc->entity);
}

#ifndef ENABLE_COMPACT

void reiserfs_alloc_mark(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-501", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc, 
	mark, alloc->entity, blk);
}

void reiserfs_alloc_dealloc(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-503", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc, 
	dealloc, alloc->entity, blk);
}

blk_t reiserfs_alloc_alloc(reiserfs_alloc_t *alloc) {
    aal_assert("umka-505", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc, 
	alloc, alloc->entity);
}

#endif

int reiserfs_alloc_test(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-662", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc, 
	test, alloc->entity, blk);
}

