/*
    alloc.c -- reiserfs block allocator common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

error_t reiserfs_alloc_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_id_t plugin_id;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-135", fs != NULL, return -1);
    aal_assert("umka-333", fs->format != NULL, return -1);
	
    if (fs->alloc) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block allocator already opened.");
	return -1;
    }
	
    if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
	return -1;
    
    plugin_id = reiserfs_format_alloc_plugin_id(fs);
    if (!(plugin = libreiser4_plugins_find_by_coords(REISERFS_ALLOC_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find block allocator plugin by its identifier %x.", plugin_id);
	goto error_free_alloc;
    }

    fs->alloc->plugin = plugin;

    if (!(fs->alloc->entity = reiserfs_format_alloc(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open block allocator.");
	goto error_free_alloc;
    }
	
    return 0;
	
error_free_alloc:
    aal_free(fs->alloc);
    fs->alloc = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_alloc_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-138", fs != NULL, return -1);
    aal_assert("umka-139", fs->alloc != NULL, return -1);

    return libreiserfs_plugins_call(return -1, fs->alloc->plugin->alloc, 
	sync, fs->alloc->entity);
}

#endif

void reiserfs_alloc_close(reiserfs_fs_t *fs) {
    aal_assert("umka-140", fs != NULL, return);
    aal_assert("umka-141", fs->alloc != NULL, return);
	
    aal_free(fs->alloc);
    fs->alloc = NULL;
}

count_t reiserfs_alloc_free(reiserfs_fs_t *fs) {
    aal_assert("umka-361", fs != NULL, return 0);
    aal_assert("umka-362", fs->alloc != NULL, return 0);
	
    return libreiserfs_plugins_call(return 0, fs->alloc->plugin->alloc, 
	free, fs->alloc->entity);
}

count_t reiserfs_alloc_used(reiserfs_fs_t *fs) {
    aal_assert("umka-498", fs != NULL, return 0);
    aal_assert("umka-499", fs->alloc != NULL, return 0);
	
    return libreiserfs_plugins_call(return 0, fs->alloc->plugin->alloc, 
	used, fs->alloc->entity);
}

void reiserfs_alloc_mark(reiserfs_fs_t *fs, blk_t blk) {
    aal_assert("umka-500", fs != NULL, return);
    aal_assert("umka-501", fs->alloc != NULL, return);

    libreiserfs_plugins_call(return, fs->alloc->plugin->alloc, 
	mark, fs->alloc->entity, blk);
}

void reiserfs_alloc_dealloc(reiserfs_fs_t *fs, blk_t blk) {
    aal_assert("umka-502", fs != NULL, return);
    aal_assert("umka-503", fs->alloc != NULL, return);

    libreiserfs_plugins_call(return, fs->alloc->plugin->alloc, 
	dealloc, fs->alloc->entity, blk);
}

blk_t reiserfs_alloc_alloc(reiserfs_fs_t *fs) {
    aal_assert("umka-504", fs != NULL, return 0);
    aal_assert("umka-505", fs->alloc != NULL, return 0);

    return libreiserfs_plugins_call(return 0, fs->alloc->plugin->alloc, 
	alloc, fs->alloc->entity);
}

