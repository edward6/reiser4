/*
	super.c -- format independent super block code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/debug.h>
#include <reiserfs/reiserfs.h>

extern aal_list_t *plugins;

int reiserfs_super_open(reiserfs_fs_t *fs) {
	int i;
	reiserfs_plugin_t *plugin;
	
	ASSERT(fs != NULL, return 0);
	ASSERT(fs->device != NULL, return 0);
	
	if (fs->super) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-007", 
			"Super block already opened.");
		return 0;
	}
	
	if (!(fs->super = aal_calloc(sizeof(*fs->super), 0)))
		return 0;
	
	for (i = 0; i < aal_list_count(plugins); i++) {
		plugin = (reiserfs_plugin_t *)aal_list_at(plugins, i);
		
		if (plugin->h.type != REISERFS_FORMAT_PLUGIN)
			continue;
		
		if ((fs->super->entity = plugin->format.init(fs->device))) {
			fs->super->plugin = plugin;
			return 1;
		}	
	}
	
	aal_free(fs->super);
	fs->super = NULL;
	
	return 0;
}

void reiserfs_super_close(reiserfs_fs_t *fs) {
	ASSERT(fs != NULL, return);
	
	fs->super->plugin->format.done(fs->super->entity);
	aal_free(fs->super);
	fs->super = NULL;
}

reiserfs_plugin_id_t reiserfs_super_journal_plugin(reiserfs_fs_t *fs) {

	ASSERT(fs != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	ASSERT(fs->super != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	
	return fs->super->plugin->format.journal_plugin_id(fs->super->entity);
}

reiserfs_plugin_id_t reiserfs_super_alloc_plugin(reiserfs_fs_t *fs) {

	ASSERT(fs != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	ASSERT(fs->super != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	
	return fs->super->plugin->format.alloc_plugin_id(fs->super->entity);
}

