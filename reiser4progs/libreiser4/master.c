/*
    master.c -- master super block functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

/* Forms master super block disk structure */
reiser4_master_t *reiser4_master_create(
    aal_device_t *device,	    /* device master will be created on */
    reiser4_id_t format_pid,	    /* disk format plugin id to be used */
    unsigned int blocksize,	    /* blocksize to be used */
    const char *uuid,		    /* uuid to be used */
    const char *label		    /* filesystem label to be used */
) {
    reiser4_master_t *master;
    
    aal_assert("umka-981", device != NULL, return NULL);
    
    /* Allocating the memory for master super block struct */
    if (!(master = aal_calloc(sizeof(*master), 0)))
	return NULL;
    
    master->device = device;
    
    if (!(master->block = aal_block_alloc(device, 
	    REISER4_MASTER_OFFSET / blocksize, 0)))
	goto error_free_master;
    
    master->super = (reiser4_master_super_t *)master->block->data;
    
    /* Setting up magic */
    aal_strncpy(master->super->mr_magic, REISER4_MASTER_MAGIC,
	aal_strlen(REISER4_MASTER_MAGIC));
    
    /* Setting up uuid and label */
    if (uuid) {
	aal_strncpy(master->super->mr_uuid, uuid, 
	    sizeof(master->super->mr_uuid));
    }
    
    if (label) {
	aal_strncpy(master->super->mr_label, label, 
	    sizeof(master->super->mr_label));
    }
    
    /* Setting up plugin id for used disk format plugin */
    set_mr_format_id(master->super, format_pid);

    /* Setting up block filesystem used */
    set_mr_blocksize(master->super, blocksize);
	
    return master;
    
error_free_master:
    aal_free(master);
    return NULL;
}

/* This function checks master super block for validness */
errno_t reiser4_master_valid(reiser4_master_t *master) {
    aal_assert("umka-898", master != NULL, return -1);
    return 0;
}

/* Callback function for comparing plugins */
static errno_t callback_try_for_guess(
    reiser4_plugin_t *plugin,	    /* plugin to be checked */
    void *data			    /* needed plugin type */
) {
    if (plugin->h.type == FORMAT_PLUGIN_TYPE) {
	aal_device_t *device = (aal_device_t *)data;

	if (libreiser4_plugin_call(return 0, plugin->format_ops, 
		confirm, device))
	    return 1;
    }
    
    return 0;
}

reiser4_plugin_t *reiser4_master_guess(aal_device_t *device) {
    return libreiser4_factory_suitable(callback_try_for_guess, device);
}

#endif

/* Makes probing of reiser4 master super block on given device */
int reiser4_master_confirm(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    reiser4_master_super_t *super;
    
    aal_assert("umka-901", device != NULL, return 0);
    
    offset = (blk_t)(REISER4_MASTER_OFFSET / 
	REISER4_DEFAULT_BLOCKSIZE);

    /* Setting up default block size (4096) to used device */
    aal_device_set_bs(device, REISER4_DEFAULT_BLOCKSIZE);
    
    /* Reading the block where master super block lies */
    if (!(block = aal_block_read(device, offset))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't read master super block at %llu.", offset);
	return 0;
    }
    
    super = (reiser4_master_super_t *)block->data;

    if (aal_strncmp(super->mr_magic, REISER4_MASTER_MAGIC, 4) == 0) {

	if (aal_device_set_bs(device, get_mr_blocksize(super))) {
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
		"Invalid block size detected %u. It must be power of two.", 
		get_mr_blocksize(super));
	    goto error_free_block;
	}
	
	aal_block_free(block);
	return 1;
    }
    
    aal_block_free(block);
    return 0;
    
error_free_block:
    aal_block_free(block);
    return 0;
}

/* Reads master super block from disk */
reiser4_master_t *reiser4_master_open(aal_device_t *device) {
    blk_t offset;
    reiser4_master_t *master;
    
    aal_assert("umka-143", device != NULL, return NULL);
    
    if (!(master = aal_calloc(sizeof(*master), 0)))
	return NULL;
    
    offset = (blk_t)(REISER4_MASTER_OFFSET / 
	REISER4_DEFAULT_BLOCKSIZE);

    /* Setting up default block size (4096) to used device */
    aal_device_set_bs(device, REISER4_DEFAULT_BLOCKSIZE);
    
    /* Reading the block where master super block lies */
    if (!(master->block = aal_block_read(device, offset))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't read master super block at %llu.", offset);
	goto error_free_master;
    }
    
    master->device = device;
    master->super = (reiser4_master_super_t *)master->block->data;

    /* Checking for reiser3 disk-format */
    if (aal_strncmp(master->super->mr_magic, REISER4_MASTER_MAGIC, 4) != 0) {
	/* 
	    Reiser4 doesn't found on passed device. In this point should be 
	    called function which detectes used format on th device.
	*/
#ifndef ENABLE_COMPACT
	{
	    reiser4_plugin_t *plugin;
	    
	    if (!(plugin = reiser4_master_guess(device)))
		goto error_free_block;
	    
	    /* Creating in-memory master super block */
	    if (!(master = reiser4_master_create(device, plugin->h.id, 
		REISER4_DEFAULT_BLOCKSIZE, NULL, NULL)))
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find reiser4 nor reiser3 filesystem.");
		goto error_free_block;
	    }
	    
	    return master;
	}
#endif
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find reiser4 filesystem.");
	goto error_free_block;
    }
    
    return master;
    
error_free_block:
    aal_block_free(master->block);
error_free_master:
    aal_free(master);
    return NULL;
}

#ifndef ENABLE_COMPACT

/* Saves master super block to device. */
errno_t reiser4_master_sync(
    reiser4_master_t *master	    /* master to be saved */
) {
    aal_assert("umka-145", master != NULL, return -1);
    aal_assert("umka-900", master->device != NULL, return -1);

    /* 
	Writing master super block to host device. Host device is device where
	filesystem lies. There is also journal device.
    */
    if (aal_block_write(master->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize master super block at %llu. %s.", 
	    aal_block_get_nr(master->block), aal_device_error(master->device));
	return -1;
    }

    return 0;
}

#endif

/* Frees master super block occupied memory */
void reiser4_master_close(
    reiser4_master_t *master		/* master to be closed */
) {
    aal_assert("umka-147", master != NULL, return);

    aal_block_free(master->block);
    aal_free(master);
}

char *reiser4_master_magic(reiser4_master_t *master) {
    aal_assert("umka-982", master != NULL, return NULL);

    return master->super->mr_magic;
}

reiser4_id_t reiser4_master_format(reiser4_master_t *master) {
    aal_assert("umka-982", master != NULL, return INVALID_PLUGIN_ID);
    return get_mr_format_id(master->super);
}

uint32_t reiser4_master_blocksize(reiser4_master_t *master) {
    aal_assert("umka-983", master != NULL, return 0);
    return get_mr_blocksize(master->super);
}

char *reiser4_master_uuid(reiser4_master_t *master) {
    aal_assert("umka-984", master != NULL, return 0);
    return master->super->mr_uuid;
}

char *reiser4_master_label(reiser4_master_t *master) {
    aal_assert("umka-985", master != NULL, return 0);
    return master->super->mr_label;
}

