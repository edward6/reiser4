/*
    format40.c -- default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "format40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_format40_super_check(reiserfs_format40_super_t *super, 
    aal_device_t *device) 
{
    blk_t dev_len = aal_device_len(device);
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %d for device "
	    "length %d blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }
    return 0;
}

static int reiserfs_format40_signature(reiserfs_format40_super_t *super) {
    return aal_strncmp(super->sb_magic, REISERFS_FORMAT40_MAGIC, 16) == 0;
}

static aal_block_t *reiserfs_format40_super_open(aal_device_t *device) {
    aal_block_t *block;
    reiserfs_format40_super_t *super;
    int i, super_offset[] = {17, -1};
	
    for (i = 0; super_offset[i] != -1; i++) {
	if ((block = aal_device_read_block(device, super_offset[i]))) {
	    super = (reiserfs_format40_super_t *)block->data;
		
	    if (reiserfs_format40_signature(super)) {
		if (reiserfs_format40_super_check(super, device)) {
		    aal_device_free_block(block);
		    continue;
		}	
		return block;
	    }
			
	    aal_device_free_block(block);
	}
    }
    return NULL;
}

static reiserfs_format40_t *reiserfs_format40_open(aal_device_t *device) {
    reiserfs_format40_t *format;
	
    if (!device)
	return NULL;
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    if (!(format->super = reiserfs_format40_super_open(device)))
	goto error_free_format;
		
    format->device = device;
    return format;
	
error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format40_sync(reiserfs_format40_t *format) {
    if (!format || !format->super)
	return -1;
    
    if (!aal_device_write_block(format->device, format->super)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %d.", 
	    aal_device_get_block_location(format->device, format->super));
	return -1;
    }
    return 0;
}

static reiserfs_format40_t *reiserfs_format40_create(aal_device_t *device, count_t blocks) {
    reiserfs_format40_t *format;
    reiserfs_format40_super_t *super;

    if (!device)
	return NULL;
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;
    
    if (!(format->super = aal_device_alloc_block(device, REISERFS_FORMAT40_OFFSET, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    super = (reiserfs_format40_super_t *)format->super->data;
    set_sb_block_count(super, blocks);
    
    /* There will be superblock forming code */
   
    if (!reiserfs_format40_sync(format))
	goto error_free_super;

    return format;
    
error_free_super:
    aal_device_free_block(format->super);
error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format40_check(reiserfs_format40_t *format) {
    return reiserfs_format40_super_check((reiserfs_format40_super_t *)format->super->data, 
	format->device);
}

static void reiserfs_format40_close(reiserfs_format40_t *format, int sync) {
    if (sync) 
	reiserfs_format40_sync(format);
    
    aal_device_free_block(format->super);
    aal_free(format);
}

static int reiserfs_format40_probe(aal_device_t *device) {
    aal_block_t *block;

    if (!(block = reiserfs_format40_super_open(device)))
	return 0;
	
    aal_device_free_block(block);
    return 1;
}

static const char *formats[] = {"4.0"};

static const char *reiserfs_format40_format(reiserfs_format40_t *format) {
    return formats[0];
}

static reiserfs_plugin_id_t reiserfs_format40_journal_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format40_alloc_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format40_node_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static blk_t reiserfs_format40_root_block(reiserfs_format40_t *format) {
    return get_sb_root_block((reiserfs_format40_super_t *)format->super->data);
}

static reiserfs_plugin_t format40_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "Format40",
	    .desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_format_opaque_t *(*)(aal_device_t *))reiserfs_format40_open,
	.create = (reiserfs_format_opaque_t *(*)(aal_device_t *, count_t))reiserfs_format40_create,
	.close = (void (*)(reiserfs_format_opaque_t *, int))reiserfs_format40_close,
	.sync = (error_t (*)(reiserfs_format_opaque_t *))reiserfs_format40_sync,
	.check = (error_t (*)(reiserfs_format_opaque_t *))reiserfs_format40_check,
	.probe = (int (*)(aal_device_t *))reiserfs_format40_probe,
	.format = (const char *(*)(reiserfs_format_opaque_t *))reiserfs_format40_format,
	
	.root_block = (blk_t (*)(reiserfs_format_opaque_t *))reiserfs_format40_root_block,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
	    reiserfs_format40_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
	    reiserfs_format40_alloc_plugin,
	
	.node_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
	    reiserfs_format40_node_plugin,
    }
};

reiserfs_plugin_t *reiserfs_format40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &format40_plugin;
}

reiserfs_plugin_register(reiserfs_format40_entry);

