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
reiserfs_alloc_t *reiserfs_alloc_open(
    reiserfs_format_t *format	/* disk-format allocator is going to be opened on */
) {
    count_t len;
    reiserfs_id_t pid;
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-135", format != NULL, return NULL);

    /* Initializing instance of block allocator */
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;
    
    if ((pid = reiserfs_format_alloc_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid block allocator plugin id has been found.");
	goto error_free_alloc;
    }
    
    /* Finding block allocator plugin */
    if (!(plugin = libreiser4_factory_find_by_id(ALLOC_PLUGIN_TYPE, pid)))
    	libreiser4_factory_failed(goto error_free_alloc, find, alloc, pid);

    alloc->plugin = plugin;

    if (!(len = reiserfs_format_get_len(format))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid filesystem size has been found.");
	goto error_free_alloc;
    }

    /* Calling "open" method from block allocator plugin */
    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc_ops, open, format->device, len)))
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
reiserfs_alloc_t *reiserfs_alloc_create(
    reiserfs_format_t *format	    /* disk-format block allocator is going to be created on */
) {
    count_t len;
    reiserfs_id_t pid;
    reiserfs_alloc_t *alloc;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-726", format != NULL, return NULL);

    /* Allocating memory for the allocator instance */
    if (!(alloc = aal_calloc(sizeof(*alloc), 0)))
	return NULL;

    if ((pid = reiserfs_format_alloc_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid block allocator plugin id has been found.");
	goto error_free_alloc;
    }
    
    /* Getting needed plugin from plugin factory by its id */
    if (!(plugin = libreiser4_factory_find_by_id(ALLOC_PLUGIN_TYPE, pid)))
    	libreiser4_factory_failed(goto error_free_alloc, find, block allocator, pid);

    alloc->plugin = plugin;

    if (!(len = reiserfs_format_get_len(format))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid filesystem size has been found.");
	goto error_free_alloc;
    }

    /* Query the block allocator plugin for creating allocator entity */
    if (!(alloc->entity = libreiser4_plugin_call(goto error_free_alloc, 
	plugin->alloc_ops, create, format->device, len)))
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
errno_t reiserfs_alloc_sync(
    reiserfs_alloc_t *alloc	/* allocator to be syncked */
) {
    aal_assert("umka-139", alloc != NULL, return -1);

    return libreiser4_plugin_call(return -1, alloc->plugin->alloc_ops, 
	sync, alloc->entity);
}

#endif

/* Close passed allocator instance */
void reiserfs_alloc_close(
    reiserfs_alloc_t *alloc	/* allocator to be closed */
) {
    aal_assert("umka-141", alloc != NULL, return);

    /* Calling the plugin for close its internal instance properly */
    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	close, alloc->entity);
    
    aal_free(alloc);
}

/* Returns the number of free blocks in allocator */
count_t reiserfs_alloc_free(
    reiserfs_alloc_t *alloc	/* allocator to be realeased */
) {
    aal_assert("umka-362", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	free, alloc->entity);
}

/* Returns the number of used blocks in allocator */
count_t reiserfs_alloc_used(
    reiserfs_alloc_t *alloc	/* allocator used blocks will be obtained from */
) {
    aal_assert("umka-499", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	used, alloc->entity);
}

#ifndef ENABLE_COMPACT

/* Marks specified block as used */
void reiserfs_alloc_mark(
    reiserfs_alloc_t *alloc,	/* allocator for working with */
    blk_t blk			/* block to be marked */
) {
    aal_assert("umka-501", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	mark, alloc->entity, blk);
}

/* Deallocs specified block */
void reiserfs_alloc_dealloc(
    reiserfs_alloc_t *alloc,	/* allocator for wiorking with */
    blk_t blk			/* block to be deallocated */
) {
    aal_assert("umka-503", alloc != NULL, return);

    libreiser4_plugin_call(return, alloc->plugin->alloc_ops, 
	dealloc, alloc->entity, blk);
}

/* Makes request to plugin for allocating block */
blk_t reiserfs_alloc_alloc(
    reiserfs_alloc_t *alloc	/* allocator for working with */
) {
    aal_assert("umka-505", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	alloc, alloc->entity);
}

errno_t reiserfs_alloc_check(
    reiserfs_alloc_t *alloc,	/* allocator to be checked */
    int flags			/* some flags will be used by plugin in the future */
) {
    aal_assert("umka-833", alloc != NULL, return -1);

    return libreiser4_plugin_call(return -1, alloc->plugin->alloc_ops, 
	check, alloc->entity, flags);
}

#endif

/* 
    Checks whether specified block used or not. Returns TRUE for used and FALSE 
    otherwise.
*/
int reiserfs_alloc_test(
    reiserfs_alloc_t *alloc,	/* allocator for working with */
    blk_t blk			/* block to be tested (used or not ) */
) {
    aal_assert("umka-662", alloc != NULL, return 0);

    return libreiser4_plugin_call(return 0, alloc->plugin->alloc_ops, 
	test, alloc->entity, blk);
}

