/*
	super.c -- format independent super block code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/debug.h>
#include <reiserfs/reiserfs.h>

extern aal_list_t *plugins;

int reiserfs_super_open(reiserfs_fs_t *fs) {
	aal_block_t *block;
	reiserfs_plugin_t *plugin;
	struct reiserfs_master_super *master;
	
	ASSERT(fs != NULL, return 0);
	ASSERT(fs->device != NULL, return 0);
	
	if (fs->super) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-007", 
			"Super block already opened.");
		return 0;
	}
	
	if (!(fs->super = aal_calloc(sizeof(*fs->super), 0)))
		return 0;
	
	aal_device_set_blocksize(fs->device, REISERFS_DEFAULT_BLOCKSIZE);
	
	if (!(block = aal_block_read(fs->device, 
		(blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE))))
	{
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-018", 
			"Can't read master super block.");
		goto error_free_super;
	}
	
	master = (struct reiserfs_master_super *)block->data;
	if (aal_strncmp(master->mr_magic, REISERFS_MASTER_MAGIC, 4) != 0)
		goto error_free_block;
	
	aal_memcpy(&fs->super->master, master, sizeof(*master));
	
	if (!aal_device_set_blocksize(fs->device, get_mr_blocksize(master))) {
			aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-019",
					"Invalid block size detected %d. It must be power of two.", 
			get_mr_blocksize(master));
		goto error_free_block;
	}
	
	if (!(plugin = reiserfs_plugin_find(REISERFS_FORMAT_PLUGIN, 
		get_mr_format_id(master)))) 
	{
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-020", 
			"Can't find disk-format plugin by its identifier %d.", get_mr_format_id(master));
		goto error_free_block;
	}
	
	aal_block_free(block);
	
	if (!(fs->super->entity = plugin->format.init(fs->device))) {
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-021", 
			"Can't initialize disk-format plugin.");
		goto error_free_block;
	}	
	fs->super->plugin = plugin;
	
	return 1;
	
error_free_block:
	aal_block_free(block);
error_free_super:
	aal_free(fs->super);
	fs->super = NULL;
error:
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

