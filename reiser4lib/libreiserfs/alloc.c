/*
    alloc.c -- reiserfs block allocator common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

error_t reiserfs_alloc_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_id_t plugin_id;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-135", fs != NULL, return -1);
    aal_assert("umka-333", fs->super != NULL, return -1);
	
    if (fs->alloc) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block allocator already initialized.");
	return -1;
    }
	
    if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
	return -1;
	
    plugin_id = reiserfs_super_alloc_plugin(fs);
    if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_ALLOC_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find block allocator plugin by its identifier %x.", plugin_id);
	goto error_free_alloc;
    }

    fs->alloc->plugin = plugin;

    reiserfs_plugin_check_routine(plugin->alloc, open, goto error_free_alloc);
    if (!(fs->alloc->entity = plugin->alloc.open(fs->device, reiserfs_super_get_blocks(fs)))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize block allocator plugin.");
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

error_t reiserfs_alloc_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t plugin_id, count_t blocks) {
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-137", fs != NULL, return -1);
	
    if (fs->alloc) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block allocator already initialized.");
	return -1;
    }
	
    if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
	return -1;
	
    if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_ALLOC_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find block allocator plugin by its identifier %x.", plugin_id);
	goto error_free_alloc;
    }
    fs->alloc->plugin = plugin;

    reiserfs_plugin_check_routine(plugin->alloc, create, goto error_free_alloc);
    if (!(fs->alloc->entity = plugin->alloc.create(fs->device, blocks))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocator.");
	goto error_free_alloc;
    }
	
    return 0;
	
error_free_alloc:
    aal_free(fs->alloc);
    fs->alloc = NULL;
error:
    return -1;
}

error_t reiserfs_alloc_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-138", fs != NULL, return -1);
    aal_assert("umka-139", fs->alloc != NULL, return -1);

    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, sync, return -1);
    return fs->alloc->plugin->alloc.sync(fs->alloc->entity);
}

#endif

void reiserfs_alloc_close(reiserfs_fs_t *fs) {
    aal_assert("umka-140", fs != NULL, return);
    aal_assert("umka-141", fs->alloc != NULL, return);
	
    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, close, return);
    fs->alloc->plugin->alloc.close(fs->alloc->entity);
    
    aal_free(fs->alloc);
    fs->alloc = NULL;
}

count_t reiserfs_alloc_free(reiserfs_fs_t *fs) {
    aal_assert("umka-361", fs != NULL, return 0);
    aal_assert("umka-362", fs->alloc != NULL, return 0);
	
    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, free, return 0);
    return fs->alloc->plugin->alloc.free(fs->alloc->entity);
}

count_t reiserfs_alloc_used(reiserfs_fs_t *fs) {
    aal_assert("umka-361", fs != NULL, return 0);
    aal_assert("umka-362", fs->alloc != NULL, return 0);
	
    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, used, return 0);
    return fs->alloc->plugin->alloc.used(fs->alloc->entity);
}

void reiserfs_alloc_use(reiserfs_fs_t *fs, blk_t blk) {
    aal_assert("umka-361", fs != NULL, return);
    aal_assert("umka-362", fs->alloc != NULL, return);

    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, use, return);
    fs->alloc->plugin->alloc.use(fs->alloc->entity, blk);
}

void reiserfs_alloc_unuse(reiserfs_fs_t *fs, blk_t blk) {
    aal_assert("umka-361", fs != NULL, return);
    aal_assert("umka-362", fs->alloc != NULL, return);

    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, unuse, return);
    fs->alloc->plugin->alloc.unuse(fs->alloc->entity, blk);
}

blk_t reiserfs_alloc_find(reiserfs_fs_t *fs) {
    aal_assert("umka-361", fs != NULL, return 0);
    aal_assert("umka-362", fs->alloc != NULL, return 0);

    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, find, return 0);
    return fs->alloc->plugin->alloc.find(fs->alloc->entity, 0);
}

