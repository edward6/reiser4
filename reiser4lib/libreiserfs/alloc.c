/*
	alloc.c -- reiserfs block allocator common code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

int reiserfs_alloc_open(reiserfs_fs_t *fs) {
	reiserfs_plugin_id_t id;
	reiserfs_plugin_t *plugin;
	
	ASSERT(fs != NULL, return 0);
	ASSERT(fs->super != NULL, return 0);
	
	if (fs->alloc) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-015",
			"Block allocator already initialized.");
		return 0;
	}
	
	if (!(fs->alloc = aal_calloc(sizeof(*fs->alloc), 0)))
		return 0;
	
	id = reiserfs_super_alloc_plugin(fs);
	if (!(plugin = reiserfs_plugin_find(REISERFS_ALLOC_PLUGIN, id))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-016",
			"Can't find block allocator plugin by its identifier %x.", id);
		goto error_free_alloc;
	}

	fs->alloc->plugin = plugin;

	if ((fs->alloc->entity = plugin->alloc.init(fs->device))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-017",
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

void reiserfs_alloc_close(reiserfs_fs_t *fs, int sync) {
	ASSERT(fs != NULL, return);
	ASSERT(fs->alloc != NULL, return);
	
	fs->alloc->plugin->alloc.done(fs->alloc->entity, sync);
	aal_free(fs->alloc);
	fs->alloc = NULL;
}

