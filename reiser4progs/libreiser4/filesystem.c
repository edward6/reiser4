/*
    filesystem.c -- common reiser4 filesystem code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

/* Forms master super blocks */
static errno_t reiserfs_master_create(reiserfs_fs_t *fs, reiserfs_id_t format_pid, 
    unsigned int blocksize, const char *uuid, const char *label) 
{
    aal_assert("umka-142", fs != NULL, return -1);
    
    /* Allocating the memory */
    if (!(fs->master = aal_calloc(REISERFS_DEFAULT_BLOCKSIZE, 0)))
	return -1;
    
    /* Copying magic */
    aal_strncpy(fs->master->mr_magic, REISERFS_MASTER_MAGIC, 
	aal_strlen(REISERFS_MASTER_MAGIC));
    
    /* Copying uuid and label to corresponding master super block fields */
    if (uuid)
	aal_strncpy(fs->master->mr_uuid, uuid, sizeof(fs->master->mr_uuid));

    if (label)
	aal_strncpy(fs->master->mr_label, label, sizeof(fs->master->mr_label));

    /* Setting plugin id for used disk format plugin */
    set_mr_format_id(fs->master, format_pid);

    /* Setting block filesystem used */
    set_mr_block_size(fs->master, blocksize);
	
    return 0;
}

#endif

/* Reads master super block from disk */
static errno_t reiserfs_master_open(reiserfs_fs_t *fs) {
    blk_t master_offset;
    aal_block_t *block;
    reiserfs_master_t *master;
    
    aal_assert("umka-143", fs != NULL, return -1);
    
    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);

    /* Setting up default block size (4096) to used device */
    aal_device_set_bs(fs->host_device, REISERFS_DEFAULT_BLOCKSIZE);
    
    /* Reading the block where master super block lies */
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
	
	if (!(format36 = libreiser4_factory_find(REISERFS_FORMAT_PLUGIN, 0x1)))
    	    libreiser4_factory_failed(goto error_free_block, find, format, 0x1);
	
	if (!libreiser4_plugin_call(goto error_free_block, 
		format36->format_ops, confirm, fs->host_device))
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
	
	/* Updating master super block in filesystem instance */
	aal_memcpy(fs->master, master, sizeof(*master));
	
	/* Setting actual used block size from master super block */
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

/* Saves master super block to device. */
static errno_t reiserfs_master_sync(reiserfs_fs_t *fs) {
    blk_t master_offset;	
    aal_block_t *block;
	
    aal_assert("umka-144", fs != NULL, return -1);
    aal_assert("umka-145", fs->master != NULL, return -1);

    master_offset = (blk_t)(REISERFS_MASTER_OFFSET / REISERFS_DEFAULT_BLOCKSIZE);
    if (!(block = aal_block_alloc(fs->host_device, master_offset, 0)))
	return -1;
    
    /* 
	Writing master super block to host device. Host device is device where
	filesystem lies. There is also journal device.
    */
    aal_memcpy(block->data, fs->master, REISERFS_DEFAULT_BLOCKSIZE);
    if (aal_block_write(block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize master super block at %llu. %s.", 
	    master_offset, aal_device_error(fs->host_device));
	return -1;
    }

    return 0;
}

#endif

/* Frees master super block occupied memory */
static void reiserfs_master_close(reiserfs_fs_t *fs) {
    aal_assert("umka-146", fs != NULL, return);
    aal_assert("umka-147", fs->master != NULL, return);

    aal_free(fs->master);
}

/*
    Builds root directory key. It is used for any lookups and other. This method id 
    needed because of root key in reiser3 and reiser4 has a diffrent locality and object
    id values.
*/
static errno_t reiserfs_fs_build_root_key(reiserfs_fs_t *fs, 
    reiserfs_id_t pid) 
{
    oid_t objectid;
    oid_t parent_objectid;
    reiserfs_plugin_t *plugin;
    
    /* Finding needed key plugin by its identifier */
    if (!(plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, pid)))
	libreiser4_factory_failed(return -1, find, key, pid);

    /* Getting root directory attributes from oid allocator */
    parent_objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid_ops, root_parent_objectid,);

    objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid_ops, root_objectid,);

    /* Initializing the key by found plugin */
    fs->key.plugin = plugin;

    /* Building the key */
    reiserfs_key_build_generic_full(&fs->key, KEY40_STATDATA_MINOR,
	parent_objectid, objectid, 0);

    return 0;
}

/* 
    Opens filesysetm on specified host device and journal device. Replays the 
    journal if "replay" flag is specified.
*/
reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay) 
{
    count_t len;
    reiserfs_fs_t *fs;

    reiserfs_id_t oid_pid;
    reiserfs_id_t format_pid;
    reiserfs_id_t alloc_pid;
    reiserfs_id_t journal_pid;

    void *oid_area_start, *oid_area_end;
	
    aal_assert("umka-148", host_device != NULL, return NULL);

    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;

    fs->host_device = host_device;
    fs->journal_device = journal_device;

    /* Reads master super block. See above for details */
    if (reiserfs_master_open(fs))
	goto error_free_fs;
    
    /* Initializes used disk format. See format.c for details */
    format_pid = get_mr_format_id(fs->master);
    if (!(fs->format = reiserfs_format_open(host_device, format_pid)))
	goto error_free_master;

    /* Getting plugins in use from disk format object */
    alloc_pid = reiserfs_format_alloc_pid(fs->format);
    journal_pid = reiserfs_format_journal_pid(fs->format);
    oid_pid = reiserfs_format_oid_pid(fs->format);
   
    len = reiserfs_format_get_blocks(fs->format);
    
    /* Initializes block allocator. See alloc.c for details */
    if (!(fs->alloc = reiserfs_alloc_open(host_device, len, alloc_pid)))
	goto error_free_super;
    
    /* Jouranl device may be not specified. In this case it will not be opened */
    if (journal_device) {
	    
	/* Setting up block size in use for journal device */
	aal_device_set_bs(journal_device, reiserfs_fs_blocksize(fs));

	/* Initializing the journal. See  journal.c for details */
	if (!(fs->journal = reiserfs_journal_open(journal_device, journal_pid)))
	    goto error_free_alloc;
	
	/* 
	    Reopening super block after journal replaying. It is needed because
	    journal may contain super block in unflushed transactions.
	*/
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
    
    /* Initializes oid allocator */
    libreiser4_plugin_call(goto error_free_journal, fs->format->plugin->format_ops, 
	oid, fs->format->entity, &oid_area_start, &oid_area_end);
    
    if (!(fs->oid = reiserfs_oid_open(oid_area_start, oid_area_end, oid_pid)))
	goto error_free_journal;
  
    /* 
	Initilaizes root directory key.
	FIXME-UMKA: Here should be not hardcoded key id.
    */
    if (reiserfs_fs_build_root_key(fs, 0x0))
	goto error_free_oid;
    
    /* Opens the tree starting from root block */
    if (!(fs->tree = reiserfs_tree_open(fs)))
	goto error_free_oid;
    
    if (!(fs->dir = reiserfs_dir_open(fs, "/")))
	goto error_free_tree;
    
    return fs;

error_free_tree:
    reiserfs_tree_close(fs->tree);
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

/* Creates filesystem on specified host and journal device */
reiserfs_fs_t *reiserfs_fs_create(reiserfs_profile_t *profile, 
    aal_device_t *host_device, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    void *journal_params)
{
    reiserfs_fs_t *fs;
    blk_t blk, master_blk;
    void *oid_area_start, *oid_area_end;
    blk_t journal_area_start, journal_area_end;

    aal_assert("umka-149", host_device != NULL, return NULL);
    aal_assert("umka-150", journal_device != NULL, return NULL);
    aal_assert("vpf-113", profile != NULL, return NULL);

    /* Makes check for validness of specified block size value */
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Invalid block size %u. It must be power of two.", 
	    blocksize);
	return NULL;
    }

    /* Checks whether filesystem size is enough big */
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

    /* Creates master super block */
    if (reiserfs_master_create(fs, profile->format, blocksize, uuid, label))
	goto error_free_fs;

    /* Creates disk format */
    if (!(fs->format = reiserfs_format_create(host_device, len, profile->tail, profile->format)))
	goto error_free_master;

    /* Creates block allocator */
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
    
    /* Creates journal on journal device */
    if (!(fs->journal = reiserfs_journal_create(journal_device, 
	    journal_params, profile->journal)))
	goto error_free_alloc;
   
    libreiser4_plugin_call(goto error_free_journal, fs->journal->plugin->journal_ops, 
	area, fs->journal->entity, &journal_area_start, &journal_area_end);
    
    /* Setts up journal blocks in block allocator */
    for (blk = journal_area_start; blk <= journal_area_end; blk++)
	reiserfs_alloc_mark(fs->alloc, blk);
   
    /* 
	Initializes oid allocator on got from disk format oid area of disk format 
	specific super block.
    */
    libreiser4_plugin_call(goto error_free_journal, fs->format->plugin->format_ops, 
	oid, fs->format->entity, &oid_area_start, &oid_area_end);
    
    if (!(fs->oid = reiserfs_oid_create(oid_area_start, oid_area_end, 
	    profile->oid)))
	goto error_free_journal;

    /* Initializes root key */
    if (reiserfs_fs_build_root_key(fs, profile->key))
	goto error_free_oid;
    
    /* Creates tree */
    if (!(fs->tree = reiserfs_tree_create(fs, profile)))
	goto error_free_oid;
    
    /* Setts up root block */
    reiserfs_format_set_root(fs->format, 
	aal_block_get_nr(fs->tree->cache->node->block));

    /* Creates root directory */
    {
	reiserfs_plugin_t *dir_plugin;
	reiserfs_object_hint_t dir_hint;
	
	/* Finding directroy plugin */
	if (!(dir_plugin = libreiser4_factory_find(REISERFS_DIR_PLUGIN, profile->dir)))
	    libreiser4_factory_failed(goto error_free_tree, find, dir, profile->dir);
	
	dir_hint.statdata_pid = profile->item.statdata;
	dir_hint.direntry_pid = profile->item.direntry;
	dir_hint.hash_pid = profile->hash;
	
	/* Creating object "dir40". See object.c for details */
	if (!(fs->dir = reiserfs_dir_create(fs, &dir_hint, dir_plugin, NULL, "/"))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create root directory.");
	    goto error_free_tree;
	}
    }
    
    /* Setts up free blocks in block allocator */
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

/* Synchronizes all filesystem objects to device */
errno_t reiserfs_fs_sync(reiserfs_fs_t *fs) {
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

errno_t reiserfs_fs_check(reiserfs_fs_t *fs) {
    return 0;
}

#endif

/* 
    Closes all filesystem's entities. Calls plugins' "done" routine for every 
    plugin and frees all assosiated memory. 
*/
void reiserfs_fs_close(reiserfs_fs_t *fs) {
    
    aal_assert("umka-230", fs != NULL, return);
    
    reiserfs_object_close(fs->dir);
    reiserfs_tree_close(fs->tree);
    reiserfs_oid_close(fs->oid);
    
    if (fs->journal)
	reiserfs_journal_close(fs->journal);
	
    reiserfs_alloc_close(fs->alloc);
    reiserfs_format_close(fs->format);
    reiserfs_master_close(fs);
    aal_free(fs);
}

/* Returns format string from disk format object (for instance, reiserfs 4.0) */
const char *reiserfs_fs_format(reiserfs_fs_t *fs) {
    return reiserfs_format_format(fs->format);
}

/* Returns disk format plugin in use */
reiserfs_id_t reiserfs_fs_format_pid(reiserfs_fs_t *fs) {
    aal_assert("umka-151", fs != NULL, return -1);
    aal_assert("umka-152", fs->master != NULL, return -1);

    return get_mr_format_id(fs->master);
}

/* Returns filesystem block size value */
uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs) {
    aal_assert("umka-153", fs != NULL, return 0);
    aal_assert("umka-154", fs->master != NULL, return 0);
    
    return get_mr_block_size(fs->master);
}

