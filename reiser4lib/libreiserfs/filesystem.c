/*
	filesystem.c -- common reiserfs filesystem code.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
	aal_device_t *journal_device, int replay) 
{
	reiserfs_fs_t *fs;
	
	ASSERT(host_device != NULL, return NULL);

	if (!(fs = aal_calloc(sizeof(*fs), 0)))
		return NULL;

	fs->device = host_device;
	
	if (!reiserfs_super_open(fs))
		goto error_free_fs;

	if (journal_device)
		aal_device_set_blocksize(journal_device, get_mr_block_size(&fs->super->master));

	if (reiserfs_super_journal_plugin(fs) != REISERFS_UNSUPPORTED_PLUGIN && 
			journal_device && !reiserfs_journal_open(fs, journal_device, replay))
		goto error_free_super;
	
	if (reiserfs_super_alloc_plugin(fs) != REISERFS_UNSUPPORTED_PLUGIN &&
			!reiserfs_alloc_open(fs))
		goto error_free_journal;
	
	if (!reiserfs_tree_open(fs))
		goto error_free_alloc;
	
	return fs;

error_free_alloc:
	reiserfs_alloc_close(fs);
error_free_journal:
	reiserfs_journal_close(fs);
error_free_super:
	reiserfs_super_close(fs);
error_free_fs:
	aal_free(fs);
error:
	return NULL;
}

/* 
	Closes all filesystem's entities. Calls plugins' "done" 
	routine for every plugin and frees all assosiated memory. 
*/
void reiserfs_fs_close(reiserfs_fs_t *fs) {
	reiserfs_tree_close(fs);
	reiserfs_alloc_close(fs);
	reiserfs_journal_close(fs);
	reiserfs_super_close(fs);
	aal_free(fs);
}

