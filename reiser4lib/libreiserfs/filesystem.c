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

	if (reiserfs_super_journal_plugin(fs) != -1 && journal_device && 
			!reiserfs_journal_open(fs, journal_device, replay))
		goto error_free_super;
	
	if (reiserfs_super_alloc_plugin(fs) != -1 && !reiserfs_alloc_open(fs))
		goto error_free_journal;
	
	if (!reiserfs_tree_open(fs))
		goto error_free_alloc;
	
	return fs;

error_free_alloc:
	reiserfs_alloc_close(fs, 0);
error_free_journal:
	if (fs->journal)
		reiserfs_journal_close(fs, 0);
error_free_super:
	reiserfs_super_close(fs, 0);
error_free_fs:
	aal_free(fs);
error:
	return NULL;
}

reiserfs_fs_t *reiserfs_fs_create(aal_device_t *host_device, reiserfs_plugin_id_t format, 
	unsigned int blocksize, const char *uuid, const char *label, count_t len, 
	aal_device_t *journal_device, reiserfs_params_opaque_t *journal_params)
{
	reiserfs_fs_t *fs;

	ASSERT(host_device != NULL, return NULL);
	ASSERT(journal_device != NULL, return NULL);

	if (!aal_pow_of_two(blocksize)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-025", 
			"Invalid block size %d. It must be power of two.", blocksize);
		return NULL;
	}
	
	if (!(fs = aal_calloc(sizeof(*fs), 0)))
		return NULL;
	
	fs->device = host_device;
	
	if (!reiserfs_super_create(fs, format, blocksize, uuid, label, len))
		goto error_free_fs;
	
	if (!reiserfs_journal_create(fs, journal_device, journal_params))
		goto error_free_super;
	
	if (!reiserfs_alloc_create(fs))
		goto error_free_journal;
	
	if (!reiserfs_tree_create(fs))
		goto error_free_alloc;
	
	return fs;

error_free_alloc:
	reiserfs_alloc_close(fs, 0);
error_free_journal:
	reiserfs_journal_close(fs, 0);
error_free_super:
	reiserfs_super_close(fs, 0);
error_free_fs:
	aal_free(fs);
error:
	return NULL;
}

/* 
	Closes all filesystem's entities. Calls plugins' "done" 
	routine for every plugin and frees all assosiated memory. 
*/
void reiserfs_fs_close(reiserfs_fs_t *fs, int sync) {
	reiserfs_tree_close(fs, sync);
	reiserfs_alloc_close(fs, sync);
	
	if (fs->journal)
		reiserfs_journal_close(fs, sync);
	
	reiserfs_super_close(fs, sync);
	aal_free(fs);
}

const char *reiserfs_fs_format(reiserfs_fs_t *fs) {
	return reiserfs_super_format(fs);
}

unsigned int reiserfs_fs_blocksize(reiserfs_fs_t *fs) {
	return reiserfs_super_blocksize(fs);
}

