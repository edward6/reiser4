/*
    filesystem.c -- common reiserfs filesystem code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

static error_t reiserfs_master_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t 
    format_plugin_id, unsigned int blocksize, const char *uuid, const char *label) 
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
	
    set_mr_format_id(fs->master, format_plugin_id);
    set_mr_block_size(fs->master, blocksize);
	
    return 0;
}

#endif

static error_t reiserfs_master_init(reiserfs_fs_t *fs) {
    blk_t master_offset;
    aal_block_t *block;
    reiserfs_master_t *master;
    
    aal_assert("umka-143", fs != NULL, return -1);
    
    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);
    aal_device_set_blocksize(fs->host_device, REISERFS_DEFAULT_BLOCKSIZE);
	
    if (!(block = aal_device_read_block(fs->host_device, master_offset))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't read master super block at %llu.", master_offset);
	return -1;
    }
    
    master = (reiserfs_master_t *)block->data;

    /* Checking for reiser3 disk-format */
    if (aal_strncmp(master->mr_magic, REISERFS_MASTER_MAGIC, 4) != 0) {
#ifndef ENABLE_COMPACT    
	reiserfs_plugin_t *format36;
	
	if (!(format36 = libreiser4_plugins_find_by_coords(REISERFS_FORMAT_PLUGIN, 0x1)))
	    goto error_free_block;
	
	if (!libreiserfs_plugins_call(goto error_free_block, format36->format, confirm, 
		fs->host_device))
	    goto error_free_block;
		
	/* Forming in memory master super block for reiser3 */
	if (reiserfs_master_create(fs, 0x1, aal_device_get_blocksize(fs->host_device), "", "")) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create in-memory "
		"master super block in order to initialize reiser3 filesystem.");
	    goto error_free_block;
	}
#endif	
    } else {
	if (!(fs->master = aal_calloc(sizeof(*master), 0)))
	    goto error_free_block;
	
	aal_memcpy(fs->master, master, sizeof(*master));
	
	if (aal_device_set_blocksize(fs->host_device, get_mr_block_size(master))) {
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
		"Invalid block size detected %u. It must be power of two.", 
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
    if (!(block = aal_device_alloc_block(fs->host_device, master_offset, 0)))
	return -1;
    
    aal_memcpy(block->data, fs->master, REISERFS_DEFAULT_BLOCKSIZE);
    if (aal_device_write_block(fs->host_device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize master super block at %llu.", 
	    master_offset);
	return -1;
    }

    return 0;
}

#endif

static void reiserfs_master_fini(reiserfs_fs_t *fs) {
    aal_assert("umka-146", fs != NULL, return);
    aal_assert("umka-147", fs->master != NULL, return);

    aal_free(fs->master);
}

reiserfs_fs_t *reiserfs_fs_init(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay) 
{
    reiserfs_fs_t *fs;
	
    aal_assert("umka-148", host_device != NULL, return NULL);

    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;

    fs->host_device = host_device;
    fs->journal_device = journal_device;

    if (reiserfs_master_init(fs))
	goto error_free_fs;
	    
    if (reiserfs_format_init(fs))
	goto error_free_master;

    if (reiserfs_alloc_init(fs))
	goto error_free_super;
	
    if (journal_device) {
	aal_device_set_blocksize(journal_device, reiserfs_fs_blocksize(fs));

	if (reiserfs_journal_init(fs, replay))
	    goto error_free_alloc;
	
	/* Reopening recent superblock */
	if (replay) {
	    if (reiserfs_format_reinit(fs))
		goto error_free_journal;
	}
    }
    
    if (reiserfs_oid_init(fs))
	goto error_free_journal;
    
    if (reiserfs_tree_init(fs))
	goto error_free_oid;
	
    return fs;

error_free_oid:
    reiserfs_oid_fini(fs);
error_free_journal:
    if (fs->journal)
	reiserfs_journal_fini(fs);
error_free_alloc:
    reiserfs_alloc_fini(fs);
error_free_super:
    reiserfs_format_fini(fs);
error_free_master:
    reiserfs_master_fini(fs);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

#define REISERFS_MIN_SIZE (23 + 100)

reiserfs_fs_t *reiserfs_fs_create(aal_device_t *host_device, 
    reiserfs_profile_t *profile, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    reiserfs_params_opaque_t *journal_params)
{
    reiserfs_fs_t *fs;

    aal_assert("umka-149", host_device != NULL, return NULL);
    aal_assert("umka-150", journal_device != NULL, return NULL);
    aal_assert("vpf-113", profile != NULL, return NULL);

    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Invalid block size %u. It must be power of two.", 
	    blocksize);
	return NULL;
    }

    if (aal_device_len(host_device) < REISERFS_MIN_SIZE) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Device %s is too small (%llu). ReiserFS required device %u blocks long.", 
	    aal_device_name(host_device), aal_device_len(host_device), REISERFS_MIN_SIZE);
	return NULL;
    }
	    
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;
	
    fs->host_device = host_device;
    fs->journal_device = journal_device;

    if (reiserfs_master_create(fs, profile->format, blocksize, uuid, label))    
	goto error_free_fs;

    if (reiserfs_format_create(fs, profile->format, len, journal_params))
	goto error_free_master;

    if (reiserfs_alloc_init(fs))
	goto error_free_super;

    if (reiserfs_journal_init(fs, 0))
	goto error_free_alloc;

    if (reiserfs_oid_init(fs))
	goto error_free_journal;

    if (reiserfs_tree_create(fs, profile))
	goto error_free_oid;
    
    reiserfs_format_set_free(fs, reiserfs_alloc_free(fs));
    return fs;

error_free_oid:
    reiserfs_oid_fini(fs);
error_free_journal:
    reiserfs_journal_fini(fs);
error_free_alloc:
    reiserfs_alloc_fini(fs);
error_free_super:
    reiserfs_format_fini(fs);
error_free_master:
    reiserfs_master_fini(fs);    
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

error_t reiserfs_fs_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-231", fs != NULL, return -1);
   
    if (reiserfs_master_sync(fs))
	return -1;
    
    /* 
	As format is owner of all objects (oid allocator, block allocator),
	journal, it will sync they itself.
    */
    if (reiserfs_format_sync(fs))
	return -1;

    if (reiserfs_tree_sync(fs))
	return -1;
    
    return 0;
}

#endif

/* 
    Closes all filesystem's entities. Calls plugins' "done" 
    routine for every plugin and frees all assosiated memory. 
*/
void reiserfs_fs_fini(reiserfs_fs_t *fs) {
    
    aal_assert("umka-230", fs != NULL, return);
    
    reiserfs_tree_fini(fs);
    
    reiserfs_oid_fini(fs);
    
    if (fs->journal)
	reiserfs_journal_fini(fs);
	
    reiserfs_alloc_fini(fs);
    reiserfs_format_fini(fs);
    reiserfs_master_fini(fs);
    aal_free(fs);
}

const char *reiserfs_fs_format(reiserfs_fs_t *fs) {
    return reiserfs_format_format(fs);
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

