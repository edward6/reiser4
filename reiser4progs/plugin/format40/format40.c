/*
    format40.c -- default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static errno_t format40_super_check(reiserfs_format40_super_t *super, 
    aal_device_t *device) 
{
    blk_t offset;
    blk_t dev_len = aal_device_len(device);
    
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %llu for device "
	    "length %llu blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }
    
    offset = (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(device));
    if (get_sb_root_block(super) < offset || get_sb_root_block(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Superblock has an invalid root block %llu for device "
	    "length %llu blocks.", get_sb_root_block(super), dev_len);
	return -1;
    }
    return 0;
}

static int format40_signature(reiserfs_format40_super_t *super) {
    return aal_strncmp(super->sb_magic, 
	REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC)) == 0;
}

static aal_block_t *format40_super_open(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    reiserfs_format40_super_t *super;
    
    offset = (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(device));
	
    if (!(block = aal_block_read(device, offset))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't read block %llu. %s.", offset, aal_device_error(device));
	return NULL;
    }
    super = (reiserfs_format40_super_t *)block->data;
    
    if (!format40_signature(super))
	return NULL;
    
    if (format40_super_check(super, device)) {
        aal_block_free(block);
        return NULL;
    }
    
    return block;
}

static reiserfs_format40_t *format40_open(aal_device_t *device) {
    reiserfs_format40_t *format;

    aal_assert("umka-393", device != NULL, return NULL);

    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    format->device = device;
    
    if (!(format->super = format40_super_open(device)))
	goto error_free_format;
		
    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

/* This function should create super block and update all copies */
static reiserfs_format40_t *format40_create(aal_device_t *device, 
    count_t blocks, uint16_t tail_policy)
{
    blk_t blk;
    reiserfs_format40_t *format;
    reiserfs_format40_super_t *super;
    
    aal_assert("umka-395", device != NULL, return NULL);
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;

    if (!(format->super = aal_block_alloc(device, (REISERFS_FORMAT40_OFFSET / 
	aal_device_get_bs(device)), 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    
    super = (reiserfs_format40_super_t *)format->super->data;
    aal_memcpy(super->sb_magic, REISERFS_FORMAT40_MAGIC, 
	aal_strlen(REISERFS_FORMAT40_MAGIC));

    set_sb_block_count(super, blocks);
    set_sb_tree_height(super, 2);
    set_sb_flushes(super, 0);
    set_sb_tail_policy(super, tail_policy);

    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

/* This function should update all copies of the super block */
static errno_t format40_sync(reiserfs_format40_t *format) {
    blk_t offset;
    aal_assert("umka-394", format != NULL, return -1); 
   
    if (aal_block_write(format->super)) {
	offset = aal_block_get_nr(format->super);
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %llu. %s.", offset, 
	    aal_device_error(format->device));
	return -1;
    }
    
    return 0;
}

#endif

static errno_t format40_check(reiserfs_format40_t *format) {
    aal_assert("umka-397", format != NULL, return -1);
    
    return format40_super_check((reiserfs_format40_super_t *)format->super->data, 
	format->device);
}

static void format40_close(reiserfs_format40_t *format) {
    aal_assert("umka-398", format != NULL, return);
    
    aal_block_free(format->super);
    aal_free(format);
}

static int format40_confirm(aal_device_t *device) {
    aal_block_t *block;

    aal_assert("umka-733", device != NULL, return 0);
    
    if (!(block = format40_super_open(device)))
	return 0;
	
    aal_block_free(block);
    return 1;
}

static void format40_oid(reiserfs_format40_t *format, 
    void **oid_area_start, void **oid_area_end) 
{
    aal_assert("umka-732", format != NULL, return);
    
    *oid_area_start = &((reiserfs_format40_super_t *)format->super->data)->sb_oid;
    *oid_area_end = &((reiserfs_format40_super_t *)format->super->data)->sb_file_count;
}

static const char *formats[] = {"4.0"};

static const char *format40_format(reiserfs_format40_t *format) {
    return formats[0];
}

static reiserfs_id_t format40_journal_plugin(reiserfs_format40_t *format) {
    return REISERFS_FORMAT40_JOURNAL;
}

static reiserfs_id_t format40_alloc_plugin(reiserfs_format40_t *format) {
    return REISERFS_FORMAT40_ALLOC;
}

static reiserfs_id_t format40_oid_plugin(reiserfs_format40_t *format) {
    return REISERFS_FORMAT40_OID;
}

static blk_t format40_offset(reiserfs_format40_t *format) {
    aal_assert("umka-399", format != NULL, return 0);
    return (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(format->device));
}

static blk_t format40_get_root(reiserfs_format40_t *format) {
    aal_assert("umka-400", format != NULL, return 0);
    return get_sb_root_block((reiserfs_format40_super_t *)format->super->data);
}

static count_t format40_get_blocks(reiserfs_format40_t *format) {
    aal_assert("umka-401", format != NULL, return 0);
    return get_sb_block_count((reiserfs_format40_super_t *)format->super->data);
}

static count_t format40_get_free(reiserfs_format40_t *format) {
    aal_assert("umka-402", format != NULL, return 0);
    return get_sb_free_blocks((reiserfs_format40_super_t *)format->super->data);
}

static uint16_t format40_get_height(reiserfs_format40_t *format) {
    aal_assert("umka-555", format != NULL, return 0);
    return get_sb_tree_height((reiserfs_format40_super_t *)format->super->data);
}

#ifndef ENABLE_COMPACT

static void format40_set_root(reiserfs_format40_t *format, blk_t root) {
    aal_assert("umka-403", format != NULL, return);
    set_sb_root_block((reiserfs_format40_super_t *)format->super->data, root);
}

static void format40_set_blocks(reiserfs_format40_t *format, count_t blocks) {
    aal_assert("umka-404", format != NULL, return);
    set_sb_block_count((reiserfs_format40_super_t *)format->super->data, blocks);
}

static void format40_set_free(reiserfs_format40_t *format, count_t blocks) {
    aal_assert("umka-405", format != NULL, return);
    set_sb_free_blocks((reiserfs_format40_super_t *)format->super->data, blocks);
}

static void format40_set_height(reiserfs_format40_t *format, uint16_t height) {
    aal_assert("umka-555", format != NULL, return);
    set_sb_tree_height((reiserfs_format40_super_t *)format->super->data, height);
}

#endif

static reiserfs_plugin_t format40_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "format40",
	    .desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))format40_open,

#ifndef ENABLE_COMPACT	
	.sync = (errno_t (*)(reiserfs_opaque_t *))format40_sync,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t, uint16_t))format40_create,
#else
	.sync = NULL,
	.create = NULL,
#endif
	.oid = (void (*)(reiserfs_opaque_t *, void **, void **))format40_oid,
	.close = (void (*)(reiserfs_opaque_t *))format40_close,
	.check = (errno_t (*)(reiserfs_opaque_t *))format40_check,
	.confirm = (int (*)(aal_device_t *))format40_confirm,
	.format = (const char *(*)(reiserfs_opaque_t *))format40_format,
	
	.offset = (blk_t (*)(reiserfs_opaque_t *))format40_offset,
	
	.get_root = (blk_t (*)(reiserfs_opaque_t *))format40_get_root,
	.get_blocks = (count_t (*)(reiserfs_opaque_t *))format40_get_blocks,
	.get_free = (count_t (*)(reiserfs_opaque_t *))format40_get_free,
	.get_height = (uint16_t (*)(reiserfs_opaque_t *))format40_get_height,
	
#ifndef ENABLE_COMPACT	
	.set_root = (void (*)(reiserfs_opaque_t *, blk_t))format40_set_root,
	.set_blocks = (void (*)(reiserfs_opaque_t *, count_t))format40_set_blocks,
	.set_free = (void (*)(reiserfs_opaque_t *, count_t))format40_set_free,
	.set_height = (void (*)(reiserfs_opaque_t *, uint16_t))format40_set_height,
#else
	.set_root = NULL,
	.set_blocks = NULL,
	.set_free = NULL,
	.set_height = NULL,
#endif
	.journal_plugin_id = (reiserfs_id_t(*)(reiserfs_opaque_t *))
	    format40_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_id_t(*)(reiserfs_opaque_t *))
	    format40_alloc_plugin,
	
	.oid_plugin_id = (reiserfs_id_t(*)(reiserfs_opaque_t *))
	    format40_oid_plugin,
    }
};

static reiserfs_plugin_t *format40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &format40_plugin;
}

libreiser4_factory_register(format40_entry);

