/*
	super.c -- format independent super block code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

#include <reiserfs/debug.h>
#include <reiserfs/reiserfs.h>

static reiserfs_master_super_t *reiserfs_super_fill_master(reiserfs_master_super_t *master, 
	reiserfs_plugin_id_t format, unsigned int blocksize, const char *uuid, 
	const char *label) 
{
	aal_strncpy(master->mr_magic, REISERFS_MASTER_MAGIC, strlen(REISERFS_MASTER_MAGIC));
	aal_strncpy(master->mr_uuid, uuid, strlen(uuid));
	aal_strncpy(master->mr_label, uuid, strlen(label));
	
	set_mr_format_id(master, format);
	set_mr_block_size(master, blocksize);
	
	return master;
}

int reiserfs_super_open(reiserfs_fs_t *fs) {
	aal_block_t *block;
	reiserfs_plugin_t *plugin;
	reiserfs_master_super_t *master;
	
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

	/* Checking for reiser3 disk-format */
	if (aal_strncmp(master->mr_magic, REISERFS_MASTER_MAGIC, 4) != 0) {
		unsigned int blocksize;
		
		if (!(plugin = reiserfs_plugin_find(REISERFS_FORMAT_PLUGIN, 0x2)))
			goto error_free_block;
	
		if (!(blocksize = plugin->format.probe(fs->device)))
			goto error_free_block;
		
		/* Forming in memory master super block for reiser3 */
		reiserfs_super_fill_master(master, 0x2, blocksize, "", "");
	}	
	
	aal_memcpy(&fs->super->master, master, sizeof(*master));
	
	if (!aal_device_set_blocksize(fs->device, get_mr_block_size(master))) {
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-019",
			"Invalid block size detected %d. It must be power of two.", 
			get_mr_block_size(master));
		goto error_free_block;
	}

	if (!(plugin = reiserfs_plugin_find(REISERFS_FORMAT_PLUGIN, 
		get_mr_format_id(master)))) 
	{
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-020", 
			"Can't find disk-format plugin by its identifier %x.", 
			get_mr_format_id(master));
		
		goto error_free_block;
	}
	
	if (!(fs->super->entity = plugin->format.init(fs->device))) {
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-021", 
			"Can't initialize disk-format plugin.");
		goto error_free_block;
	}	
	fs->super->plugin = plugin;
	aal_block_free(block);

	return 1;
	
error_free_block:
	aal_block_free(block);
error_free_super:
	aal_free(fs->super);
	fs->super = NULL;
error:
	return 0;
}

void reiserfs_super_close(reiserfs_fs_t *fs, int sync) {
	ASSERT(fs != NULL, return);
	ASSERT(fs->super != NULL, return);
	
	fs->super->plugin->format.done(fs->super->entity, sync);
	aal_free(fs->super);
	fs->super = NULL;
}

int reiserfs_super_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t format, 
	unsigned int blocksize, const char *uuid, const char *label, count_t len) 
{
	aal_block_t *block;
	reiserfs_plugin_t *plugin;
		
	ASSERT(fs != NULL, return 0);

	if (!(plugin = reiserfs_plugin_find(REISERFS_FORMAT_PLUGIN, format))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-026", 
			"Can't find format plugin by its identifier %x.", format);
		return 0;
	}
	
	if (!(fs->super = aal_calloc(sizeof(*fs->super), 0)))
		return 0;
	
	reiserfs_super_fill_master(&fs->super->master, format, blocksize, uuid, label);
	
	if (!(block = aal_block_alloc_with(fs->device, 
			(blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE), 
			&fs->super->master)))
		goto error_free_super;
	
	if (!aal_block_write(fs->device, block)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-027", 
			"Can't create master super block on the device.");
		aal_block_free(block);
		goto error_free_super;
	}
	
	aal_block_free(block);
	
	/* Creating specified disk-format and format-specific superblock */
	if (!(fs->super->entity = plugin->format.create(fs->device))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-028", 
			"Can't create disk-format for %s format.", plugin->h.label);
		goto error_free_super;
	}
	fs->super->plugin = plugin;
	
	return 1;

error_free_super:
	aal_free(fs->super);
	fs->super = NULL;
error:
	return 0;
}
	
const char *reiserfs_super_format(reiserfs_fs_t *fs) {
	ASSERT(fs != NULL, return NULL);
	ASSERT(fs->super != NULL, return NULL);
	
	return fs->super->plugin->format.format(fs->super->entity);
}

unsigned int reiserfs_super_blocksize(reiserfs_fs_t *fs) {
	ASSERT(fs != NULL, return 0);
	ASSERT(fs->super != NULL, return 0);
	
	return get_mr_block_size(&fs->super->master);
}

reiserfs_plugin_id_t reiserfs_super_journal_plugin(reiserfs_fs_t *fs) {

	ASSERT(fs != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	ASSERT(fs->super != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	
	return fs->super->plugin->format.journal_plugin_id();
}

reiserfs_plugin_id_t reiserfs_super_alloc_plugin(reiserfs_fs_t *fs) {

	ASSERT(fs != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	ASSERT(fs->super != NULL, return REISERFS_UNSUPPORTED_PLUGIN);
	
	return fs->super->plugin->format.alloc_plugin_id();
}

