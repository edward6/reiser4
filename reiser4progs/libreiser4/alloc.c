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
    Initializes block allocator structures and requests block allocator plugin 
    for opening. Returns initialized instance of block allocator, which may be 
    used in all further operations.
*/
reiserfs_alloc_t *reiserfs_alloc_open(aal_device_t *device, 
    count_t len, reiserfs_id_t pid) 
{
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-135", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    /* Finding block allocator plugin */
    if (!(plugin = libreiser4_factory_find(REISERFS_ALLOC_PLUGIN, pid)))
    	libreiser4_factory_failed(goto error_free_alloc, find, alloc, pid);

    alloc->plugin = plugin;

    /* Calling "open" method from block allocator plugin */
    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc_ops, open, device, len)))
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

/* 
    Creates new block allocator. Initializes all structures, calles block allocator 
    plugin in order to initialize allocator instance and returns instance to caller.
*/
reiserfs_alloc_t *reiserfs_alloc_create(aal_device_t *device, 
    count_t len, reiserfs_id_t pid) 
{
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-726", device != NULL, return NULL);

    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if (!(plugin = libreiser4_factory_find(REISERFS_ALLOC_PLUGIN, pid)))
    	libreiser4_factory_failed(goto error_free_alloc, find, alloc, pid);

    alloc->plugin = plugin;

    /* Query the block allocator plugin */
    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc_ops, create, device, len)))
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

/* Make request to allocator plugin in order to save its data to device */
errno_t reiserfs_alloc_sync(reiserfs_alloc_t *alloc) {
    aal_assert("umka-139", alloc != NULL, return -1);

    return libreiser4_plugin_call(return -1, alloc->plugin->alloc_ops, 
	sync, alloc->entity);
}

#endif

/* Close passed allocator instance */
void reiserfs_alloc_close(reiserfs_alloc_t *alloc) {
    aal_assert("umka-141", alloc != NULL, return);

    /* Calling the plugin for close its internal instance properly */
    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	close, alloc->entity);
    
    aal_free(alloc);
}

/* Returns the number of fre blocks in allocator */
count_t reiserfs_alloc_free(reiserfs_alloc_t *alloc) {
    aal_assert("umka-362", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	free, alloc->entity);
}

/* Returns the number of used blocks in allocator */
count_t reiserfs_alloc_used(reiserfs_alloc_t *alloc) {
    aal_assert("umka-499", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	used, alloc->entity);
}

#ifndef ENABLE_COMPACT

/* Marks specified block as used */
void reiserfs_alloc_mark(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-501", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	mark, alloc->entity, blk);
}

/* Deallocs specified block */
void reiserfs_alloc_dealloc(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-503", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	dealloc, alloc->entity, blk);
}

/* Makes request to plugin for allocating block */
blk_t reiserfs_alloc_alloc(reiserfs_alloc_t *alloc) {
    aal_assert("umka-505", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	alloc, alloc->entity);
}

#endif

/* 
    Checks whether specified block used or not. Returns TRUE for used and FALSE 
    otherwise.
*/
int reiserfs_alloc_test(reiserfs_alloc_t *alloc, blk_t blk) {
    aal_assert("umka-662", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	test, alloc->entity, blk);
}

