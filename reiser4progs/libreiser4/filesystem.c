/*
    filesystem.c -- common reiser4 filesystem code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

/*
    Builds root directory key. It is used for lookups and other as init key. This 
    method id needed because of root key in reiser3 and reiser4 has a diffrent 
    locality and object id values.
*/
errno_t reiser4_fs_build_root_key(
    reiser4_fs_t *fs,		/* filesystem to be used */
    reiser4_id_t pid		/* key plugin id to be used */
) {
    oid_t objectid;
    oid_t parent_objectid;
    reiser4_plugin_t *plugin;
    
    /* Finding needed key plugin by its identifier */
    if (!(plugin = libreiser4_factory_find_by_id(KEY_PLUGIN_TYPE, pid)))
	libreiser4_factory_failed(return -1, find, key, pid);

    /* Getting root directory attributes from oid allocator */
    parent_objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid_ops, root_locality,);

    objectid = libreiser4_plugin_call(return -1,
	fs->oid->plugin->oid_ops, root_objectid,);

    /* Initializing the key by found plugin */
    fs->key.plugin = plugin;

    /* Building the key */
    reiser4_key_build_generic(&fs->key, KEY40_STATDATA_MINOR,
	parent_objectid, objectid, 0);

    return 0;
}

/* 
    Opens filesysetm on specified host device and journal device. Replays the 
    journal if "replay" flag is specified.
*/
reiser4_fs_t *reiser4_fs_open(
    aal_device_t *host_device,	    /* device filesystem will lie on */
    aal_device_t *journal_device,   /* device journal will lie on */
    int replay			    /* flag that specify whether replaying is needed */
) {
    reiser4_fs_t *fs;
    reiser4_id_t pid;

    aal_assert("umka-148", host_device != NULL, return NULL);

    /* Allocating memory and initializing fields */
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;


    /* Reads master super block. See above for details */
    if (!(fs->master = reiser4_master_open(host_device)))
	goto error_free_fs;
    
    if (reiser4_master_valid(fs->master))
	goto error_free_master;
    
    /* Setting actual used block size from master super block */
    if (aal_device_set_bs(host_device, reiser4_master_blocksize(fs->master))) {
        aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	   "Invalid block size detected %u. It must be power of two.", 
	    reiser4_master_blocksize(fs->master));
	goto error_free_master;
    }
    
    /* Initializes used disk format. See format.c for details */
    pid = reiser4_master_format(fs->master);

    if (!(fs->format = reiser4_format_open(host_device, pid)))
	goto error_free_master;

    if (reiser4_format_valid(fs->format, 0))
	goto error_free_format;
    
    /* Initializes block allocator. See alloc.c for details */
    if (!(fs->alloc = reiser4_alloc_open(fs->format)))
	goto error_free_format;
    
    if (reiser4_alloc_valid(fs->alloc, 0))
	goto error_free_alloc;
    
    /* Jouranl device may be not specified. In this case it will not be opened */
    if (journal_device) {
	    
	/* Setting up block size in use for journal device */
	aal_device_set_bs(journal_device, reiser4_fs_blocksize(fs));

	/* Initializing the journal. See  journal.c for details */
	if (!(fs->journal = reiser4_journal_open(fs->format, journal_device)))
	    goto error_free_alloc;
    
	if (reiser4_journal_valid(fs->journal, 0))
	    goto error_free_journal;

#ifndef ENABLE_COMPACT	
	/* 
	    Reopening super block after journal replaying. It is needed because
	    journal may contain super block in unflushed transactions.
	*/
	if (replay) {
	    if (reiser4_journal_replay(fs->journal)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't replay journal.");
		goto error_free_journal;
	    }
	    
	    /* 
		Reopening format in odrer to keep it up to date after journal 
		replaying. Journal might contain super block ior master super
		block.
	    */
	    
	    /* FIXME-UMKA: Here also master super block should be reopened */
	    
	    if (!(fs->format = reiser4_format_reopen(fs->format, host_device)))
		goto error_free_journal;
	}
#endif

    }
    
    /* Initializes oid allocator */
    if (!(fs->oid = reiser4_oid_open(fs->format)))
	goto error_free_journal;
  
    if (reiser4_oid_valid(fs->oid, 0))
	goto error_free_oid;
    
    /* 
	Initilaizes root directory key.
	FIXME-UMKA: Here should be not hardcoded key id.
    */
    if (reiser4_fs_build_root_key(fs, KEY_REISER40_ID))
	goto error_free_oid;
    
    /* Opens the tree starting from root block */
    if (!(fs->tree = reiser4_tree_open(fs)))
	goto error_free_oid;
    
    /* Opens root firectory */
    if (!(fs->dir = reiser4_dir_open(fs, "/")))
	goto error_free_tree;
    
    return fs;

error_free_tree:
    reiser4_tree_close(fs->tree);
error_free_oid:
    reiser4_oid_close(fs->oid);
error_free_journal:
    if (fs->journal)
	reiser4_journal_close(fs->journal);
error_free_alloc:
    reiser4_alloc_close(fs->alloc);
error_free_format:
    reiser4_format_close(fs->format);
error_free_master:
    reiser4_master_close(fs->master);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

aal_device_t *reiser4_fs_host_device(reiser4_fs_t *fs) {
    aal_assert("umka-970", fs != NULL, return NULL);
    aal_assert("umka-971", fs->format != NULL, return NULL);

    return fs->format->device;
}

aal_device_t *reiser4_fs_journal_device(reiser4_fs_t *fs) {
    aal_assert("umka-972", fs != NULL, return NULL);

    return (fs->journal ? fs->journal->device : NULL);
}

#ifndef ENABLE_COMPACT

#define reiser4_MIN_SIZE (23 + 100)

/* Creates filesystem on specified host and journal devices */
reiser4_fs_t *reiser4_fs_create(
    reiser4_profile_t *profile,    /* profile to be used for new filesystem */
    aal_device_t *host_device,	    /* device filesystem will be lie on */
    size_t blocksize,		    /* blocksize to be used in new filesystem */
    const char *uuid,		    /* uuid to be used */
    const char *label,		    /* label to be used */
    count_t len,		    /* filesystem length in blocks */
    aal_device_t *journal_device,   /* device journal will be lie on */
    void *journal_params	    /* journal params (most probably will be used for r3) */
) {
    reiser4_fs_t *fs;
    blk_t blk, master_offset;
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
    if (len < reiser4_MIN_SIZE) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Device %s is too small (%llu). ReiserFS required device %u blocks long.", 
	    aal_device_name(host_device), len, reiser4_MIN_SIZE);
	return NULL;
    }
    
    /* Allocating memory and initializing fileds */
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;
	
    /* Creates master super block */
    if (!(fs->master = reiser4_master_create(host_device, profile->format, 
	    blocksize, uuid, label)))
	goto error_free_fs;

    /* Creates disk format */
    if (!(fs->format = reiser4_format_create(host_device, len, 
	    profile->drop_policy, profile->format)))
	goto error_free_master;

    /* Creates block allocator */
    if (!(fs->alloc = reiser4_alloc_create(fs->format)))
	goto error_free_format;
    
    /* Creates journal on journal device */
    if (!(fs->journal = reiser4_journal_create(fs->format, 
	    journal_device, journal_params)))
	goto error_free_alloc;
   
    reiser4_format_mark(fs->format, fs->alloc);
    
    /* Initializes oid allocator */
    if (!(fs->oid = reiser4_oid_create(fs->format)))
	goto error_free_journal;

    /* Initializes root key */
    if (reiser4_fs_build_root_key(fs, profile->key))
	goto error_free_oid;
    
    /* Creates tree */
    if (!(fs->tree = reiser4_tree_create(fs, profile)))
	goto error_free_oid;
    
    /* Setts up root block */
    reiser4_format_set_root(fs->format, 
	aal_block_get_nr(fs->tree->cache->node->block));

    /* Creates root directory */
    {
	reiser4_plugin_t *dir_plugin;
	reiser4_object_hint_t dir_hint;
	
	/* Finding directroy plugin */
	if (!(dir_plugin = libreiser4_factory_find_by_id(DIR_PLUGIN_TYPE, profile->dir.dir)))
	    libreiser4_factory_failed(goto error_free_tree, find, dir, profile->dir.dir);
	
	dir_hint.statdata_pid = profile->item.statdata;
	dir_hint.sdext = profile->sdext;

	dir_hint.direntry_pid = profile->item.direntry;
	dir_hint.hash_pid = profile->hash;
	
	/* Creating object "dir40". See object.c for details */
	if (!(fs->dir = reiser4_dir_create(fs, &dir_hint, dir_plugin, NULL, "/"))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create root directory.");
	    goto error_free_tree;
	}
    }
    
    /* Setts up free blocks in block allocator */
    reiser4_format_set_free(fs->format, reiser4_alloc_free(fs->alloc));
    
    return fs;

error_free_tree:
    reiser4_tree_close(fs->tree);
error_free_oid:
    reiser4_oid_close(fs->oid);
error_free_journal:
    reiser4_journal_close(fs->journal);
error_free_alloc:
    reiser4_alloc_close(fs->alloc);
error_free_format:
    reiser4_format_close(fs->format);
error_free_master:
    reiser4_master_close(fs->master);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

/* 
    Synchronizes all filesystem objects to corresponding devices (all filesystem
    objects except journal - to host device and journal - to journal device).
*/
errno_t reiser4_fs_sync(
    reiser4_fs_t *fs		/* filesystem instance to be synchronized */
) {
    aal_assert("umka-231", fs != NULL, return -1);
   
    /* Synchronizing the tree */
    if (reiser4_tree_sync(fs->tree))
	return -1;
    
    /* Synchronizing the journal */
    if (fs->journal && reiser4_journal_sync(fs->journal))
	return -1;
    
    /* Synchronizing block allocator */
    if (reiser4_alloc_sync(fs->alloc))
	return -1;
    
    /* Synchronizing the object allocator */
    if (reiser4_oid_sync(fs->oid))
	return -1;
    
    if (reiser4_format_sync(fs->format))
	return -1;

    /* 
	Synchronizing the master super block. As master may not be exist on device,
	because reiser3 filesystem was created, we should make sure, that filesystem
	on the host device is realy reiser4 filesystem.
    */
    if (reiser4_master_confirm(fs->format->device)) {
	if (reiser4_master_sync(fs->master))
	    return -1;
    }

    return 0;
}

#endif

/* 
    Closes all filesystem's entities. Calls plugins' "done" routine for every 
    plugin and frees all assosiated memory. 
*/
void reiser4_fs_close(
    reiser4_fs_t *fs		/* filesystem to be closed */
) {
    
    aal_assert("umka-230", fs != NULL, return);
    
    /* Closong the all filesystem objects */
    reiser4_object_close(fs->dir);
    reiser4_tree_close(fs->tree);
    reiser4_oid_close(fs->oid);
    
    if (fs->journal)
	reiser4_journal_close(fs->journal);
	
    reiser4_alloc_close(fs->alloc);
    reiser4_format_close(fs->format);
    reiser4_master_close(fs->master);

    /* Freeing memory occupied by fs instance */
    aal_free(fs);
}

/* Returns format string from disk format object (for instance, reiserfs 4.0) */
const char *reiser4_fs_name(
    reiser4_fs_t *fs		/* filesystem format name will be obtained from */
) {
    return reiser4_format_name(fs->format);
}

/* Returns disk format plugin in use */
reiser4_id_t reiser4_fs_format_pid(
    reiser4_fs_t *fs		/* filesystem disk format pid will be obtained from */
) {
    aal_assert("umka-151", fs != NULL, return -1);
    aal_assert("umka-152", fs->master != NULL, return -1);

    return reiser4_master_format(fs->master);
}

/* Returns filesystem block size value */
uint16_t reiser4_fs_blocksize(
    reiser4_fs_t *fs		/* filesystem blocksize will be obtained from */
) {
    aal_assert("umka-153", fs != NULL, return 0);
    aal_assert("umka-154", fs->master != NULL, return 0);
    
    return reiser4_master_blocksize(fs->master);
}

