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

static error_t reiserfs_master_open(reiserfs_fs_t *fs) {
    blk_t master_offset;
    aal_block_t *block;
    reiserfs_master_t *master;
    
    aal_assert("umka-143", fs != NULL, return -1);
    
    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);
    aal_device_set_bs(fs->host_device, REISERFS_DEFAULT_BLOCKSIZE);
	
    if (!(block = aal_block_read(fs->host_device, master_offset))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't read master super block at %llu.", master_offset);
	return -1;
    }
    
    master = (reiserfs_master_t *)block->data;

    /* Checking for reiser3 disk-format */
    if (aal_strncmp(master->mr_magic, REISERFS_MASTER_MAGIC, 4) != 0) {
#ifndef ENABLE_COMPACT    
	reiserfs_plugin_t *format36;
	
	if (!(format36 = libreiser4_factory_find_by_coord(REISERFS_FORMAT_PLUGIN, 0x1))) {
    	    libreiser4_factory_find_failed(REISERFS_FORMAT_PLUGIN, 0x1,
		goto error_free_block);
	}
	
	if (!libreiser4_plugin_call(goto error_free_block, format36->format, confirm, 
		fs->host_device))
	    goto error_free_block;
		
	/* Forming in memory master super block for reiser3 */
	if (reiserfs_master_create(fs, 0x1, aal_device_get_bs(fs->host_device), "", "")) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create in-memory "
		"master super block in order to initialize reiser3 filesystem.");
	    goto error_free_block;
	}
#endif	
    } else {
	if (!(fs->master = aal_calloc(sizeof(*master), 0)))
	    goto error_free_block;
	
	aal_memcpy(fs->master, master, sizeof(*master));
	
	if (aal_device_set_bs(fs->host_device, get_mr_block_size(master))) {
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
		"Invalid block size detected %u. It must be power of two.", 
		get_mr_block_size(master));
	    
	    aal_free(fs->master);
	    goto error_free_block;
	}
    }
    
    return 0;
    
error_free_block:
    aal_block_free(block);
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
    if (!(block = aal_block_alloc(fs->host_device, master_offset, 0)))
	return -1;
    
    aal_memcpy(block->data, fs->master, REISERFS_DEFAULT_BLOCKSIZE);
    if (aal_block_write(fs->host_device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize master super block at %llu.", 
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

error_t reiserfs_fs_build_root_key(reiserfs_fs_t *fs, 
    reiserfs_key_t *key, reiserfs_plugin_id_t key_plugin_id) 
{
    oid_t root_objectid;
    oid_t root_parent_objectid;
    reiserfs_plugin_t *key_plugin;
    
    if (!(key_plugin = libreiser4_factory_find_by_coord(REISERFS_KEY_PLUGIN,
	key_plugin_id)))
    {
	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, key_plugin_id,
	    return -1);
    }

    root_parent_objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid, root_parent_objectid,);

    root_objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid, root_objectid,);

    reiserfs_key_init(key, key_plugin);
    reiserfs_key_build_file_key(key, KEY40_STATDATA_MINOR,
	root_parent_objectid, root_objectid, 0);

    return 0;
}

reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay) 
{
    count_t len;
    reiserfs_fs_t *fs;
    reiserfs_key_t root_key;

    reiserfs_plugin_id_t oid_plugin_id;
    reiserfs_plugin_id_t format_plugin_id;
    reiserfs_plugin_id_t alloc_plugin_id;
    reiserfs_plugin_id_t journal_plugin_id;

    void *oid_area_start, *oid_area_end;
	
    aal_assert("umka-148", host_device != NULL, return NULL);

    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;

    fs->host_device = host_device;
    fs->journal_device = journal_device;

    if (reiserfs_master_open(fs))
	goto error_free_fs;
	    
    format_plugin_id = get_mr_format_id(fs->master);
    if (!(fs->format = reiserfs_format_open(host_device, format_plugin_id)))
	goto error_free_master;

    alloc_plugin_id = reiserfs_format_alloc_plugin_id(fs->format);
    journal_plugin_id = reiserfs_format_journal_plugin_id(fs->format);
    oid_plugin_id = reiserfs_format_oid_plugin_id(fs->format);
   
    len = reiserfs_format_get_blocks(fs->format);
    
    if (!(fs->alloc = reiserfs_alloc_open(host_device, len, alloc_plugin_id)))
	goto error_free_super;
	
    if (journal_device) {
	aal_device_set_bs(journal_device, reiserfs_fs_blocksize(fs));

	if (!(fs->journal = reiserfs_journal_open(journal_device, journal_plugin_id)))
	    goto error_free_alloc;
	
	/* Reopening recent superblock */
	if (replay) {
	    if (reiserfs_journal_replay(fs->journal)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't replay journal.");
		goto error_free_journal;
	    }
	    if (!(fs->format = reiserfs_format_reopen(fs->format, host_device)))
		goto error_free_journal;
	}
    }
    
    libreiser4_plugin_call(goto error_free_journal, fs->format->plugin->format, 
	oid, fs->format->entity, &oid_area_start, &oid_area_end);
    
    if (!(fs->oid = reiserfs_oid_open(oid_area_start, oid_area_end, oid_plugin_id)))
	goto error_free_journal;
  
    if (reiserfs_fs_build_root_key(fs, &root_key, 0x0))
	goto error_free_oid;
    
    if (!(fs->tree = reiserfs_tree_open(host_device, fs->alloc, 
	    reiserfs_format_get_root(fs->format), &root_key)))
	goto error_free_oid;
	
    return fs;

error_free_oid:
    reiserfs_oid_close(fs->oid);
error_free_journal:
    if (fs->journal)
	reiserfs_journal_close(fs->journal);
error_free_alloc:
    reiserfs_alloc_close(fs->alloc);
error_free_super:
    reiserfs_format_close(fs->format);
error_free_master:
    reiserfs_master_close(fs);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

#define REISERFS_MIN_SIZE (23 + 100)

reiserfs_fs_t *reiserfs_fs_create(reiserfs_profile_t *profile, 
    aal_device_t *host_device, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    reiserfs_opaque_t *journal_params)
{
    reiserfs_fs_t *fs;
    blk_t blk, master_blk;
    reiserfs_node_t *root_node;
    reiserfs_object_t *root_dir;
    void *oid_area_start, *oid_area_end;
    blk_t journal_area_start, journal_area_end;

    reiserfs_key_t root_key;

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

    if (!(fs->format = reiserfs_format_create(host_device, len, profile->format)))
	goto error_free_master;

    if (!(fs->alloc = reiserfs_alloc_create(host_device, len, profile->alloc)))
	goto error_free_format;

    /* 
	Marking the skiped area and master super block 
	(0-16 blocks) as used.
    */
    master_blk = (blk_t)(REISERFS_MASTER_OFFSET/aal_device_get_bs(host_device));
    for (blk = 0; blk <= master_blk; blk++)
	reiserfs_alloc_mark(fs->alloc, blk);
    
    /* Marking format-specific super blocks as used */
    reiserfs_alloc_mark(fs->alloc, reiserfs_format_offset(fs->format));
    
    if (!(fs->journal = reiserfs_journal_create(journal_device, 
	    journal_params, profile->journal)))
	goto error_free_alloc;
   
    libreiser4_plugin_call(goto error_free_journal, fs->journal->plugin->journal, 
	area, fs->journal->entity, &journal_area_start, &journal_area_end);
    
    for (blk = journal_area_start; blk <= journal_area_end; blk++)
	reiserfs_alloc_mark(fs->alloc, blk);
   
    libreiser4_plugin_call(goto error_free_journal, fs->format->plugin->format, 
	oid, fs->format->entity, &oid_area_start, &oid_area_end);
    
    if (!(fs->oid = reiserfs_oid_create(oid_area_start, oid_area_end, 
	    profile->oid)))
	goto error_free_journal;

    if (reiserfs_fs_build_root_key(fs, &root_key, profile->key))
	goto error_free_oid;
    
    if (!(fs->tree = reiserfs_tree_create(host_device, fs->alloc, 
	    &root_key, profile)))
	goto error_free_oid;
    
    root_node = reiserfs_tree_root_node(fs->tree);
    reiserfs_format_set_root(fs->format, aal_block_get_nr(root_node->block));

    {
	reiserfs_key_t *root_key;
	reiserfs_plugin_t *dir_plugin;
	
	if (!(dir_plugin = libreiser4_factory_find_by_coord(REISERFS_DIR_PLUGIN, profile->dir)))
	    libreiser4_factory_find_failed(REISERFS_DIR_PLUGIN, profile->dir, goto error_free_tree);

	if (!(root_dir = reiserfs_object_create(fs, NULL, dir_plugin, profile))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create root directory.");
	    goto error_free_tree;
	}
		
	reiserfs_object_close(root_dir);
    }
    
    reiserfs_format_set_free(fs->format, reiserfs_alloc_free(fs->alloc));
    
    return fs;

error_free_tree:
    reiserfs_tree_close(fs->tree);
error_free_oid:
    reiserfs_oid_close(fs->oid);
error_free_journal:
    reiserfs_journal_close(fs->journal);
error_free_alloc:
    reiserfs_alloc_close(fs->alloc);
error_free_format:
    reiserfs_format_close(fs->format);
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
    
    if (reiserfs_alloc_sync(fs->alloc))
	return -1;
    
    if (reiserfs_journal_sync(fs->journal))
	return -1;
    
    if (reiserfs_oid_sync(fs->oid))
	return -1;
    
    if (reiserfs_format_sync(fs->format))
	return -1;

    if (reiserfs_tree_sync(fs->tree))
	return -1;
    
    return 0;
}

#endif

/* 
    Closes all filesystem's entities. Calls plugins' "done" 
    routine for every plugin and frees all assosiated memory. 
*/
void reiserfs_fs_close(reiserfs_fs_t *fs) {
    
    aal_assert("umka-230", fs != NULL, return);
    
    reiserfs_tree_close(fs->tree);
    
    reiserfs_oid_close(fs->oid);
    
    if (fs->journal)
	reiserfs_journal_close(fs->journal);
	
    reiserfs_alloc_close(fs->alloc);
    reiserfs_format_close(fs->format);
    reiserfs_master_close(fs);
    aal_free(fs);
}

const char *reiserfs_fs_format(reiserfs_fs_t *fs) {
    return reiserfs_format_format(fs->format);
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

