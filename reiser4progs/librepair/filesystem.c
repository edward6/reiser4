/*
    libprogs/filesystem.c - methods are needed mostly by fsck for work 
    with broken filesystems.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <repair/librepair.h>
#include <reiser4/reiser4.h>

errno_t repair_fs_check(reiser4_fs_t *fs) {
    return 0;
}

static errno_t repair_master_check(reiserfs_fs_t *fs, callback_ask_user_t ask_blocksize) 
{
    uint16_t blocksize = 0;
    int error = 0;
    int free_master = 0;
    reiserfs_plugin_t *plugin;
   
    aal_assert("vpf-161", fs != NULL, return -1);
    aal_assert("vpf-163", ask_blocksize != NULL, return -1);
    aal_assert("vpf-164", repair_data(fs) != NULL, return -1);
    aal_assert("vpf-170", repair_data(fs)->host_device != NULL, return -1);
    
    if (!fs->master) {
	/* Master SB was not opened. Create a new one. */
	if (aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_YES|EXCEPTION_NO, 
	    "Master super block cannot be found. Do you want to build a new one on (%s)?",
	    aal_device_name(repair_data(fs)->host_device)) == EXCEPTION_NO) 
	    return -1;

        if (!(blocksize = ask_blocksize(fs, &error)) && error)
	    return -1;

	/* 
	    FIXME-VITALY: What should be done here with uuid and label? 
	    At least not here as uiud and label seem to be on the wrong place.
	    Move them to specific SB.
	*/
	
	/* Create a new master SB. */
	if (!(fs->master = reiserfs_master_create(repair_data(fs)->host_device, 
	    INVALID_PLUGIN_ID, blocksize, NULL, NULL))) 
	{
	    aal_exception_fatal("Cannot create a new master super block.");
	    return -1;
	} else if (repair_verbose(repair_data(fs)))
	    aal_exception_info("A new master superblock was created on (%s).", 
		aal_device_name(repair_data(fs)->host_device));
	free_master = 1;
    } else {
	/* Master SB was opened. Check it for valideness. */

	/* Check the blocksize. */
	if (!aal_pow_of_two(reiserfs_master_blocksize(fs->master))) {
	    aal_exception_fatal("Invalid blocksize found in the master super block (%u).", 
		reiserfs_master_blocksize(fs->master));
	    
	    if (!(blocksize = ask_blocksize(fs, &error)) && error)
		return -1;
	}	
    }

    /* Setting actual used block size from master super block */
    if (blocksize && aal_device_set_bs(repair_data(fs)->host_device, 
	reiserfs_master_blocksize(fs->master))) 
    {
        aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	   "Invalid block size was specified (%u). It must be power of two.", 
	    reiserfs_master_blocksize(fs->master));
	return -1;
    }
    
    return 0;
}    

static errno_t repair_alloc_check(reiserfs_fs_t *fs) {
    return 0;
}

static errno_t repair_oid_check(reiserfs_fs_t *fs) {
    return 0;
}

reiserfs_fs_t *repair_fs_open(repair_data_t *data, callback_ask_user_t ask_blocksize) {
    reiserfs_fs_t *fs;
    void *oid_area_start, *oid_area_end;

    aal_assert("vpf-159", data != NULL, return NULL);
    aal_assert("vpf-172", data->host_device != NULL, return NULL);
    
    /* Allocating memory and initializing fields */
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;
    
    fs->data = data;
    /* Try to open master and rebuild if needed. */
    fs->master = reiserfs_master_open(data->host_device);
	
    /* Check opened master or build a new one. */
    if (repair_master_check(fs, ask_blocksize))
	goto error_free_master;
    
    /* Try to open the disk format. */
    fs->format = reiserfs_format_open(data->host_device, reiserfs_master_format(fs->master));
    
    /* Check the opened disk format or rebuild it if needed. */
    if (repair_format_check(fs))
	goto error_free_format;
    
    fs->alloc = reiserfs_alloc_open(fs->format);
    
    if (repair_alloc_check(fs))
	goto error_free_alloc;

    /* Initializes oid allocator */
    fs->oid = reiserfs_oid_open(fs->format);
  
    if (repair_oid_check(fs))
	goto error_free_oid;
    
    /* FIXME-VITALY: Get key id in a not hardcoded way. */
    if (reiserfs_fs_build_root_key(fs, KEY_REISER40_ID))
	goto error_free_oid;
     
    return fs;
    
error_free_oid:
    if (fs->oid)
	reiserfs_oid_close(fs->oid);
error_free_alloc:
    if (fs->alloc)
	reiserfs_alloc_close(fs->alloc);
error_free_format:
    if (fs->format)
	reiserfs_format_close(fs->format);
error_free_master:
    if (fs->master)
	reiserfs_master_close(fs->master);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

errno_t repair_fs_sync(reiserfs_fs_t *fs) 
{
    aal_assert("vpf-173", fs != NULL, return -1);
    
    /* Synchronizing block allocator */
    if (reiserfs_alloc_sync(fs->alloc))
	return -1;
    
    /* Synchronizing the object allocator */
    if (reiserfs_oid_sync(fs->oid))
	return -1;

    /* Synchronizing the disk format */
    if (reiserfs_format_sync(fs->format))
	return -1;

    if (reiserfs_master_confirm(fs->format->device)) {
	if (reiserfs_master_sync(fs->master))
	    return -1;
    }
    
 return 0;
}

/* 
    Closes all filesystem's entities. Calls plugins' "done" routine for every 
    plugin and frees all assosiated memory. 
*/
void repair_fs_close(reiserfs_fs_t *fs) 
{
    aal_assert("vpf-174", fs != NULL, return);

    reiserfs_oid_close(fs->oid);

    reiserfs_alloc_close(fs->alloc);
    reiserfs_format_close(fs->format);
    reiserfs_master_close(fs->master);

    /* Freeing memory occupied by fs instance */
    aal_free(fs);
}

