/*
    filesystem.c -- common reiserfs filesystem code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

#ifndef ENABLE_COMPACT

static error_t reiserfs_master_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t plugin_id, 
    unsigned int blocksize, const char *uuid, const char *label) 
{
    aal_assert("umka-142", fs != NULL, return -1);
    
    if (!(fs->master = aal_calloc(REISERFS_DEFAULT_BLOCKSIZE, 0)))
	return -1;
    
    aal_strncpy(fs->master->mr_magic, REISERFS_MASTER_MAGIC, 
	aal_strlen(REISERFS_MASTER_MAGIC));
    
    if (uuid)
	aal_strncpy(fs->master->mr_uuid, uuid, sizeof(fs->master->mr_uuid));

    if (label)
	aal_strncpy(fs->master->mr_label, label, sizeof(fs->master->mr_label));
	
    set_mr_format_id(fs->master, plugin_id);
    set_mr_block_size(fs->master, blocksize);
	
    return 0;
}

#endif

static error_t reiserfs_master_open(reiserfs_fs_t *fs) {
    blk_t master_offset;
    aal_block_t *block;
    reiserfs_master_t *master;
    
    aal_assert("umka-143", fs != NULL, return -1);
    
    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);
    aal_device_set_blocksize(fs->device, REISERFS_DEFAULT_BLOCKSIZE);
	
    if (!(block = aal_device_read_block(fs->device, master_offset))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't read master super block at %d.", master_offset);
	return -1;
    }
    
    master = (reiserfs_master_t *)block->data;

    /* Checking for reiser3 disk-format */
    if (aal_strncmp(master->mr_magic, REISERFS_MASTER_MAGIC, 4) != 0) {
#ifndef ENABLE_COMPACT    
	reiserfs_plugin_t *format36;
	
	if (!(format36 = reiserfs_plugins_find_by_coords(REISERFS_FORMAT_PLUGIN, 0x2)))
	    goto error_free_block;
		
	reiserfs_plugin_check_routine(format36->format, probe, goto error_free_block);
	if (!format36->format.probe(fs->device))
	    goto error_free_block;
		
	/* Forming in memory master super block for reiser3 */
	if (reiserfs_master_create(fs, 0x2, aal_device_get_blocksize(fs->device), "", "")) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create in-memory "
		"master super block in order to open reiser3 filesystem.");
	    goto error_free_block;
	}
#endif	
    } else {
	if (!(fs->master = aal_calloc(sizeof(*master), 0)))
	    goto error_free_block;
	
	aal_memcpy(fs->master, master, sizeof(*master));
	
	if (aal_device_set_blocksize(fs->device, get_mr_block_size(master))) {
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
		"Invalid block size detected %d. It must be power of two.", 
		get_mr_block_size(master));
	    
	    aal_free(fs->master);
	    goto error_free_block;
	}
    }
    
    return 0;
    
error_free_block:
    aal_device_free_block(block);
error:
    return -1;    
}

#ifndef ENABLE_COMPACT

static error_t reiserfs_master_sync(reiserfs_fs_t *fs) {
    blk_t master_offset;	
    aal_block_t *block;
	
    aal_assert("umka-144", fs != NULL, return -1);
    aal_assert("umka-145", fs->master != NULL, return -1);

    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);
    if (!(block = aal_device_alloc_block(fs->device, master_offset, 0)))
	return -1;
    
    aal_memcpy(block->data, fs->master, REISERFS_DEFAULT_BLOCKSIZE);
    if (aal_device_write_block(fs->device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize master super block at %d.", 
	    master_offset);
	return -1;
    }

    return 0;
}

#endif

static void reiserfs_master_close(reiserfs_fs_t *fs) {
    aal_assert("umka-146", fs != NULL, return);
    aal_assert("umka-147", fs->master != NULL, return);

    aal_free(fs->master);
}

reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay) 
{
    reiserfs_fs_t *fs;
	
    aal_assert("umka-148", host_device != NULL, return NULL);

    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;

    fs->device = host_device;
    
    if (reiserfs_master_open(fs))
	goto error_free_fs;
	    
    if (reiserfs_super_open(fs))
	goto error_free_master;

    if (reiserfs_alloc_open(fs))
	goto error_free_super;
	
    if (journal_device) {
	aal_device_set_blocksize(journal_device, reiserfs_fs_blocksize(fs));

	if (reiserfs_super_journal_plugin(fs) != -1 && 
		reiserfs_journal_open(fs, journal_device, replay))
	    goto error_free_alloc;
    }
	
    if (reiserfs_tree_open(fs))
	goto error_free_journal;
	
    return fs;

error_free_journal:
    if (fs->journal)
	reiserfs_journal_close(fs);
error_free_alloc:
    reiserfs_alloc_close(fs);
error_free_super:
    reiserfs_super_close(fs);
error_free_master:
    reiserfs_master_close(fs);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_fs_t *reiserfs_fs_create(aal_device_t *host_device, 
    reiserfs_plugin_id_t format_plugin_id, reiserfs_plugin_id_t alloc_plugin_id,
    reiserfs_plugin_id_t node_plugin_id, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    reiserfs_params_opaque_t *journal_params)
{
    reiserfs_fs_t *fs;

    aal_assert("umka-149", host_device != NULL, return NULL);
    aal_assert("umka-150", journal_device != NULL, return NULL);

    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Invalid block size %d. It must be power of two.", 
	    blocksize);
	return NULL;
    }
	
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;
	
    fs->device = host_device;
    
    if (reiserfs_master_create(fs, format_plugin_id, blocksize, uuid, label))    
	goto error_free_fs;
	    
    if (reiserfs_alloc_create(fs, alloc_plugin_id, len))
	goto error_free_master;
    
    if (reiserfs_super_create(fs, format_plugin_id, len))
	goto error_free_alloc;
	
    if (reiserfs_journal_create(fs, journal_device, journal_params))
	goto error_free_super;
	
    if (reiserfs_tree_create(fs, node_plugin_id))
	goto error_free_journal;
    
    /* Setting up free blocks value to format-specific super block */
    reiserfs_plugin_check_routine(fs->super->plugin->format, set_free, goto error_free_journal);
    fs->super->plugin->format.set_free(fs->super->entity, reiserfs_alloc_free(fs));

    return fs;

error_free_journal:
    reiserfs_journal_close(fs);
error_free_super:
    reiserfs_super_close(fs);
error_free_alloc:
    reiserfs_alloc_close(fs);
error_free_master:
    reiserfs_master_close(fs);    
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

error_t reiserfs_fs_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-231", fs != NULL, return -1);
   
    if (reiserfs_master_sync(fs))
	return -1;

    if (reiserfs_super_sync(fs))
	return -1;

    if (fs->journal && reiserfs_journal_sync(fs))
	return -1;

    if (reiserfs_alloc_sync(fs))
	return -1;

/*    if (reiserfs_tree_sync(fs))
	return -1;*/
    
    return 0;
}

#endif

/* 
    Closes all filesystem's entities. Calls plugins' "done" 
    routine for every plugin and frees all assosiated memory. 
*/
void reiserfs_fs_close(reiserfs_fs_t *fs) {
    
    aal_assert("umka-230", fs != NULL, return);
    
/*    reiserfs_tree_close(fs);
    if (fs->journal)
	reiserfs_journal_close(fs);*/
	
    reiserfs_alloc_close(fs);
    reiserfs_super_close(fs);
    reiserfs_master_close(fs);
    aal_free(fs);
}

const char *reiserfs_fs_format(reiserfs_fs_t *fs) {
    return reiserfs_super_format(fs);
}

reiserfs_plugin_id_t reiserfs_fs_format_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-151", fs != NULL, return -1);
    aal_assert("umka-152", fs->master != NULL, return -1);

    return get_mr_format_id(fs->master);
}

uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs) {
    aal_assert("umka-153", fs != NULL, return 0);
    aal_assert("umka-154", fs->master != NULL, return 0);
    
    return get_mr_block_size(fs->master);
}

