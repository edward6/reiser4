/*
    alloc.c -- reiserfs block allocator common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

int reiserfs_alloc_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
	
    ASSERT(fs != NULL, return 0);
    ASSERT(fs->super != NULL, return 0);
	
    if (fs->alloc) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-017",
	    "Block allocator already initialized.");
	return 0;
    }
	
    if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
	return 0;
	
    id = reiserfs_super_alloc_plugin(fs);
    if (!(plugin = reiserfs_plugin_find(REISERFS_ALLOC_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-018",
	    "Can't find block allocator plugin by its identifier %x.", id);
	goto error_free_alloc;
    }

    fs->alloc->plugin = plugin;

    reiserfs_plugin_check_routine(plugin->alloc, open, goto error_free_alloc);
    if (!(fs->alloc->entity = plugin->alloc.open(fs->device))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-019",
	    "Can't initialize block allocator plugin.");
	goto error_free_alloc;
    }
	
    return 1;
	
error_free_alloc:
    aal_free(fs->alloc);
    fs->alloc = NULL;
error:
    return 0;
}

int reiserfs_alloc_create(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_id_t id;
	
    ASSERT(fs != NULL, return 0);
	
    if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
	return 0;
	
    id = reiserfs_super_alloc_plugin(fs);
    if (!(plugin = reiserfs_plugin_find(REISERFS_ALLOC_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-030",
	    "Can't find block allocator plugin by its identifier %x.", id);
	goto error_free_alloc;
    }
    fs->alloc->plugin = plugin;

    reiserfs_plugin_check_routine(plugin->alloc, create, goto error_free_alloc);
    if (!(fs->alloc->entity = plugin->alloc.create(fs->device))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-031",
	    "Can't allocator.");
	goto error_free_alloc;
    }
	
    return 1;
	
error_free_alloc:
    aal_free(fs->alloc);
    fs->alloc = NULL;
error:
    return 0;
}

int reiserfs_alloc_sync(reiserfs_fs_t *fs) {
    ASSERT(fs != NULL, return 0);
    ASSERT(fs->alloc != NULL, return 0);

    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, sync, return 0);
    return fs->alloc->plugin->alloc.sync(fs->alloc->entity);
}

void reiserfs_alloc_close(reiserfs_fs_t *fs, int sync) {
    ASSERT(fs != NULL, return);
    ASSERT(fs->alloc != NULL, return);
	
    reiserfs_plugin_check_routine(fs->alloc->plugin->alloc, close, return);
    fs->alloc->plugin->alloc.close(fs->alloc->entity, sync);
    aal_free(fs->alloc);
    fs->alloc = NULL;
}

